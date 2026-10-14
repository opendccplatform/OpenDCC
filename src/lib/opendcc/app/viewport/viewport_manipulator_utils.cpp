// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/base/logging/logger.h"
#include <pxr/base/gf/frustum.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/base/gf/camera.h>
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/settings.h"
#include <pxr/base/gf/line.h>
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/block.h"
#include "viewport_widget.h"
#include "viewport_hydra_engine.h"
#include "viewport_gl_widget.h"
#include "opendcc/app/core/undo/router.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.tools");

PXR_NAMESPACE_USING_DIRECTIVE
namespace manipulator_utils
{
    std::array<int, 3> get_basis_indices_from_rot_order(UsdGeomXformCommonAPI::RotationOrder rotation_order)
    {
        switch (rotation_order)
        {
        case UsdGeomXformCommonAPI::RotationOrderXYZ:
            return { 0, 1, 2 };
        case UsdGeomXformCommonAPI::RotationOrderXZY:
            return { 0, 2, 1 };
        case UsdGeomXformCommonAPI::RotationOrderYXZ:
            return { 1, 0, 2 };
        case UsdGeomXformCommonAPI::RotationOrderYZX:
            return { 1, 2, 0 };
        case UsdGeomXformCommonAPI::RotationOrderZXY:
            return { 2, 0, 1 };
        case UsdGeomXformCommonAPI::RotationOrderZYX:
            return { 2, 1, 0 };
        default:
            OPENDCC_ERROR("Failed to get basis vector indices. Rotate manipulation might be incorrect.");
            return { 0, 1, 2 };
        }
    }

    double compute_screen_factor(const ViewportViewPtr& viewport_view, const PXR_NS::GfVec3d& center)
    {
        GfCamera camera = viewport_view->get_camera();
        const auto viewport_dim = viewport_view->get_viewport_dimensions();
        GfFrustum frustum = camera.GetFrustum();

        GfMatrix4d projection = CameraUtilConformedWindow(frustum.ComputeProjectionMatrix(), CameraUtilConformWindowPolicy::CameraUtilFit,
                                                          viewport_dim.height != 0 ? (double)viewport_dim.width / viewport_dim.height : 1.0);

        GfMatrix4d vp_matrix = frustum.ComputeViewMatrix() * projection;
        GfVec4d trf(center[0], center[1], center[2], 1.0);
        trf = trf * vp_matrix;
        const double display_scale = Application::instance().get_settings()->get("viewport.manipulators.global_scale", 1.0);
        const double screen_factor = display_scale * 0.15 * trf[3];

        if (frustum.GetProjectionType() == GfFrustum::ProjectionType::Perspective)
        {
            return screen_factor * frustum.GetFOV() / 35; // reference value for gizmo
        }
        else
        {
            const double right = frustum.GetWindow().GetMax()[0];
            const double left = frustum.GetWindow().GetMin()[0];
            return screen_factor * (right - left);
        }
    }

    UsdTimeCode get_non_varying_time(const UsdAttribute& attr)
    {
        if (!attr)
            return UsdTimeCode::Default();

        const auto num_time_samples = attr.GetNumTimeSamples();
        if (num_time_samples == 0)
        {
            return UsdTimeCode::Default();
        }
        else if (num_time_samples == 1)
        {
            std::vector<double> timesamples(1);
            attr.GetTimeSamples(&timesamples);
            return timesamples[0];
        }

        return UsdTimeCode::Default();
    }

    GfRay compute_pick_ray(const ViewportViewPtr& viewport_view, int x, int y)
    {
        const auto frustum = compute_view_frustum(viewport_view);
        const auto pick_point = compute_pick_point(viewport_view->get_viewport_dimensions(), x, y);
        return frustum.ComputePickRay(pick_point);
    }

    GfRay compute_pick_ray(const GfFrustum& frustum, const ViewportDimensions& viewport_dim, int x, int y)
    {
        const auto pick_point = compute_pick_point(viewport_dim, x, y);
        return frustum.ComputePickRay(pick_point);
    }

