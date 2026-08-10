// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/create_mesh.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/base/pybind_bridge/boost_python.h"
#include <pxr/base/tf/pyError.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

std::string commands::CreateMeshCommand::cmd_name = "create_mesh";

CommandSyntax commands::CreateMeshCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<TfToken>("name", "Prim name")
        .arg<TfToken>("shape", "It's either 'plane' or 'sphere'")
        .kwarg<int>("u_segments", "The number of segments in the U direction")
        .kwarg<int>("v_segments", "The number of segments in the V direction")
        .kwarg<float>("width", "The width of the mesh")
        .kwarg<float>("depth", "The depth of the mesh")
        .kwarg<float>("radius", "The radius of the sphere")
        .kwarg<UsdStageWeakPtr>("stage", "The stage on which the prim will be created")
        .kwarg<SdfPath>("parent", "Parent prim")
        .kwarg<bool>("change_selection", "If true, update the selection after creating the prim, otherwise, do not proceed.")
        .result<SdfPath>("Created prim's path");
}

std::shared_ptr<Command> commands::CreateMeshCommand::create_cmd()
{
    return std::make_shared<commands::CreateMeshCommand>();
}

enum MeshShape
{
    Plane,
    Sphere
};

OPENDCC_NAMESPACE::CommandResult commands::CreateMeshCommand::execute(const CommandArgs& args)
{
    TfToken name = *args.get_arg<TfToken>(0);
    TfToken shape = *args.get_arg<TfToken>(1);

    int u_seg = 1;
    int v_seg = 1;

    MeshShape mesh_shape;

    if (shape == "plane")
    {
        mesh_shape = Plane;
    }
    else if (shape == "sphere")
    {
        mesh_shape = Sphere;
    }
    else
    {
        OPENDCC_WARN("Unknown mesh shape.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    float width = 1.0f;
    float depth = 1.0f;
    float radius = 1.0f;

    if (auto u_segments_kwarg = args.get_kwarg<int>("u_segments"))
        u_seg = *u_segments_kwarg;
    if (auto v_segments_kwarg = args.get_kwarg<int>("v_segments"))
        v_seg = *v_segments_kwarg;
    if (auto width_kwarg = args.get_kwarg<float>("width"))
        width = *width_kwarg;
    if (auto depth_kwarg = args.get_kwarg<float>("depth"))
        depth = *depth_kwarg;
    if (auto radius_kwarg = args.get_kwarg<float>("radius"))
        radius = *radius_kwarg;

    SdfPath parent_path;
    UsdStageWeakPtr stage;
    if (auto parent_kwarg = args.get_kwarg<SdfPath>("parent"))
        parent_path = *parent_kwarg;
    else
        parent_path = SdfPath::AbsoluteRootPath();

    if (auto change_selection = args.get_kwarg<bool>("change_selection"))
        m_change_selection = *change_selection;

    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
        stage = *stage_kwarg;
    else
        stage = Application::instance().get_session()->get_current_stage();

    if (!TfIsValidIdentifier(name))
    {
        OPENDCC_WARN("Failed to create prim with name '{}': invalid identifier.", name.GetText());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }
    if (!stage)
    {
        OPENDCC_WARN("Failed to create prim: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto parent_prim = stage->GetPrimAtPath(parent_path);
    if (!parent_prim)
    {
        OPENDCC_WARN("Failed to create prim: parent prim doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto new_name = commands::utils::get_new_name_for_prim(name, parent_prim);
    auto new_path = SdfPath(parent_prim.GetPath().AppendChild(new_name));
    auto target_path = stage->GetEditTarget().MapToSpecPath(new_path);
    if (target_path.IsEmpty())
    {
        OPENDCC_WARN("Failed to create prim: target path is empty.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    SdfPrimSpecHandle prim_spec;
    UsdEditsBlock block;
    {
        SdfChangeBlock block;

        prim_spec = SdfCreatePrimInLayer(stage->GetEditTarget().GetLayer(), target_path);
        prim_spec->SetSpecifier(SdfSpecifierDef);
        prim_spec->SetTypeName("Mesh");
        if (!prim_spec)
            return CommandResult(CommandResult::Status::FAIL);

        if (UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z)
        {
            auto rotate_spec = SdfAttributeSpec::New(prim_spec, "xformOp:rotateXYZ", SdfValueTypeNames->Vector3f);
            rotate_spec->SetDefaultValue(VtValue(GfVec3f(90, 0, 0)));
            auto op_order_spec = SdfAttributeSpec::New(prim_spec, "xformOpOrder", SdfValueTypeNames->TokenArray);
            op_order_spec->SetDefaultValue(VtValue(VtTokenArray({ TfToken("xformOp:rotateXYZ") })));
        }
    }

    auto mesh_prim = stage->GetPrimAtPath(new_path);
    auto mesh = UsdGeomMesh(mesh_prim);
    mesh.CreateDoubleSidedAttr(VtValue(true));
    auto mesh_primvars = UsdGeomPrimvarsAPI(mesh_prim);
    auto boundable = UsdGeomBoundable(mesh_prim);

    switch (mesh_shape)
    {
    case Plane:
    {
        u_seg = std::max(1, u_seg);
        v_seg = std::max(1, v_seg);

        VtArray<int> face_vertex_counts_data(u_seg * v_seg, 4);
        mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts_data);

        VtArray<int> face_vertex_indices_data;
        face_vertex_indices_data.reserve(u_seg * v_seg * 4);
        const int row = u_seg + 1;
        for (int v = 0; v < v_seg; v++)
        {
            for (int u = 0; u < u_seg; u++)
            {
                face_vertex_indices_data.push_back(u + row * v);
                face_vertex_indices_data.push_back(u + row * v + 1);
                face_vertex_indices_data.push_back(u + row * (v + 1) + 1);
                face_vertex_indices_data.push_back(u + row * (v + 1));
            }
        }

        mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices_data);

        VtArray<GfVec3f> points_data;
        VtArray<GfVec2f> st_data;
        const int num_points = (u_seg + 1) * (v_seg + 1);
        points_data.reserve(num_points);
        st_data.reserve(num_points);

        for (int v = v_seg; v >= 0; v--)
        {
            float v_div = float(v) / v_seg;
            for (int u = 0; u < (u_seg + 1); u++)
            {
                float u_div = float(u) / u_seg;

                points_data.push_back(GfVec3f(u_div * width - width / 2.0f, 0.0f, v_div * depth - depth / 2.0f));
                st_data.push_back(GfVec2f(u_div, 1.0f - v_div));
            }
        }

        mesh.CreatePointsAttr().Set(points_data);

        auto primvar = mesh_primvars.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray);
        primvar.SetInterpolation(UsdGeomTokens->faceVarying);
        primvar.Set(st_data);
        primvar.SetIndices(face_vertex_indices_data);

        VtArray<GfVec3f> extent_data;
        extent_data.reserve(2);
        extent_data.push_back(GfVec3f(-width / 2.0f, 0.0f, -depth / 2.0f));
        extent_data.push_back(GfVec3f(width / 2.0f, 0.0f, depth / 2.0f));

        boundable.CreateExtentAttr().Set(extent_data);

        break;
    }
    case Sphere:
    {
        u_seg = std::max(3, u_seg);
        v_seg = std::max(3, v_seg);

        VtArray<int> face_vertex_counts_data(u_seg * v_seg, 4);

        for (int v = 0; v < v_seg; v += v_seg - 1)
        {
            for (int u = 0; u < u_seg; u++)
            {
                face_vertex_counts_data[v * u_seg + u] = 3;
            }
        }

        mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts_data);

        VtArray<int> face_vertex_indices_data;
        VtArray<int> st_indices_data;

        VtArray<GfVec3f> points_data;
        VtArray<GfVec2f> st_data;

        int number_of_indices = u_seg * (v_seg - 2) * 4 + u_seg * 6;
        face_vertex_indices_data.reserve(number_of_indices);
        st_indices_data.reserve(number_of_indices);

        int number_of_points = u_seg * (v_seg - 1) + 2;
        points_data.reserve(number_of_points);
        st_data.reserve(number_of_points);

        points_data.push_back(GfVec3f(0.0f, radius, 0.0f));
        st_data.push_back(GfVec2f(0.5f, 1.0f));

        for (int v = 0; v < (v_seg - 1); v++)
        {
            float polar = M_PI * float(v + 1) / float(v_seg);
            float sp = sin(polar);
            float cp = cos(polar) * radius;

            for (int u = 0; u < u_seg; u++)
            {
                float azimuth = 2.0f * M_PI * float(u) / float(u_seg);
                float sa = sin(azimuth);
                float ca = cos(azimuth);
                float x = sp * ca * radius;
                float y = cp;
                float z = sp * sa * radius;
                points_data.push_back(GfVec3f(x, y, z));
            }
        }

        for (int v = 0; v < (v_seg - 1); v++)
        {
            float v_div = float(v + 1) / v_seg;
            for (int u = 0; u < (u_seg + 1); u++)
            {
                float u_div = float(u) / u_seg;
                st_data.push_back(GfVec2f(1.0f - u_div, 1.0f - v_div));
            }
        }

        points_data.push_back(GfVec3f(0.0f, -radius, 0.0f));
        st_data.push_back(GfVec2f(0.5f, 0.0f));

        for (int u = 0; u < u_seg; u++)
        {
            face_vertex_indices_data.push_back(0);
            face_vertex_indices_data.push_back((u + 1) % u_seg + 1);
            face_vertex_indices_data.push_back(u + 1);
            st_indices_data.push_back(0);
            st_indices_data.push_back(u + 2);
            st_indices_data.push_back(u + 1);
        }

        for (int v = 0; v < (v_seg - 2); v++)
        {
            int aStart = v * u_seg + 1;
            int bStart = (v + 1) * u_seg + 1;
            int st_aStart = v * (u_seg + 1) + 1;
            int st_bStart = (v + 1) * (u_seg + 1) + 1;
            for (int u = 0; u < u_seg; u++)
            {
                face_vertex_indices_data.push_back(aStart + u);
                face_vertex_indices_data.push_back(aStart + (u + 1) % u_seg);
                face_vertex_indices_data.push_back(bStart + (u + 1) % u_seg);
                face_vertex_indices_data.push_back(bStart + u);
                st_indices_data.push_back(st_aStart + u);
                st_indices_data.push_back(st_aStart + u + 1);
                st_indices_data.push_back(st_bStart + u + 1);
                st_indices_data.push_back(st_bStart + u);
            }
        }

        for (int u = 0; u < u_seg; u++)
        {
            face_vertex_indices_data.push_back((int)points_data.size() - 1);
            face_vertex_indices_data.push_back(u + u_seg * (v_seg - 2) + 1);
            face_vertex_indices_data.push_back((u + 1) % u_seg + u_seg * (v_seg - 2) + 1);
            st_indices_data.push_back((int)st_data.size() - 1);
            st_indices_data.push_back(u + (u_seg + 1) * (v_seg - 2) + 1);
            st_indices_data.push_back(u + (u_seg + 1) * (v_seg - 2) + 2);
        }

        mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices_data);
        mesh.CreatePointsAttr().Set(points_data);

        auto primvar = mesh_primvars.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray);
        primvar.SetInterpolation(UsdGeomTokens->faceVarying);
        primvar.Set(st_data);
        primvar.SetIndices(st_indices_data);

        VtArray<GfVec3f> extent_data;
        extent_data.reserve(2);
        extent_data.push_back(GfVec3f(-radius, -radius, -radius));
        extent_data.push_back(GfVec3f(radius, radius, radius));

        boundable.CreateExtentAttr().Set(extent_data);

        break;
    }
    }

    m_inverse = block.take_edits();

    m_old_selection = Application::instance().get_selection();
    if (m_change_selection)
    {
        Application::instance().set_prim_selection({ new_path });
    }
    return CommandResult(CommandResult::Status::SUCCESS, new_path);
}

void OPENDCC_NAMESPACE::commands::CreateMeshCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
    auto cur_selection = Application::instance().get_selection();
    if (m_change_selection)
    {
        Application::instance().set_selection(m_old_selection);
    }
    m_old_selection = cur_selection;
}

void OPENDCC_NAMESPACE::commands::CreateMeshCommand::redo()
{
    do_cmd();
}

void OPENDCC_NAMESPACE::commands::CreateMeshCommand::undo()
{
    do_cmd();
}

OPENDCC_NAMESPACE_CLOSE
