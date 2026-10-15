// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/uv_editor/uv_rotate_command.h"
#include "opendcc/usd_editor/uv_editor/uv_editor_gl_widget.h"
#include "opendcc/usd_editor/uv_editor/utils.h"

#include "opendcc/base/commands_api/core/command_registry.h"

#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"

#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"

#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/type.h>

#include <iostream>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandResult;
using base::commands::CommandArgs;

//////////////////////////////////////////////////////////////////////////
// UvRotateCommand
//////////////////////////////////////////////////////////////////////////

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UvRotateCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command("uv_rotate", CommandSyntax(), [] { return std::make_shared<UvRotateCommand>(); });
}

//////////////////////////////////////////////////////////////////////////
// UvRotateCommand::Command
//////////////////////////////////////////////////////////////////////////

CommandResult UvRotateCommand::execute(const CommandArgs& args) /* override */
{
    return {};
}

//////////////////////////////////////////////////////////////////////////
// UvRotateCommand::UndoCommand
//////////////////////////////////////////////////////////////////////////

void UvRotateCommand::undo() /* override */
{
    do_cmd();
}

void UvRotateCommand::redo() /* override */
{
    do_cmd();
}

//////////////////////////////////////////////////////////////////////////
// UvRotateCommand::ToolCommand
//////////////////////////////////////////////////////////////////////////

CommandArgs UvRotateCommand::make_args() const /* override */
{
    return {};
}

//////////////////////////////////////////////////////////////////////////
// UvRotateCommand
//////////////////////////////////////////////////////////////////////////

void UvRotateCommand::init_from_mesh_selection(UVEditorGLWidget* widget, const SelectionList& mesh_list)
{
    m_widget = widget;

    auto& app = Application::instance();
    const auto session = app.get_session();
    const auto stage = session->get_current_stage();
    if (!stage)
    {
        return;
    }

    m_centroid.Set(0, 0);
    uint64_t count = 0;

    const auto time = app.get_current_time();
    const auto uv_primvar = m_widget->get_uv_primvar();
    const auto token_primvar = TfToken(uv_primvar.toStdString());
    auto rich_selection = Application::instance().get_rich_selection();
    rich_selection.set_soft_selection(mesh_to_uv(mesh_list, widget->get_prims_info()));

    for (const auto& entry : mesh_list)
    {
        const auto& path = entry.first;
        const auto& data = entry.second;

        auto prim = stage->GetPrimAtPath(entry.first);
        if (!prim)
        {
            continue;
        }

        auto primvars = UsdGeomPrimvarsAPI(prim);
        if (!primvars)
        {
            continue;
        }

        const auto indices = to_uv_points_indices(widget, path, to_points_indices(prim, data));
        if (indices.empty())
        {
            continue;
        }

        auto primvar = primvars.GetPrimvar(token_primvar);
        if (!primvar)
        {
            continue;
        }

        VtArray<GfVec2f> points;
        if (!primvar.Get(&points, time))
        {
            continue;
        }

        PointsData points_data;
        for (const auto& interval : indices)
        {
            for (SelectionList::IndexType index = interval.start; index <= interval.end; ++index)
            {
                points_data.start_points[index].point = points[index];

                m_centroid += points[index];
            }
        }

        count += points_data.start_points.size();

        if (Application::instance().is_soft_selection_enabled())
        {
            for (const auto& weight : rich_selection.get_weights(entry.first))
            {
                const auto& point = points[weight.first];
                points_data.start_points[weight.first].point = point;
                points_data.start_points[weight.first].weight = weight.second;
            }
        }

        points_data.primvars = primvars;
        m_selection.push_back(std::move(points_data));
    }

    m_centroid /= count;
}

void UvRotateCommand::init_from_uv_selection(UVEditorGLWidget* widget, const SelectionList& uv_list)
{
    m_widget = widget;

    auto& app = Application::instance();
    const auto session = app.get_session();
    const auto stage = session->get_current_stage();
    if (!stage)
    {
        return;
    }

    m_centroid.Set(0, 0);
    uint64_t count = 0;

    const auto time = app.get_current_time();
    const auto uv_primvar = m_widget->get_uv_primvar();
    const auto token_primvar = TfToken(uv_primvar.toStdString());
    auto rich_selection = Application::instance().get_rich_selection();
    rich_selection.set_soft_selection(uv_list);

    for (const auto& entry : uv_list)
    {
        const auto& path = entry.first;
        const auto& data = entry.second;

        auto prim = stage->GetPrimAtPath(entry.first);
        if (!prim)
        {
            continue;
        }

        auto primvars = UsdGeomPrimvarsAPI(prim);
        if (!primvars)
        {
            continue;
        }

        auto primvar = primvars.GetPrimvar(token_primvar);
        if (!primvar)
        {
            continue;
        }

        VtVec2fArray points;
        if (!primvar.Get(&points, time))
        {
            continue;
        }

        PointsData points_data;
        for (const auto& interval : data.get_point_index_intervals())
        {
            for (SelectionList::IndexType index = interval.start; index <= interval.end; ++index)
            {
                points_data.start_points[index].point = points[index];

                m_centroid += points[index];
            }
        }

        count += points_data.start_points.size();

        if (Application::instance().is_soft_selection_enabled())
        {
            for (const auto& weight : rich_selection.get_weights(entry.first))
            {
                const auto& point = points[weight.first];
                points_data.start_points[weight.first].point = point;
                points_data.start_points[weight.first].weight = weight.second;
            }
        }

        points_data.primvars = primvars;
        m_selection.push_back(std::move(points_data));
    }

    m_centroid /= count;
}

