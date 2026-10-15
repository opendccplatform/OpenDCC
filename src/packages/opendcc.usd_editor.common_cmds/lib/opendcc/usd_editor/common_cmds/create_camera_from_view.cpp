// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "create_camera_from_view.h"

#include <pxr/usd/usdGeom/camera.h>
#include <pxr/base/gf/transform.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/usd/schemaRegistry.h>

#include "opendcc/usd_editor/common_cmds/create_prim.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/command_utils.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandArg;
using base::commands::CommandResult;

CommandSyntax commands::CreateCameraFromViewCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<GfCamera>("view", "View GfCamera")
        .kwarg<UsdStageWeakPtr>("stage", "The stage on which the prim will be created")
        .kwarg<SdfPath>("parent", "Parent prim")
        .result<SdfPath>("Created camera prim's path");
}

std::string commands::CreateCameraFromViewCommand::cmd_name = "create_camera_from_view";

std::shared_ptr<Command> commands::CreateCameraFromViewCommand::create_cmd()
{
    return std::make_shared<commands::CreateCameraFromViewCommand>();
}

static TfToken projection_to_token(GfCamera::Projection projection)
{
    switch (projection)
    {
    case GfCamera::Perspective:
        return UsdGeomTokens->perspective;
    case GfCamera::Orthographic:
        return UsdGeomTokens->orthographic;
    default:
        OPENDCC_WARN("Unknown projection type {}", static_cast<int>(projection));
        return TfToken();
    }
}

CommandResult commands::CreateCameraFromViewCommand::execute(const CommandArgs& args)
{
    // view
    const auto view_ptr = args.get_arg<GfCamera>(0);

    if (!view_ptr)
    {
        OPENDCC_WARN("Not found \"view\" arg");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    const GfCamera view = *view_ptr;

    // stage
    const std::shared_ptr<CommandArg<UsdStageWeakPtr>> stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage");
    const UsdStageWeakPtr stage =
        stage_kwarg ? UsdStageWeakPtr(*stage_kwarg) : UsdStageWeakPtr(Application::instance().get_session()->get_current_stage());

    if (!stage)
    {
        OPENDCC_WARN("Failed to create camera: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    // parent
    const auto parent_kwarg = args.get_kwarg<SdfPath>("parent");
    const SdfPath parent_path = parent_kwarg ? SdfPath(*parent_kwarg) : SdfPath(SdfPath::AbsoluteRootPath());

    const UsdPrim parent_prim = stage->GetPrimAtPath(parent_path);
    if (!parent_prim)
    {
        OPENDCC_WARN("Failed to create prim: parent prim doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    // create
    const TfToken name = TfToken("Camera");
    const TfToken type = TfToken("Camera");

    const SdfLayerHandle& layer = stage->GetEditTarget().GetLayer();

    const TfToken new_name = commands::utils::get_new_name_for_prim(name, parent_prim);
    const SdfPath new_path = SdfPath(parent_prim.GetPath().AppendChild(new_name));

    UsdEditsBlock block;
    {
        const SdfChangeBlock block;

        SdfPrimSpecHandle prim_spec =
            SdfPrimSpec::New(layer->GetPrimAtPath(new_path.GetParentPath()), new_path.GetName(), PXR_NS::SdfSpecifierDef, type.GetString());

        if (!prim_spec)
        {
            return CommandResult(CommandResult::Status::FAIL);
        }

        const auto clipping_range = view.GetClippingRange();
        const auto clipping_planes = view.GetClippingPlanes();

        VtVec4fArray vt_clipping_planes;

        for (const auto& clipping_plane : clipping_planes)
        {
            vt_clipping_planes.push_back(clipping_plane);
        }

        const GfTransform transform(view.GetTransform());

        const auto rotate = transform.GetRotation().Decompose(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis());

        const struct
        {
            const char* name;
            SdfValueTypeName type;
            VtValue value;
        } properties_info[] = {
            { "projection", SdfValueTypeNames->Token, VtValue(projection_to_token(view.GetProjection())) },
            { "horizontalAperture", SdfValueTypeNames->Float, VtValue(view.GetHorizontalAperture()) },
            { "verticalAperture", SdfValueTypeNames->Float, VtValue(view.GetVerticalAperture()) },
            { "horizontalApertureOffset", SdfValueTypeNames->Float, VtValue(view.GetHorizontalApertureOffset()) },
            { "verticalApertureOffset", SdfValueTypeNames->Float, VtValue(view.GetVerticalApertureOffset()) },
            { "focalLength", SdfValueTypeNames->Float, VtValue(view.GetFocalLength()) },
            { "clippingRange", SdfValueTypeNames->Float2, VtValue(GfVec2f(clipping_range.GetMin(), clipping_range.GetMax())) },
            { "clippingPlanes", SdfValueTypeNames->Float4Array, VtValue(vt_clipping_planes) },
            { "fStop", SdfValueTypeNames->Float, VtValue(view.GetFStop()) },
            { "focusDistance", SdfValueTypeNames->Float, VtValue(view.GetFocusDistance()) },
            { "xformOp:rotateXYZ", SdfValueTypeNames->Float3, VtValue(GfVec3f(rotate[2], rotate[1], rotate[0])) },
            { "xformOp:scale", SdfValueTypeNames->Float3, VtValue(GfVec3f(transform.GetScale())) },
            { "xformOp:translate", SdfValueTypeNames->Double3, VtValue(GfVec3d(transform.GetTranslation())) },
            { "xformOpOrder", SdfValueTypeNames->TokenArray,
              VtValue(VtTokenArray({
                  TfToken("xformOp:translate"),
                  TfToken("xformOp:rotateXYZ"),
                  TfToken("xformOp:scale"),
              })) },
        };

#if PXR_VERSION < 2008
        // usd 19.11
#else
        // usd 20.08
        const auto prim_def = UsdSchemaRegistry::GetInstance().FindConcretePrimDefinition(name);
#endif

        for (const auto& property_info : properties_info)
        {
#if PXR_VERSION < 2008
            // usd 19.11
            auto attr_spec = UsdSchemaRegistry::GetAttributeDefinition(name, TfToken(property_info.name));

            if (attr_spec && attr_spec->GetDefaultValue() == property_info.value)
            {
                continue;
            }
#else
            // usd 20.08
            auto attr_spec = prim_def->GetSchemaAttributeSpec(TfToken(property_info.name));

            if (attr_spec && attr_spec->GetDefaultValue() == property_info.value)
            {
                continue;
            }
#endif

            SdfPropertySpecHandle property = SdfAttributeSpec::New(prim_spec, property_info.name, property_info.type);
            if (!property)
            {
                return CommandResult(CommandResult::Status::FAIL);
            }
            if (!property->SetDefaultValue(property_info.value))
            {
                return CommandResult(CommandResult::Status::FAIL);
            }
        }
    }

    m_inverse = block.take_edits();

    return CommandResult(CommandResult::Status::SUCCESS, new_path);
}

void commands::CreateCameraFromViewCommand::undo()
{
    do_cmd();
}

void commands::CreateCameraFromViewCommand::redo()
{
    do_cmd();
}

void commands::CreateCameraFromViewCommand::do_cmd()
{
    if (m_inverse)
    {
        m_inverse->invert();
    }
}

OPENDCC_NAMESPACE_CLOSE
