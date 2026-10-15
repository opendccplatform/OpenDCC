// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_move_tool_command.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/usd_editor/common_tools/viewport_move_tool_context.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/undo/router.h"
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/base/gf/transform.h>
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "pxr/base/work/loops.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string ViewportMoveToolCommand::cmd_name = "move";

CommandSyntax ViewportMoveToolCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<GfVec3d>("move_delta", "Translation delta")
        .kwarg<SelectionList>("objects", "Affected objects")
        .kwarg<bool>("object_space", "Apply transformation in object space");
}

std::shared_ptr<Command> ViewportMoveToolCommand::create_cmd()
{
    return std::make_shared<ViewportMoveToolCommand>();
}

using namespace manipulator_utils;

void ViewportMoveToolCommand::undo()
{
    m_inverse->invert();
}

void ViewportMoveToolCommand::redo()
{
    m_inverse->invert();
}

void ViewportMoveToolCommand::set_initial_state(const SelectionList& selection, ViewportMoveToolContext::AxisOrientation orientation)
{
    m_orientation = orientation;
    m_selection = selection;
    m_can_edit = false;
    m_start_gizmo_matrix.SetZero();
    if (selection.empty())
        return;

    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;

    m_prim_transforms.clear();
    m_points_delta.clear();

    const auto time = Application::instance().get_current_time();
    UsdGeomXformCache cache(time);
    auto selected_paths = selection.get_fully_selected_paths();
    GfVec3f centroid(0);
    size_t point_count = 0;
    GfMatrix4d world_transform;
    for (const auto& entry : selection)
    {
        const auto& path = entry.first;
        const auto& sel_data = entry.second;
        auto prim = stage->GetPrimAtPath(entry.first);
        if (!prim)
            continue;

        if (!sel_data.get_instance_indices().empty())
        {
            if (auto point_instancer = UsdGeomPointInstancer(prim))
            {
                auto prim_world = cache.GetLocalToWorldTransform(prim);
                const auto is_time_varying = cache.TransformMightBeTimeVarying(prim) || point_instancer.GetPositionsAttr().ValueMightBeTimeVarying();
                if (is_time_varying && !m_instancer_data.empty())
                    continue;
                m_can_edit = !is_time_varying;

                VtMatrix4dArray local_xforms;
                UsdTimeCode instancer_time;
                if (is_time_varying)
                {
                    std::vector<double> samples;
                    instancer_time = point_instancer.GetTimeSamples(&samples) ? Application::instance().get_current_time() : UsdTimeCode::Default();
                }
                else
                {
                    instancer_time = get_non_varying_time(point_instancer.GetPositionsAttr());
                }
                VtIntArray proto_indices;
                point_instancer.GetProtoIndicesAttr().Get(&proto_indices, instancer_time);
                point_instancer.ComputeInstanceTransformsAtTime(&local_xforms, instancer_time, instancer_time,
                                                                UsdGeomPointInstancer::ExcludeProtoXform);
                for (const auto& ind : sel_data.get_instance_indices())
                {
                    const auto world_pos = local_xforms[ind] * prim_world;
                    centroid += GfVec3f(world_pos.ExtractTranslation());
                    ++point_count;
                }

                if (sel_data.get_instance_indices().size() == 1 && m_instancer_data.empty())
                {
                    const auto ind = *sel_data.get_instance_indices().begin();
                    world_transform = local_xforms[ind] * prim_world;
                }
                if (m_can_edit)
                {
                    const auto& instance_indices_set = sel_data.get_instance_indices();
                    InstancerData data;
                    data.point_instancer = point_instancer;
                    data.indices = std::vector<SelectionList::IndexType>(instance_indices_set.begin(), instance_indices_set.end());
                    data.local_xforms = local_xforms;
                    m_instancer_data.emplace_back(std::move(data));
                }
            }
            else if (auto xform = UsdGeomXformable(prim))
            {
                // treat as fully selected prim
                selected_paths.push_back(path);
            }
            continue;
        }
        else if (sel_data.get_point_indices().empty() && sel_data.get_edge_indices().empty() && sel_data.get_element_indices().empty())
        {
            continue;
        }

        auto point_based = UsdGeomPointBased(prim);
        if (!point_based || (point_based.GetPointsAttr().ValueMightBeTimeVarying() && !m_points_delta.empty()))
            continue;
        m_can_edit = !point_based.GetPointsAttr().ValueMightBeTimeVarying();

        world_transform = cache.GetLocalToWorldTransform(prim);
        VtArray<GfVec3f> points;
        if (!TF_VERIFY(point_based.GetPointsAttr().Get(&points, time), "Failed to extract points from prim '%s'.", path.GetText()))
            continue;

        PointsDelta delta;
        delta.point_based = point_based;
        if (Application::instance().is_soft_selection_enabled())
        {
            for (const auto& weight : Application::instance().get_rich_selection().get_weights(entry.first))
            {
                const auto& point = points[weight.first];
                delta.start_points[weight.first].point = point;
                delta.start_points[weight.first].weight = weight.second;
            }

            GfVec3f selected_centroid = { 0.0f, 0.0f, 0.0f };
            size_t selected_points_count = 0;
            std::tie(selected_centroid, selected_points_count) = compute_centroid_data(sel_data, prim, points, world_transform);
            centroid += selected_centroid;
            point_count += selected_points_count;
        }
        else
        {
            const auto add_point_delta = [&delta, &point_count, &centroid, &points, &world_transform](int point_index) {
                auto iter = delta.start_points.find(point_index);
                if (iter == delta.start_points.end())
                {
                    const auto& point = points[point_index];
                    delta.start_points[point_index].point = point;
                    delta.start_points[point_index].weight = 1.0f;
                    centroid += GfVec3f(world_transform.Transform(point));
                    ++point_count;
                }
            };
            visit_all_selected_points(sel_data, prim, add_point_delta);
        }
        if (m_can_edit)
            m_points_delta.push_back(std::move(delta));
    }

    if (selected_paths.empty() && point_count > 0)
    {
        centroid /= point_count;
        if (orientation == ViewportMoveToolContext::AxisOrientation::WORLD || m_points_delta.size() > 1 || m_instancer_data.size() > 1 ||
            (m_instancer_data.size() == 1 && m_instancer_data[0].indices.size() > 1))
        {
            m_start_gizmo_matrix.SetIdentity();
        }
        else
        {
            m_start_gizmo_matrix = world_transform.RemoveScaleShear();
        }
        m_start_gizmo_matrix.SetTranslateOnly(centroid);
        return;
    }

    for (int i = selected_paths.size() - 1; i >= 0; i--)
    {
        const auto& path = selected_paths[i];
        auto prim = stage->GetPrimAtPath(path);
        if (!prim)
            continue;

        auto xform = UsdGeomXformable(prim);
        if (!xform)
            continue;

        const auto is_time_varying = cache.TransformMightBeTimeVarying(prim);
        if (is_time_varying && (i != 0 || !m_prim_transforms.empty()))
            continue;
        m_can_edit = !is_time_varying;

        TransformData prim_transform;
        prim_transform.xform = xform;

        bool reset_xform_stack;
        const auto local_transform = cache.GetLocalTransformation(prim, &reset_xform_stack);

        GfMatrix4d world_transform;
        if (reset_xform_stack)
        {
            prim_transform.parent_transform.SetIdentity();
            world_transform = local_transform;
        }
        else
        {
            prim_transform.parent_transform = cache.GetParentToWorldTransform(prim);
            world_transform = local_transform * prim_transform.parent_transform;
        }
        prim_transform.local = local_transform;

        GfVec3d translation;
        GfVec3f rotation;
        GfVec3f scale;
        UsdGeomXformCommonAPI::RotationOrder rot_order;
        GfVec3d pivot_world_pos;
        auto xform_api = UsdGeomXformCommonAPI(prim);

        if (xform_api.GetXformVectorsByAccumulation(&translation, &rotation, &scale, &prim_transform.local_pivot_pos, &rot_order, time))
        {
            pivot_world_pos = world_transform.Transform(prim_transform.local_pivot_pos);
            if (orientation == ViewportMoveToolContext::AxisOrientation::OBJECT)
                prim_transform.transform = world_transform.RemoveScaleShear();
            else
                prim_transform.transform.SetIdentity();
        }
        else
        {
            prim_transform.local_pivot_pos.Set(0, 0, 0);
            pivot_world_pos = world_transform.ExtractTranslation();
            prim_transform.transform.SetIdentity();
        }

        if (m_can_edit)
        {
            prim_transform.transform.SetTranslateOnly(pivot_world_pos);
            m_prim_transforms.push_back(std::move(prim_transform));
        }
        else
        {
            m_start_gizmo_matrix = prim_transform.transform.SetTranslateOnly(pivot_world_pos);
        }
    }

    if (m_prim_transforms.empty())
        return;

    m_start_gizmo_matrix = m_prim_transforms.front().transform;
    std::sort(m_prim_transforms.begin(), m_prim_transforms.end(), std::greater<TransformData>());
}

