// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/create_material.h"
#include "opendcc/app/core/application.h"
#include "opendcc/usd_editor/common_cmds/create_prim.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "pxr/usd/usdUtils/pipeline.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandInterface;
using base::commands::CommandResult;

std::string commands::CreateMaterialCommand::cmd_name = "create_material";

CommandSyntax commands::CreateMaterialCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<TfToken>("name", "Material name")
        .kwarg<UsdStageWeakPtr>("stage", "The stage on which the material will be created")
        .kwarg<bool>("change_selection", "If true, update the selection after creating the material, otherwise, do not proceed.")
        .result<SdfPath>("Created material's path");
}

std::shared_ptr<Command> commands::CreateMaterialCommand::create_cmd()
{
    return std::make_shared<commands::CreateMaterialCommand>();
}

CommandResult commands::CreateMaterialCommand::execute(const CommandArgs& args)
{
    TfToken scope_name = UsdUtilsGetMaterialsScopeName();
    TfToken name = *args.get_arg<TfToken>(0);
    if (!TfIsValidIdentifier(name))
    {
        OPENDCC_WARN("Failed to create material with name '{}': invalid identifier.", name.GetText());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    m_create_mat_args = CommandArgs().arg(name).arg(TfToken("Material"));

    bool has_kwarg_with_stage = false;
    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
    {
        m_stage = *stage_kwarg;
        has_kwarg_with_stage = true;
        m_create_mat_args.kwarg("stage", stage_kwarg);
        m_remove_args.kwarg("stage", stage_kwarg);
    }
    else
    {
        m_stage = Application::instance().get_session()->get_current_stage();
    }

    if (!m_stage)
    {
        OPENDCC_WARN("Failed to create material: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto prim = m_stage->GetPseudoRoot();
    m_scope_path = prim.GetPath().AppendChild(scope_name);
    m_create_mat_args.kwarg("parent", m_scope_path);

    auto scope_prim = m_stage->GetPrimAtPath(m_scope_path);
    if (!scope_prim)
    {
        m_need_create_scope = true;

        m_create_scope_args = CommandArgs().arg(scope_name).arg(TfToken("Scope"));
        if (has_kwarg_with_stage)
        {
            m_create_scope_args.kwarg("stage", m_stage);
        }
    }

    if (auto change_selection = args.get_kwarg<bool>("change_selection"))
    {
        m_create_mat_args.kwarg("change_selection", change_selection);
        if (m_need_create_scope)
        {
            m_create_scope_args.kwarg("change_selection", change_selection);
        }
    }

    auto result = do_cmd();

    SdfPathVector removed_paths;
    if (m_need_create_scope)
    {
        removed_paths.push_back(m_scope_path);
    }
    removed_paths.push_back(m_material_path);
    m_remove_args.arg(removed_paths);

    return result;
}

CommandResult OPENDCC_NAMESPACE::commands::CreateMaterialCommand::do_cmd()
{
    if (m_need_create_scope)
    {
        auto prim = m_stage->GetPrimAtPath(m_scope_path);
        if (!prim)
        {
            CommandInterface::execute("create_prim", m_create_scope_args, false);
        }
    }

    auto material_result = CommandInterface::execute("create_prim", m_create_mat_args, false);
    if (material_result.has_result())
    {
        m_material_path = *material_result.get_result<SdfPath>();
    }
    else
    {
        OPENDCC_WARN("Failed to create material");
        return CommandResult(CommandResult::Status::FAIL);
    }

    return CommandResult(CommandResult::Status::SUCCESS, m_material_path);
}

void OPENDCC_NAMESPACE::commands::CreateMaterialCommand::redo()
{
    if (m_stage)
    {
        do_cmd();
    }
}

void OPENDCC_NAMESPACE::commands::CreateMaterialCommand::undo()
{
    CommandInterface::execute("remove_prims", m_remove_args, false);
}

OPENDCC_NAMESPACE_CLOSE