    bool compute_axis_intersection(const ViewportViewPtr& viewport_view, const GfVec3d& gizmo_world_pos, const GfVec3d& direction,
                                   const GfMatrix4d& view_proj, int x, int y, GfVec3d& result)
    {
        const auto pick_ray = compute_pick_ray(viewport_view, x, y);
        if (GfAbs(GfDot(direction, pick_ray.GetDirection())) > 0.99)
            return false;

        GfVec3d line_point;
        GfFindClosestPoints(pick_ray, GfLine(gizmo_world_pos, direction), nullptr, &line_point, nullptr, nullptr);

        const auto proj_depth = view_proj.Transform(line_point)[2];
        if (proj_depth >= -1 && proj_depth <= 1)
        {
            result = line_point;
            return true;
        }
        return false;
    }

    bool compute_plane_intersection(const ViewportViewPtr& viewport_view, const GfVec3d& gizmo_world_pos, const GfVec3d& plane_normal,
                                    const GfMatrix4d& view_proj, int x, int y, GfVec3d& result)
    {
        const auto pick_ray = compute_pick_ray(viewport_view, x, y);
        if (GfAbs(GfDot(plane_normal, pick_ray.GetDirection())) < 0.01)
            return false;

        GfPlane plane(plane_normal, gizmo_world_pos);
        double dist;
        pick_ray.Intersect(plane, &dist);
        const auto point_on_ray = pick_ray.GetPoint(dist);

        const auto proj_depth = view_proj.Transform(point_on_ray)[2];
        if (proj_depth >= -1 && proj_depth <= 1)
        {
            result = point_on_ray;
            return true;
        }
        return false;
    }

    GfVec3d compute_sphere_intersection(const ViewportViewPtr& viewport_view, double screen_factor, const GfVec3d& gizmo_world_pos, int x, int y)
    {
        const auto frustum = compute_view_frustum(viewport_view);
        const auto pick_ray = compute_pick_ray(viewport_view, x, y);

        double dist;
        if (pick_ray.Intersect(gizmo_world_pos, screen_factor, &dist))
            return pick_ray.GetPoint(dist);

        GfPlane camera_plane(frustum.ComputeViewDirection(), gizmo_world_pos);
        pick_ray.Intersect(camera_plane, &dist);
        return pick_ray.GetPoint(dist);
    }

    GfFrustum compute_view_frustum(const ViewportViewPtr& viewport_view)
    {
        const auto camera = viewport_view->get_camera();
        GfFrustum frustum = camera.GetFrustum();
        const auto viewport_dim = viewport_view->get_viewport_dimensions();
        CameraUtilConformWindow(&frustum, CameraUtilConformWindowPolicy::CameraUtilFit,
                                viewport_dim.height != 0.0 ? (double)viewport_dim.width / viewport_dim.height : 1.0);
        return frustum;
    }

    GfVec2d compute_pick_point(const ViewportDimensions& viewport_dim, int x, int y)
    {
        return GfVec2d(2 * double(x) / viewport_dim.width - 1, 1 - 2 * double(y) / viewport_dim.height);
    }

    bool compute_screen_space_pos(const ViewportViewPtr& viewport_view, const GfVec3d& gizmo_world_pos, const GfVec3d& direction,
                                  const GfMatrix4d& view_proj, int x, int y, GfVec3d& result)
    {
        const auto dim = viewport_view->get_viewport_dimensions();
        result = GfVec3d(2 * double(x) / dim.width - 1, 1 - 2 * double(y) / dim.height, 0);
        return true;
    }

    GfQuatd to_quaternion(const GfVec3d& euler_angles)
    {
        auto qd = (GfRotation(GfVec3d(1, 0, 0), euler_angles[0]) * GfRotation(GfVec3d(0, 1, 0), euler_angles[1]) *
                   GfRotation(GfVec3d(0, 0, 1), euler_angles[2]))
                      .GetQuat();
        auto i = qd.GetImaginary();
        auto q = GfQuatd(qd.GetReal(), i[0], i[1], i[2]);
        q.Normalize();
        return q;
    }