void ViewportMoveToolCommand::start_block()
{
    m_change_block = std::make_unique<commands::UsdEditsBlock>();
}

void ViewportMoveToolCommand::end_block()
{
    // HACK:
    // Due to UsdImagingDelegate recreates some rprims (e.g. PointInstancer)
    // we need to update current selection for all viewports.
    // Since only PointInstancer updates require this operation we check if we have any changes
    if (!m_instancer_data.empty())
        commands::UndoRouter::add_inverse(std::make_shared<manipulator_utils::ViewportSelection>());

    m_inverse = m_change_block->take_edits();
    m_change_block = nullptr;
}

bool ViewportMoveToolCommand::is_recording() const
{
    return m_change_block != nullptr;
}

void ViewportMoveToolCommand::apply_delta(const GfVec3d& delta)
{
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;

    m_move_delta = delta;
    const auto time = Application::instance().get_current_time();
    std::vector<std::function<void()>> deferred_edits;
    {
        SdfChangeBlock change_block;
        for (const auto& point_delta : m_points_delta)
        {
            const auto world = point_delta.point_based.ComputeLocalToWorldTransform(time);
            const auto inv_world = world.GetInverse();
            auto point_attr = point_delta.point_based.GetPointsAttr();
            VtArray<GfVec3f> points;
            point_attr.Get(&points, time);
            for (const auto& point : point_delta.start_points)
            {
                points[point.first] = GfVec3f(inv_world.Transform(world.Transform(point.second.point) + delta * point.second.weight));
            }

            point_attr.Set(points, get_non_varying_time(point_attr));
            VtVec3fArray extent;
            if (UsdGeomPointBased::ComputeExtent(points, &extent))
            {
                point_delta.point_based.GetExtentAttr().Set(extent, get_non_varying_time(point_delta.point_based.GetExtentAttr()));
            }
        }

        if (!m_instancer_data.empty())
        {
            for (const auto& data : m_instancer_data)
            {
                auto translate_time = get_non_varying_time(data.point_instancer.GetPositionsAttr());

                VtVec3fArray positions;
                data.point_instancer.GetPositionsAttr().Get(&positions, translate_time);
                const auto world = data.point_instancer.ComputeLocalToWorldTransform(translate_time);
                const auto world_inv = world.GetInverse();

                WorkParallelForN(data.indices.size(),
                                 [positions = positions.data(), &data, stage, &world, &world_inv, delta](size_t begin, size_t end) {
                                     for (auto i = begin; i < end; i++)
                                     {
                                         const auto ind = data.indices[i];
                                         const auto instance_world = data.local_xforms[ind] * world;
                                         const auto new_world_pos = instance_world.ExtractTranslation() + delta;
                                         const auto new_local_pos = world_inv.Transform(new_world_pos);
                                         positions[ind] = GfVec3f(new_local_pos);
                                     }
                                 });
                data.point_instancer.GetPositionsAttr().Set(positions, translate_time);
                const auto extent_time = get_non_varying_time(data.point_instancer.GetExtentAttr());
                VtVec3fArray extent;
                data.point_instancer.ComputeExtentAtTime(&extent, extent_time, extent_time);
                data.point_instancer.GetExtentAttr().Set(extent, extent_time);
            }
        }
        if (!m_points_delta.empty() || !m_instancer_data.empty())
        {
            session->get_stage_bbox_cache(session->get_current_stage_id()).Clear();
        }
        for (const auto& prim_transform : m_prim_transforms)
        {
            const auto new_pivot_world_pos = prim_transform.transform.ExtractTranslation() + delta;
            static const GfMatrix4d identity_matrix(1);

            GfVec3d new_local_translate;
            if (prim_transform.parent_transform != identity_matrix)
            {
                new_local_translate = prim_transform.parent_transform.GetInverse().Transform(new_pivot_world_pos);
                new_local_translate -= prim_transform.local_pivot_pos;
            }
            else
            {
                new_local_translate = new_pivot_world_pos - prim_transform.local_pivot_pos;
            }

            auto translate_time = UsdTimeCode::Default();
            bool dummy;
            for (const auto& op : prim_transform.xform.GetOrderedXformOps(&dummy))
            {
                if (op.GetOpType() == UsdGeomXformOp::Type::TypeTranslate)
                {
                    translate_time = get_non_varying_time(op.GetAttr());
                    break;
                }
            }
            UsdGeomXformCommonAPI xform_api(prim_transform.xform);
            if (xform_api)
            {
                xform_api.SetTranslate(new_local_translate, translate_time);
            }
            else
            {
                GfTransform transform(prim_transform.local);
                transform.SetTranslation(new_local_translate);

                if (GfIsClose(transform.GetPivotOrientation().GetAngle(), 0, 0.001))
                {
                    prim_transform.xform.ClearXformOpOrder();
                    transform.SetPivotPosition(prim_transform.local_pivot_pos);
                    deferred_edits.emplace_back([transform, xform = prim_transform.xform] { decompose_to_common_api(xform, transform); });
                }
                else
                {
                    auto matrix_op = prim_transform.xform.MakeMatrixXform();
                    matrix_op.Set(transform.GetMatrix(), get_non_varying_time(matrix_op));
                }
            }
        }
    }
    if (!deferred_edits.empty())
    {
        SdfChangeBlock change_block;
        for (const auto& edit : deferred_edits)
            edit();
    }
    // HACK:
    // Due to UsdImagingDelegate recreates some rprims (e.g. PointInstancer)
    // we need to update current selection for all viewports.
    // Since only PointInstancer updates require this operation we check if we have any changes
    if (!m_instancer_data.empty())
        for (const auto& viewport : ViewportWidget::get_live_widgets())
            viewport->get_gl_widget()->get_engine()->set_selected(Application::instance().get_selection(),
                                                                  Application::instance().get_rich_selection());
}

