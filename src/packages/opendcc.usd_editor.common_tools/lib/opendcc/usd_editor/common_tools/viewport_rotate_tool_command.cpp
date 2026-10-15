// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_command.h"
#include "opendcc/app/core/selection_list.h"
#include <pxr/base/gf/transform.h>
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include <pxr/base/gf/rotation.h>
#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/usdGeom/pointBased.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/work/loops.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string ViewportRotateToolCommand::cmd_name = "rotate";

CommandSyntax ViewportRotateToolCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<GfRotation>("rotation_delta", "Rotation delta")
        .kwarg<SelectionList>("objects", "Affected objects")
        .kwarg<bool>("object_space", "Apply transformation in object space")
        .kwarg<bool>("gimbal_space", "Apply transformation in gimbal space");
}

std::shared_ptr<Command> ViewportRotateToolCommand::create_cmd()
{
    return std::make_shared<ViewportRotateToolCommand>();
}

using namespace manipulator_utils;

void ViewportRotateToolCommand::undo()
{
    m_inverse->invert();
}

void ViewportRotateToolCommand::start_block()
{
    m_change_block = std::make_unique<commands::UsdEditsBlock>();
}

void ViewportRotateToolCommand::end_block()
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

bool ViewportRotateToolCommand::is_recording() const
{
    return m_change_block != nullptr;
}

bool ViewportRotateToolCommand::can_edit() const
{
    return m_can_edit;
}