void UvRotateCommand::start()
{
    m_change_block = std::make_unique<commands::UsdEditsBlock>();
}

void UvRotateCommand::end()
{
    m_inverse = m_change_block->take_edits();
    m_change_block = nullptr;
}

bool UvRotateCommand::is_started() const
{
    return !!m_change_block;
}

void UvRotateCommand::apply_rotate(double angle)
{
    const auto& app = Application::instance();
    const auto time = app.get_current_time();

    const auto uv_primvar = m_widget->get_uv_primvar();
    const auto token_primvar = TfToken(uv_primvar.toStdString());

    auto angle_in_radians = angle * M_PI / 180.0;
    auto centroid = get_centroid();

    SdfChangeBlock change_block;

    for (const auto& select : m_selection)
    {
        auto primvar = select.primvars.GetPrimvar(token_primvar);
        if (!primvar)
        {
            continue;
        }

        VtArray<GfVec2f> points;
        if (!primvar.Get(&points, time))
        {
            continue;
        }

        for (auto point : select.start_points)
        {
            auto cos = std::cos(angle_in_radians * point.second.weight);
            auto sin = std::sin(angle_in_radians * point.second.weight);

            point.second.point -= centroid;
            auto tmp_x = point.second.point[0];
            point.second.point[0] = tmp_x * cos - point.second.point[1] * sin;
            point.second.point[1] = tmp_x * sin + point.second.point[1] * cos;
            point.second.point += centroid;
            points[point.first] = point.second.point;
        }

        primvar.Set(points, get_non_varying_time(primvar));

        m_widget->update_range(select.primvars.GetPath(), points);
    }
}

const PXR_NS::GfVec2f& UvRotateCommand::get_centroid() const
{
    return m_centroid;
}

void UvRotateCommand::do_cmd()
{
    if (m_inverse)
    {
        m_inverse->invert();

        auto& app = Application::instance();
        const auto time = app.get_current_time();

        const auto uv_primvar = m_widget->get_uv_primvar();
        const auto token_primvar = TfToken(uv_primvar.toStdString());

        for (const auto& select : m_selection)
        {
            auto primvar = select.primvars.GetPrimvar(token_primvar);
            if (!primvar)
            {
                continue;
            }

            VtArray<GfVec2f> points;
            if (!primvar.Get(&points, time))
            {
                continue;
            }

            m_widget->update_range(select.primvars.GetPath(), points);
        }
        m_widget->update();
    }
}

SelectionData::IndexIntervals UvRotateCommand::to_points_indices(const PXR_NS::UsdPrim& prim, const SelectionData& mesh_data) const
{
    SelectionData::IndexIntervals intervals;

    const auto points = mesh_data.get_point_index_intervals();
    const auto edges = mesh_data.get_edge_index_intervals();
    const auto elements = mesh_data.get_element_index_intervals();

    if (points.empty() && edges.empty() && elements.empty())
    {
        return intervals;
    }

    auto& app = Application::instance();
    const auto time = app.get_current_time();
    const auto session = app.get_session();
    const auto id = session->get_current_stage_id();
    auto& cache = session->get_stage_topology_cache(id);

    const auto topology = cache.get_topology(prim, time);

    intervals.insert(points);

    for (const auto& edge : edges)
    {
        for (SelectionData::IndexType index = edge.start; index <= edge.end; ++index)
        {
            auto tuple = topology->edge_map.get_vertices_by_edge_id(index);
            if (!std::get<bool>(tuple))
            {
                continue;
            }
            const auto vertices = std::get<PXR_NS::GfVec2i>(tuple);
            intervals.insert(vertices[0]);
            intervals.insert(vertices[1]);
        }
    }

    const auto& face_counts = topology->mesh_topology.GetFaceVertexCounts();
    const auto& face_indices = topology->mesh_topology.GetFaceVertexIndices();
    const auto& face_starts = topology->face_starts;
    for (const auto& element : elements)
    {
        for (auto index = element.start; index <= element.end; ++index)
        {
            const auto face_start = face_starts[index];
            for (int i = 0; i < face_counts[index]; ++i)
            {
                intervals.insert(face_indices[face_start + i]);
            }
        }
    }

    return intervals;
}

SelectionData::IndexIntervals UvRotateCommand::to_uv_points_indices(UVEditorGLWidget* widget, const PXR_NS::SdfPath& path,
                                                                    const SelectionData::IndexIntervals mesh_indices) const
{
    SelectionData::IndexIntervals intervals;

    if (mesh_indices.empty())
    {
        return intervals;
    }

    const auto prims_info = widget->get_prims_info();
    const auto find = prims_info.find(path);
    if (find == prims_info.end())
    {
        return intervals;
    }

    const auto& info = find->second;

    for (const auto& interval : mesh_indices)
    {
        for (auto index = interval.start; index <= interval.end; ++index)
        {
            const auto uv_indices = info.mesh_vertices_to_uv_vertices[index];
            intervals.insert(uv_indices.begin(), uv_indices.end());
        }
    }

    return intervals;
}

OPENDCC_NAMESPACE_CLOSE