    void decompose_to_common_api(const UsdGeomXformable& xform, const GfTransform& transform)
    {
        static const GfVec3d zero_vec(0);
        static const GfVec3f one_vec(1);

        auto get_time = [&xform](const TfToken& attr_name) {
            return get_non_varying_time(xform.GetPrim().GetAttribute(attr_name));
        };

        auto xform_api = UsdGeomXformCommonAPI(xform);
        if (!GfIsClose(transform.GetTranslation(), zero_vec, 0.0001))
            xform_api.SetTranslate(transform.GetTranslation(), get_time(TfToken("xform:translate")));

        const auto euler_angles = transform.GetRotation().Decompose(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis());
        if (!GfIsClose(euler_angles, zero_vec, 0.0001))
            xform_api.SetRotate(GfVec3f(euler_angles[2], euler_angles[1], euler_angles[0]), UsdGeomXformCommonAPI::RotationOrder::RotationOrderXYZ,
                                get_time(TfToken("xform:rotateXYZ")));
        if (!GfIsClose(transform.GetScale(), one_vec, 0.0001))
            xform_api.SetScale(GfVec3f(transform.GetScale()), get_time(TfToken("xform:scale")));
        if (!GfIsClose(transform.GetPivotPosition(), zero_vec, 0.0001))
            xform_api.SetPivot(GfVec3f(transform.GetPivotPosition()), get_time(TfToken("xform:translate:pivot")));
    }

    void reset_pivot(const SelectionList& selection_list)
    {
        auto stage = Application::instance().get_session()->get_current_stage();
        commands::UsdEditsUndoBlock undo_block;
        SdfChangeBlock change_block;
        auto cache = UsdGeomXformCache(Application::instance().get_current_time());
        std::vector<std::function<void()>> deferred_edits;
        for (const auto& path : selection_list.get_fully_selected_paths())
        {
            auto prim = stage->GetPrimAtPath(path);
            if (!prim || cache.TransformMightBeTimeVarying(prim))
                continue;
            auto xform = UsdGeomXformable(stage->GetPrimAtPath(path));
            if (!xform)
                continue;
            bool dummy;
            auto local_mat = cache.GetLocalTransformation(prim, &dummy);
            GfTransform transform(local_mat);
            xform.ClearXformOpOrder();
            deferred_edits.emplace_back([xform, transform] { decompose_to_common_api(xform, transform); });
            prim.RemoveProperty(TfToken("xformOp:translate:pivot"));
        }

        if (!deferred_edits.empty())
        {
            SdfChangeBlock block;
            for (const auto& edit : deferred_edits)
                edit();
        }
    }

    GfVec3d get_euler_angles(const UsdGeomXformable& xform, UsdTimeCode time)
    {
        bool reset;
        GfVec3d result(0);
        for (const auto& op : xform.GetOrderedXformOps(&reset))
        {
            switch (op.GetOpType())
            {
            case UsdGeomXformOp::Type::TypeRotateX:
                op.GetAs(&result[0], time);
                break;
            case UsdGeomXformOp::Type::TypeRotateY:
                op.GetAs(&result[1], time);
                break;
            case UsdGeomXformOp::Type::TypeRotateZ:
                op.GetAs(&result[2], time);
                break;
            case UsdGeomXformOp::Type::TypeRotateXYZ:
            case UsdGeomXformOp::Type::TypeRotateXZY:
            case UsdGeomXformOp::Type::TypeRotateYXZ:
            case UsdGeomXformOp::Type::TypeRotateYZX:
            case UsdGeomXformOp::Type::TypeRotateZXY:
            case UsdGeomXformOp::Type::TypeRotateZYX:
                op.GetAs(&result, time);
                break;
            default:
                continue;
            }
        }
        return result;
    }

