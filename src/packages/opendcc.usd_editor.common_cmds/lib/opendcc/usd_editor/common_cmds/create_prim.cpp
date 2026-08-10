// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/create_prim.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/base/pybind_bridge/boost_python.h"
#include <pxr/base/tf/pyError.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/usdGeom/metrics.h>
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

std::string commands::CreatePrimCommand::cmd_name = "create_prim";

CommandSyntax commands::CreatePrimCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<TfToken>("name", "Prim name")
        .arg<TfToken>("type", "Prim type")
        .kwarg<UsdStageWeakPtr>("stage", "The stage on which the prim will be created")
        .kwarg<SdfPath>("parent", "Parent prim")
        .kwarg<bool>("change_selection", "If true, update the selection after creating the prim, otherwise, do not proceed.")
        .result<SdfPath>("Created prim's path");
}

std::shared_ptr<Command> commands::CreatePrimCommand::create_cmd()
{
    return std::make_shared<commands::CreatePrimCommand>();
}

OPENDCC_NAMESPACE::CommandResult commands::CreatePrimCommand::execute(const CommandArgs& args)
{
    TfToken name = *args.get_arg<TfToken>(0);
    TfToken type = *args.get_arg<TfToken>(1);
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
        if (!type.IsEmpty())
            prim_spec->SetTypeName(type);
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

    m_inverse = block.take_edits();

    m_old_selection = Application::instance().get_selection();
    if (m_change_selection)
    {
        Application::instance().set_prim_selection({ new_path });
    }
    return CommandResult(CommandResult::Status::SUCCESS, new_path);
}

void OPENDCC_NAMESPACE::commands::CreatePrimCommand::do_cmd()
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

void OPENDCC_NAMESPACE::commands::CreatePrimCommand::redo()
{
    do_cmd();
}

void OPENDCC_NAMESPACE::commands::CreatePrimCommand::undo()
{
    do_cmd();
}

OPENDCC_NAMESPACE_CLOSE
