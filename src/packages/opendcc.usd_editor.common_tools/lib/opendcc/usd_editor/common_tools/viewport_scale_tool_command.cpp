// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_scale_tool_command.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/matrix4f.h>
#include "pxr/base/work/loops.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string ViewportScaleToolCommand::cmd_name = "scale";
CommandSyntax ViewportScaleToolCommand::cmd_syntax()
{
    return CommandSyntax().arg<GfVec3d>("scale_delta", "Scale delta").kwarg<SelectionList>("objects", "Affected objects");
}
std::shared_ptr<Command> ViewportScaleToolCommand::create_cmd()
{
    return std::make_shared<ViewportScaleToolCommand>();
}

using namespace manipulator_utils;

void ViewportScaleToolCommand::undo()
{
    m_inverse->invert();
}

void ViewportScaleToolCommand::redo()
{
    m_inverse->invert();
}

void ViewportScaleToolCommand::set_initial_state(const SelectionList& selection)
{
    m_selection = selection;
    m_can_edit = false;
    m_start_gizmo_data.gizmo_matrix.SetZero();
    if (selection.empty())
        return;

    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;

    auto selected_paths = selection.get_fully_selected_paths();

    const auto time = Application::instance().get_current_time();
    UsdGeomXformCache cache(Application::instance().get_current_time());
    size_t point_count = 0;
    GfMatrix4d world_transform;
    GfVec3f centroid(0);
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
                if (m_can_edit || m_instancer_data.empty())
                {
                    VtVec3fArray scales;
                    point_instancer.GetScalesAttr().Get(&scales, instancer_time);

                    const auto& instance_indices_set = sel_data.get_instance_indices();
                    InstancerData data;
                    data.point_instancer = point_instancer;
                    data.indices = std::vector<SelectionList::IndexType>(instance_indices_set.begin(), instance_indices_set.end());
                    data.local_scales = scales;
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
        VtVec3fArray points;
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
            auto add_point_delta = [&delta, &point_count, &centroid, &points, &world_transform](int point_ind) {
                const auto& point = points[point_ind];
                auto iter = delta.start_points.find(point_ind);
                if (iter == delta.start_points.end())
                {
                    delta.start_points[point_ind].point = point;
                    delta.start_points[point_ind].weight = 1.0f;
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
        m_pivot = centroid;
        if (m_points_delta.size() > 1 || m_instancer_data.size() > 1 || (m_instancer_data.size() == 1 && m_instancer_data[0].indices.size() > 1))
        {
            m_start_gizmo_data.gizmo_matrix.SetTranslate(centroid);
            m_start_gizmo_data.scale.Set(1, 1, 1);
        }
        else if (m_instancer_data.size() == 1 && m_points_delta.empty())
        {
            m_start_gizmo_data.gizmo_matrix = world_transform.RemoveScaleShear();
            m_start_gizmo_data.scale = m_instancer_data[0].local_scales[m_instancer_data[0].indices[0]];
        }
        else
        {
            auto prim = m_points_delta.front().point_based.GetPrim();
            bool reset;
            const auto local_transform = cache.GetLocalTransformation(prim, &reset);
            m_start_gizmo_data.gizmo_matrix = cache.GetLocalToWorldTransform(prim).RemoveScaleShear();
            m_start_gizmo_data.gizmo_matrix.SetTranslateOnly(m_pivot);
            m_start_gizmo_data.scale.Set(1, 1, 1);
        }

        if (m_instancer_data.size() == 1)
        {
            if (cache.TransformMightBeTimeVarying(m_instancer_data[0].point_instancer.GetPrim()) ||
                m_instancer_data[0].point_instancer.GetPositionsAttr().ValueMightBeTimeVarying())
            {
                m_instancer_data.clear();
            }
        }
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
        prim_transform.local = local_transform;
        GfMatrix4d world_transform;
        if (reset_xform_stack)
        {
            world_transform = local_transform;
            prim_transform.parent_transform.SetIdentity();
        }
        else
        {
            prim_transform.parent_transform = cache.GetParentToWorldTransform(prim);
            world_transform = local_transform * prim_transform.parent_transform;
        }

        GfVec3d translation;
        GfVec3f rotation;
        GfVec3f scale;
        GfVec3f pivot;
        UsdGeomXformCommonAPI::RotationOrder rot_order;
        GfVec3d pivot_world_pos;
        auto xform_api = UsdGeomXformCommonAPI(prim);
        if (xform_api.GetXformVectorsByAccumulation(&translation, &rotation, &scale, &pivot, &rot_order, Application::instance().get_current_time()))
        {
            pivot_world_pos = world_transform.Transform(pivot);
            prim_transform.local_scale = scale;
            prim_transform.pivot = pivot;
        }
        else
        {
            const auto transform = GfTransform(local_transform);
            prim_transform.local_scale = GfVec3f(transform.GetScale());
            prim_transform.pivot.Set(0, 0, 0);
            pivot_world_pos = world_transform.ExtractTranslation();
        }
        prim_transform.transform = world_transform.RemoveScaleShear();

        prim_transform.transform.SetTranslateOnly(pivot_world_pos);
        m_prim_transforms.push_back(std::move(prim_transform));
    }

    if (m_prim_transforms.empty())
        return;

    m_start_gizmo_data.gizmo_matrix = m_prim_transforms.front().transform;
    m_start_gizmo_data.scale = m_prim_transforms.front().local_scale;
    std::sort(m_prim_transforms.begin(), m_prim_transforms.end(), std::greater<TransformData>());
    m_pivot = GfVec3f(m_start_gizmo_data.gizmo_matrix.ExtractTranslation());
}

void ViewportScaleToolCommand::start_block()
{
    m_change_block = std::make_unique<commands::UsdEditsBlock>();
}

void ViewportScaleToolCommand::end_block()
{
    // HACK:
    // Due to UsdImagingDelegate recreates some rprims (e.g. PointInstancer)
    // we need to update current selection for all viewports.
    // Since only PointInstancer updates require this operation we check if we have any changes
    if (!m_instancer_data.empty())
        commands::UndoRouter::add_inverse(std::make_shared<ViewportSelection>());

    m_inverse = m_change_block->take_edits();
    m_change_block = nullptr;
}

bool ViewportScaleToolCommand::is_recording() const
{
    return m_change_block != nullptr;
}

void ViewportScaleToolCommand::apply_delta(const PXR_NS::GfVec3d& delta)
{
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;
    m_scale_delta = delta;
    const auto time = Application::instance().get_current_time();
    std::vector<std::function<void()>> deferred_edits;
    {
        SdfChangeBlock change_block;
        for (const auto& point_delta : m_points_delta)
        {
            VtVec3fArray points;
            point_delta.point_based.GetPointsAttr().Get(&points, time);

            const auto world_inv = point_delta.point_based.ComputeLocalToWorldTransform(time).GetInverse();
            const auto local_pivot = GfVec3f(world_inv.Transform(m_pivot));
            for (const auto& point : point_delta.start_points)
            {
                const auto transform_mat = GfMatrix4f().SetTranslate(-local_pivot) *
                                           GfMatrix4f().SetScale(GfVec3f(1) + GfVec3f(delta - GfVec3d(1)) * point.second.weight) *
                                           GfMatrix4f().SetTranslate(local_pivot);
                points[point.first] = transform_mat.Transform(point.second.point);
            }
            point_delta.point_based.GetPointsAttr().Set(points, get_non_varying_time(point_delta.point_based.GetPointsAttr()));
            VtVec3fArray extent;
            if (UsdGeomPointBased::ComputeExtent(points, &extent))
            {
                point_delta.point_based.GetExtentAttr().Set(extent, get_non_varying_time(point_delta.point_based.GetExtentAttr()));
            }
        }
        for (const auto& data : m_instancer_data)
        {
            auto scale_time = get_non_varying_time(data.point_instancer.GetPositionsAttr());

            VtVec3fArray scales;
            auto scales_attr = data.point_instancer.GetScalesAttr();
            if (!scales_attr.Get(&scales, scale_time))
            {
                size_t instance_count;
                VtIntArray proto_indices;
                data.point_instancer.GetProtoIndicesAttr().Get(&proto_indices, scale_time);
                instance_count = proto_indices.size();
                scales_attr = data.point_instancer.CreateScalesAttr(VtValue(VtVec3fArray(instance_count, GfVec3f(1))));
                scales_attr.Get(&scales, scale_time);
            }
            const auto world = data.point_instancer.ComputeLocalToWorldTransform(scale_time);
            const auto world_inv = world.GetInverse();

            WorkParallelForN(data.indices.size(), [scales = scales.data(), &data, stage, &world, &world_inv, delta](size_t begin, size_t end) {
                for (auto i = begin; i < end; i++)
                {
                    const auto ind = data.indices[i];
                    const auto scale = data.local_scales.empty() ? GfVec3f(1) : data.local_scales[ind];
                    scales[ind] = GfCompMult(scale, GfVec3f(delta));
                }
            });

            scales_attr.Set(scales, scale_time);
            const auto extent_time = get_non_varying_time(data.point_instancer.GetExtentAttr());
            VtVec3fArray extent;
            data.point_instancer.ComputeExtentAtTime(&extent, extent_time, extent_time);
            data.point_instancer.GetExtentAttr().Set(extent, extent_time);
        }
        if (!m_points_delta.empty() || !m_instancer_data.empty())
            session->get_stage_bbox_cache(session->get_current_stage_id()).Clear();
        for (const auto& prim_transform : m_prim_transforms)
        {
            GfVec3f new_scale = GfCompMult(prim_transform.local_scale, GfVec3f(delta));

            auto scale_time = UsdTimeCode::Default();
            bool dummy;
            for (const auto& op : prim_transform.xform.GetOrderedXformOps(&dummy))
            {
                if (op.GetOpType() == UsdGeomXformOp::Type::TypeScale)
                {
                    scale_time = get_non_varying_time(op.GetAttr());
                    break;
                }
            }
            UsdGeomXformCommonAPI xform_api(prim_transform.xform);
            if (xform_api)
            {
                xform_api.SetScale(new_scale, scale_time);
            }
            else
            {
                static const GfMatrix4d identity_matrix(1);

                GfTransform transform(prim_transform.local);
                transform.SetScale(new_scale);
                if (prim_transform.parent_transform == identity_matrix)
                    transform.SetTranslation(prim_transform.transform.ExtractTranslation() - prim_transform.pivot);
                else
                    transform.SetTranslation(prim_transform.parent_transform.GetInverse().Transform(prim_transform.transform.ExtractTranslation()) -
                                             prim_transform.pivot);

                if (GfIsClose(transform.GetPivotOrientation().GetAngle(), 0, 0.001))
                {
                    prim_transform.xform.ClearXformOpOrder();
                    transform.SetPivotPosition(prim_transform.pivot);
                    deferred_edits.emplace_back(
                        [transform, xform = prim_transform.xform, pivot = prim_transform.pivot] { decompose_to_common_api(xform, transform); });
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

bool ViewportScaleToolCommand::can_edit() const
{
    return m_can_edit;
}

bool ViewportScaleToolCommand::get_start_gizmo_data(ViewportScaleManipulator::GizmoData& result)
{
    static const GfMatrix4d zero(0.0);
    if (m_start_gizmo_data.gizmo_matrix == zero)
        return false;
    result = m_start_gizmo_data;
    return true;
}

CommandResult ViewportScaleToolCommand::execute(const CommandArgs& args)
{
    m_scale_delta = *args.get_arg<GfVec3d>(0);

    if (auto obj_arg = args.get_kwarg<SelectionList>("objects"))
        m_selection = *obj_arg;
    else
        m_selection = Application::instance().get_selection();

    set_initial_state(m_selection);
    start_block();
    apply_delta(m_scale_delta);
    end_block();
    return CommandResult(CommandResult::Status::SUCCESS);
}

CommandArgs ViewportScaleToolCommand::make_args() const
{
    CommandArgs result;
    result.arg(m_scale_delta);
    if (m_selection != Application::instance().get_selection())
        result.kwarg("objects", m_selection);
    return result;
}

bool ViewportScaleToolCommand::TransformData::operator>(const TransformData& other) const
{
    return xform.GetPath() > other.xform.GetPath();
}
OPENDCC_NAMESPACE_CLOSE