bool ViewportMoveToolCommand::can_edit() const
{
    return m_can_edit;
}

bool ViewportMoveToolCommand::get_start_gizmo_matrix(GfMatrix4d& result)
{
    static const GfMatrix4d zero(0.0);
    if (m_start_gizmo_matrix == zero)
        return false;
    result = m_start_gizmo_matrix;
    return true;
}

SelectionList ViewportMoveToolCommand::get_selection() const
{
    return m_selection;
}

CommandArgs ViewportMoveToolCommand::make_args() const
{
    // TODO: add more args
    CommandArgs result;
    result.arg(m_move_delta);
    if (m_orientation == ViewportMoveToolContext::AxisOrientation::OBJECT)
        result.kwarg("object_space", true);

    if (m_selection != Application::instance().get_selection())
        result.kwarg("objects", m_selection);

    return result;
}

CommandResult ViewportMoveToolCommand::execute(const CommandArgs& args)
{
    m_move_delta = *args.get_arg<GfVec3d>(0);
    if (args.has_kwarg("object_space") && *args.get_kwarg<bool>("object_space"))
        m_orientation = ViewportMoveToolContext::AxisOrientation::OBJECT;
    else
        m_orientation = ViewportMoveToolContext::AxisOrientation::WORLD;

    if (auto obj_arg = args.get_kwarg<SelectionList>("objects"))
        m_selection = *obj_arg;
    else
        m_selection = Application::instance().get_selection();

    set_initial_state(m_selection, m_orientation);
    start_block();
    apply_delta(m_move_delta);
    end_block();
    return CommandResult(CommandResult::Status::SUCCESS);
}

bool ViewportMoveToolCommand::TransformData::operator>(const TransformData& other) const
{
    return xform.GetPath() > other.xform.GetPath();
}

bool ViewportMoveToolCommand::TransformData::operator<(const TransformData& other) const
{
    return xform.GetPath() < other.xform.GetPath();
}
OPENDCC_NAMESPACE_CLOSE