    GfVec3f decompose_to_euler(const GfMatrix4d& matrix, UsdGeomXformCommonAPI::RotationOrder rot_order, const GfVec3d& hint)
    {
        static const std::array<GfVec3f, 3> basis_vectors = { GfVec3f::XAxis(), GfVec3f::YAxis(), GfVec3f::ZAxis() };
        const auto basis_indices = get_basis_indices_from_rot_order(rot_order);
        std::array<double, 3> angles = { GfDegreesToRadians(hint[basis_indices[0]]), GfDegreesToRadians(hint[basis_indices[1]]),
                                         GfDegreesToRadians(hint[basis_indices[2]]) };
        GfRotation::DecomposeRotation(matrix, basis_vectors[basis_indices[0]], basis_vectors[basis_indices[1]], basis_vectors[basis_indices[2]], 1,
                                      &angles[0], &angles[1], &angles[2], nullptr, true);

        GfVec3f result;
        for (int i = 0; i < result.dimension; i++)
        {
            const auto deg = GfRadiansToDegrees(angles[i]);
            result[basis_indices[i]] = GfIsClose(deg, 0, 0.00001) ? 0.0 : deg;
        }
        return result;
    }

    void visit_all_selected_points(const SelectionData& selection_data, const PXR_NS::UsdPrim& prim, const std::function<void(int)>& visit)
    {
        auto session = Application::instance().get_session();
        const auto time = Application::instance().get_current_time();
        const auto stage_id = session->get_current_stage_id();
        auto topology_cache = session->get_stage_topology_cache(stage_id);

        for (const auto& point_index : selection_data.get_point_indices())
        {
            visit(point_index);
        }

        auto topology = topology_cache.get_topology(prim, time);
        if (!topology)
        {
            return;
        }

        for (const auto& edge_ind : selection_data.get_edge_indices())
        {
            auto vertices_tuple = topology->edge_map.get_vertices_by_edge_id(edge_ind);
            if (!std::get<bool>(vertices_tuple))
            {
                continue;
            }
            const auto vertices = std::get<PXR_NS::GfVec2i>(vertices_tuple);
            visit(vertices[0]);
            visit(vertices[1]);
        }

        const auto& face_counts = topology->mesh_topology.GetFaceVertexCounts();
        const auto& face_indices = topology->mesh_topology.GetFaceVertexIndices();
        for (const auto& face_ind : selection_data.get_element_indices())
        {
            const auto face_start = topology->face_starts[face_ind];
            for (int i = 0; i < face_counts[face_ind]; i++)
            {
                visit(face_indices[face_start + i]);
            }
        }
    }

    std::tuple<PXR_NS::GfVec3f, size_t> compute_centroid_data(const SelectionData& selection_data, const PXR_NS::UsdPrim& prim,
                                                              const PXR_NS::VtArray<PXR_NS::GfVec3f>& points,
                                                              const PXR_NS::GfMatrix4d& world_transform)
    {
        PXR_NS::GfVec3f centroid = { 0, 0, 0 };
        size_t point_count = 0;
        std::unordered_set<int> visited;
        visit_all_selected_points(selection_data, prim, [&](int point_index) {
            if (visited.find(point_index) == visited.end())
            {
                visited.insert(point_index);
                const auto& point = points[point_index];
                centroid += GfVec3f(world_transform.Transform(point));
                ++point_count;
            }
        });
        return std::make_tuple(centroid, point_count);
    }

    bool ViewportSelection::operator()()
    {
        for (const auto& viewport : ViewportWidget::get_live_widgets())
            viewport->get_gl_widget()->get_engine()->set_selected(Application::instance().get_selection(),
                                                                  Application::instance().get_rich_selection());
        commands::UndoRouter::add_inverse(std::make_shared<ViewportSelection>());
        return true;
    }

    bool ViewportSelection::merge_with(const Edit* other)
    {
        return false;
    }

    size_t ViewportSelection::get_edit_type_id() const
    {
        return commands::get_edit_type_id<ViewportSelection>();
    }

};
OPENDCC_NAMESPACE_CLOSE
