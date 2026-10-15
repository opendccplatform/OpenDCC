// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "copy_prims.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include <pxr/usd/sdf/copyUtils.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::CopyPrimsCommand::cmd_name = "copy_prims";

CommandSyntax commands::CopyPrimsCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPathVector>("selection", "Paths to the prims to be copied.")
        .kwarg<UsdStageRefPtr>("stage", "Stage")
        .description("The copy_prims command allows you to copy prims to clipboard.");
}

std::shared_ptr<Command> commands::CopyPrimsCommand::create_cmd()
{
    return std::make_shared<commands::CopyPrimsCommand>();
}

CommandResult commands::CopyPrimsCommand::execute(const CommandArgs& args)
{
    UsdEditsBlock change_block;
    SdfPathVector prim_paths;
    UsdStageRefPtr stage;

    if (auto paths_arg = args.get_arg<SdfPathVector>(0))
    {
        prim_paths = *paths_arg;
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageRefPtr>("stage"))
    {
        stage = *stage_kwarg;
    }
    else
    {
        stage = Application::instance().get_session()->get_current_stage();
    }

    if (!stage)
    {
        OPENDCC_WARN("Failed to copy prims: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (prim_paths.empty())
    {
        OPENDCC_WARN("Failed to copy prims: prim paths are empty.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto clipboard_stage = Application::get_usd_clipboard().get_new_clipboard_stage("prims");
    std::unordered_map<std::string, std::string> rename_targets_map;
    for (const auto& path : prim_paths)
    {
        auto prim = stage->GetPrimAtPath(path);
        const auto new_path = clipboard_stage->GetPseudoRoot().GetPath().AppendChild(prim.GetName());
        utils::flatten_prim(prim, new_path, clipboard_stage->GetRootLayer());
        rename_targets_map[prim.GetPath().GetString()] = new_path.GetString();
    }

    for (auto element : rename_targets_map)
    {
        utils::rename_targets(clipboard_stage, SdfPath(element.first), SdfPath(element.second));
    }

    Application::get_usd_clipboard().set_clipboard_stage(clipboard_stage);
    m_inverse = change_block.take_edits();
    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::CopyPrimsCommand::undo()
{
    do_cmd();
}

void commands::CopyPrimsCommand::redo()
{
    do_cmd();
}

void commands::CopyPrimsCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}

OPENDCC_NAMESPACE_CLOSE