void ViewportRotateToolCommand::apply_delta(const GfRotation& delta)
{
    using namespace manipulator_utils;
    using Orientation = ViewportRotateManipulator::Orientation;
    using RotateMode = ViewportRotateManipulator::RotateMode;
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;

    m_rotate_delta = delta;
    const auto time = Application::instance().get_current_time();
    std::vector<std::function<void()>> deferred_edits;
    {
        SdfChangeBlock change_block;
        for (const auto& point_delta : m_points_delta)
        {
            VtVec3fArray points;
            point_delta.point_based.GetPointsAttr().Get(&points, time);

            const auto world = point_delta.point_based.ComputeLocalToWorldTransform(time);
            const auto world_inv = world.GetInverse();
            const auto rot = m_orientation == ViewportRotateToolContext::Orientation::OBJECT
                                 ? GfRotation(world.TransformDir(delta.GetAxis()), delta.GetAngle())
                                 : delta;
            for (const auto& point : point_delta.start_points)
            {
                const auto world_old_pos = world.Transform(point.second.point);
                GfRotation weighted_rotation(rot.GetAxis(), rot.GetAngle() * point.second.weight);
                const auto transform_mat =
                    GfMatrix4f().SetTranslate(-m_pivot) * GfMatrix4f().SetRotate(weighted_rotation) * GfMatrix4f().SetTranslate(m_pivot);
                auto world_new_pos = transform_mat.Transform(world_old_pos);
                points[point.first] = GfVec3f(world_inv.Transform(world_new_pos));
            }

            point_delta.point_based.GetPointsAttr().Set(points, get_non_varying_time(point_delta.point_based.GetPointsAttr()));
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
                auto orient_time = get_non_varying_time(data.point_instancer.GetPositionsAttr());

                VtQuathArray orientations;
                auto orientations_attr = data.point_instancer.GetOrientationsAttr();
                if (!orientations_attr.Get(&orientations, orient_time))
                {
                    size_t instance_count;
                    VtIntArray proto_indices;
                    data.point_instancer.GetProtoIndicesAttr().Get(&proto_indices, orient_time);
                    instance_count = proto_indices.size();
                    orientations_attr = data.point_instancer.CreateOrientationsAttr(VtValue(VtQuathArray(instance_count, GfQuath::GetIdentity())));
                    orientations_attr.Get(&orientations, orient_time);
                }
                data.point_instancer.GetOrientationsAttr().Get(&orientations, orient_time);
                const auto world = data.point_instancer.ComputeLocalToWorldTransform(orient_time);

                WorkParallelForN(data.indices.size(),
                                 [this, orientations = orientations.data(), &data, orient_time, stage, &world, delta](size_t begin, size_t end) {
                                     for (auto i = begin; i < end; i++)
                                     {
                                         const auto ind = data.indices[i];
                                         const auto instance_world = (data.local_xforms[ind] * world).RemoveScaleShear();
                                         GfRotation world_rotation;
                                         if (m_orientation == Orientation::OBJECT && data.indices.size() == 1)
                                             world_rotation = delta;
                                         else
                                             world_rotation = GfRotation(instance_world.GetInverse().TransformDir(delta.GetAxis()), delta.GetAngle());
                                         const auto new_local_transform = GfMatrix4d().SetRotate(world_rotation) * data.local_xforms[ind];
                                         orientations[ind] = GfQuath(new_local_transform.RemoveScaleShear().ExtractRotation().GetQuat());
                                     }
                                 });
                orientations_attr.Set(orientations, orient_time);
                const auto extent_time = get_non_varying_time(data.point_instancer.GetExtentAttr());
                VtVec3fArray extent;
                data.point_instancer.ComputeExtentAtTime(&extent, extent_time, extent_time);
                data.point_instancer.GetExtentAttr().Set(extent, extent_time);
            }
        }
        if (!m_points_delta.empty() || !m_instancer_data.empty())
            session->get_stage_bbox_cache(session->get_current_stage_id()).Clear();
        for (auto& prim_transform : m_prim_transforms)
        {
            static const GfMatrix4d identity_matrix(1);

            GfVec3f euler_angles;
            // 1. Convert world axis to local space
            // 2. Rotate local transform matrix about this local axis by angle
            // 3. Make this matrix Right handed
            // 4. Extract rotation part and decompose it to Euler angles
            GfVec3f new_axis;
            if (m_orientation == Orientation::OBJECT)
            {
                new_axis = GfVec3f(delta.GetAxis());
            }
            else
            {
                new_axis = GfVec3f(prim_transform.inv_transform.TransformDir(delta.GetAxis()));
            }

            auto rotation = GfRotation(new_axis, delta.GetAngle());
            auto t = GfMatrix4d().SetRotate(rotation) * prim_transform.local.RemoveScaleShear();

            GfMatrix4d rot;
            GfVec3d sc;
            GfMatrix4d u;
            GfVec3d tr;
            GfMatrix4d pi;
            sc = prim_transform.scale;
            t = GfMatrix4d().SetScale(GfVec3d(GfSgn(sc[0]), GfSgn(sc[1]), GfSgn(sc[2]))) * t;
            t.Factor(&rot, &sc, &u, &tr, &pi);

            auto rotate_time = UsdTimeCode::Default();
            bool dummy;
            for (const auto& op : prim_transform.xform.GetOrderedXformOps(&dummy))
            {
                if (op.GetOpType() >= UsdGeomXformOp::Type::TypeRotateX && op.GetOpType() <= UsdGeomXformOp::Type::TypeRotateZYX)
                {
                    rotate_time = get_non_varying_time(op.GetAttr());
                    break;
                }
            }
            GfVec3d hint = get_euler_angles(prim_transform.xform, time);
            euler_angles = decompose_to_euler(u, prim_transform.rot_order, hint);

            UsdGeomXformCommonAPI xform_api(prim_transform.xform);
            if (xform_api)
            {
                xform_api.SetRotate(euler_angles, prim_transform.rot_order, rotate_time);
            }
            else
            {
                auto transform = GfTransform(prim_transform.local);
                transform.SetRotation(to_quaternion(euler_angles));
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

bool ViewportRotateToolCommand::get_start_gizmo_data(ViewportRotateManipulator::GizmoData& result) const
{
    static const GfMatrix4d zero(0.0);
    if (m_start_gizmo_data.gizmo_matrix == zero)
        return false;
    result = m_start_gizmo_data;
    return true;
}

bool ViewportRotateToolCommand::affects_components() const
{
    return !m_points_delta.empty() || m_instancer_data.size() > 1 || (m_instancer_data.size() == 1 && m_instancer_data[0].indices.size() > 1);
}

CommandArgs ViewportRotateToolCommand::make_args() const
{
    CommandArgs result;
    result.arg(m_rotate_delta);
    if (m_orientation == ViewportRotateToolContext::Orientation::OBJECT)
        result.kwarg("object_space", true);
    if (m_orientation == ViewportRotateToolContext::Orientation::GIMBAL)
        result.kwarg("gimbal_space", true);
    if (m_selection != Application::instance().get_selection())
        result.kwarg("objects", m_selection);
    return result;
}

CommandResult ViewportRotateToolCommand::execute(const CommandArgs& args)
{
    m_rotate_delta = *args.get_arg<GfRotation>(0);
    if (args.has_kwarg("object_space") && *args.get_kwarg<bool>("object_space"))
        m_orientation = ViewportRotateToolContext::Orientation::OBJECT;
    else if (args.has_kwarg("gimbal_space") && *args.get_kwarg<bool>("gimbal_space"))
        m_orientation = ViewportRotateToolContext::Orientation::GIMBAL;
    else
        m_orientation = ViewportRotateToolContext::Orientation::WORLD;

    if (auto obj_arg = args.get_kwarg<SelectionList>("objects"))
        m_selection = *obj_arg;
    else
        m_selection = Application::instance().get_selection();

    set_initial_state(m_selection, m_orientation);
    start_block();
    apply_delta(m_rotate_delta);
    end_block();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void ViewportRotateToolCommand::redo()
{
    m_inverse->invert();
}

void ViewportRotateToolCommand::set_initial_state(const SelectionList& selection, ViewportRotateToolContext::Orientation orientation)
{
    m_selection = selection;
    m_orientation = orientation;
    m_can_edit = false;
    m_start_gizmo_data.gizmo_matrix.SetZero();
    m_start_gizmo_data.gizmo_angles.Set(0, 0, 0);
    m_start_gizmo_data.parent_gizmo_matrix.SetZero();

    auto selected_paths = selection.get_fully_selected_paths();
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;

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
                    GfMatrix4d local_proto;
                    const auto world_pos = local_xforms[ind] * prim_world;
                    centroid += GfVec3f(world_pos.ExtractTranslation());
                    ++point_count;
                }

                if (sel_data.get_instance_indices().size() == 1 && m_instancer_data.empty())
                {
                    const auto ind = *sel_data.get_instance_indices().begin();
                    world_transform = local_xforms[ind] * prim_world;
                    world_transform = world_transform.RemoveScaleShear();
                }
                if (m_can_edit || m_instancer_data.empty())
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
        if (orientation == ViewportRotateToolContext::Orientation::WORLD || m_points_delta.size() > 1 || m_instancer_data.size() > 1 ||
            (m_instancer_data.size() == 1 && m_instancer_data[0].indices.size() > 1) || (m_points_delta.size() + m_instancer_data.size() > 1))
        {
            m_start_gizmo_data.gizmo_matrix.SetTranslate(centroid);
            m_start_gizmo_data.parent_gizmo_matrix.SetIdentity();
            m_start_gizmo_data.gizmo_angles = GfVec3f(0);
            m_start_gizmo_data.rotation_order = UsdGeomXformCommonAPI::RotationOrderXYZ;
        }
        else if (m_instancer_data.size() == 1 && m_instancer_data[0].indices.size() == 1 && m_points_delta.empty())
        {
            m_start_gizmo_data.parent_gizmo_matrix = cache.GetLocalToWorldTransform(m_instancer_data[0].point_instancer.GetPrim());
            m_start_gizmo_data.gizmo_matrix = world_transform;
            m_start_gizmo_data.gizmo_matrix.SetTranslateOnly(centroid);
            const auto ind = m_instancer_data[0].indices[0];
            auto rot =
                m_instancer_data[0].local_xforms[ind].RemoveScaleShear().DecomposeRotation(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis());
            m_start_gizmo_data.gizmo_angles = GfVec3f(rot[2], rot[1], rot[0]);
            m_start_gizmo_data.rotation_order = UsdGeomXformCommonAPI::RotationOrderXYZ;
        }
        else
        {
            auto prim = m_points_delta.front().point_based.GetPrim();
            bool reset;
            const auto local_transform = cache.GetLocalTransformation(prim, &reset);
            m_start_gizmo_data.parent_gizmo_matrix = cache.GetParentToWorldTransform(prim);
            m_start_gizmo_data.gizmo_matrix = local_transform.RemoveScaleShear() * m_start_gizmo_data.parent_gizmo_matrix;
            m_start_gizmo_data.gizmo_matrix = m_start_gizmo_data.gizmo_matrix.RemoveScaleShear();
            m_start_gizmo_data.gizmo_matrix.SetTranslateOnly(centroid);

            GfVec3d translation;
            GfVec3f rotation;
            GfVec3f scale;
            GfVec3f pivot;
            UsdGeomXformCommonAPI::RotationOrder rot_order;
            auto api = UsdGeomXformCommonAPI(prim);
            if (api.GetXformVectorsByAccumulation(&translation, &rotation, &scale, &pivot, &rot_order, Application::instance().get_current_time()))
            {
                m_start_gizmo_data.gizmo_angles = rotation;
                m_start_gizmo_data.rotation_order = rot_order;
            }
            else
            {
                auto transform = GfTransform(local_transform);
                auto euler_angles = transform.GetRotation().Decompose(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis());
                m_start_gizmo_data.gizmo_angles = GfVec3f(euler_angles[2], euler_angles[1], euler_angles[0]);
                m_start_gizmo_data.rotation_order = UsdGeomXformCommonAPI::RotationOrderXYZ;
            }
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
        GfVec3f pivot;
        GfVec3d pivot_world_pos;
        auto xform_api = UsdGeomXformCommonAPI(prim);

        if (xform_api.GetXformVectorsByAccumulation(&translation, &rotation, &scale, &pivot, &prim_transform.rot_order,
                                                    Application::instance().get_current_time()))
        {
            pivot_world_pos = world_transform.Transform(pivot);
            prim_transform.scale = scale;
            prim_transform.local_angles = rotation;
            prim_transform.pivot = pivot;
        }
        else
        {
            auto transform = GfTransform(local_transform);
            auto euler_angles = transform.GetRotation().Decompose(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis());
            prim_transform.rot_order = UsdGeomXformCommonAPI::RotationOrder::RotationOrderXYZ;
            prim_transform.local_angles = GfVec3f(euler_angles[2], euler_angles[1], euler_angles[0]);
            prim_transform.scale = static_cast<GfVec3f>(transform.GetScale());
            pivot_world_pos = world_transform.ExtractTranslation();
            prim_transform.pivot.Set(0, 0, 0);
            prim_transform.transform.SetIdentity();
        }
        prim_transform.transform =
            (local_transform.RemoveScaleShear() * prim_transform.parent_transform).RemoveScaleShear(); // world_transform.RemoveScaleShear();
        prim_transform.inv_transform = prim_transform.transform.GetInverse();

        prim_transform.transform.SetTranslateOnly(pivot_world_pos);
        m_prim_transforms.push_back(std::move(prim_transform));
    }

    if (m_prim_transforms.empty())
        return;

    m_start_gizmo_data.parent_gizmo_matrix = m_prim_transforms.front().parent_transform;
    m_start_gizmo_data.gizmo_matrix = m_prim_transforms.front().local.RemoveScaleShear() * m_start_gizmo_data.parent_gizmo_matrix;
    m_start_gizmo_data.gizmo_matrix = m_start_gizmo_data.gizmo_matrix.RemoveScaleShear();
    m_start_gizmo_data.gizmo_matrix.SetTranslateOnly(m_prim_transforms.front().transform.ExtractTranslation());
    m_start_gizmo_data.rotation_order = m_prim_transforms.front().rot_order;

    m_start_gizmo_data.gizmo_angles = m_prim_transforms.front().local_angles;
    std::sort(m_prim_transforms.begin(), m_prim_transforms.end(), std::greater<TransformData>());
    if (orientation == ViewportRotateToolContext::Orientation::WORLD)
        m_start_gizmo_data.gizmo_matrix.SetTranslate(m_start_gizmo_data.gizmo_matrix.ExtractTranslation());
    m_pivot = GfVec3f(m_start_gizmo_data.gizmo_matrix.ExtractTranslation());
}

bool ViewportRotateToolCommand::TransformData::operator>(const TransformData& other) const
{
    return xform.GetPath() > other.xform.GetPath();
}

OPENDCC_NAMESPACE_CLOSE
