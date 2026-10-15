// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "paste_prims.h"
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

std::string commands::PastePrimsCommand::cmd_name = "paste_prims";

CommandSyntax commands::PastePrimsCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPath>("path", "Path to paste")
        .kwarg<UsdStageRefPtr>("stage", "Stage")
        .description("The paste_prims command allows you to paste prims from the clipboard.");
}

std::shared_ptr<Command> commands::PastePrimsCommand::create_cmd()
{
    return std::make_shared<commands::PastePrimsCommand>();
}

CommandResult commands::PastePrimsCommand::execute(const CommandArgs& args)
{
    UsdEditsBlock change_block;
    SdfPath paste_path;
    UsdStageWeakPtr stage;

    if (auto path_arg = args.get_arg<SdfPath>(0))
    {
        paste_path = *path_arg;
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
    {
        stage = *stage_kwarg;
    }
    else
    {
        stage = Application::instance().get_session()->get_current_stage();
    }

    if (!stage)
    {
        OPENDCC_WARN("Failed to paste prims: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (paste_path.IsEmpty())
    {
        OPENDCC_WARN("Failed to paste prims: paste paths are empty.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    SdfPathVector duplicated_paths;
    auto clipboard_stage = Application::get_usd_clipboard().get_clipboard_stage();
    if (!clipboard_stage)
    {
        OPENDCC_WARN("Failed to paste prims: clipboard data error.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    auto custom_data = clipboard_stage->GetRootLayer()->GetCustomLayerData();
    if (custom_data.find("stored_data_type")->second != "prims")
    {
        OPENDCC_WARN("Failed to paste prims: clipboard data error.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    std::unordered_map<std::string, std::string> rename_map;
    SdfPathVector pasted_prims_paths;
    for (const auto& prim : clipboard_stage->GetPseudoRoot().GetAllChildren())
    {
        const auto new_name = utils::get_new_name_for_prim(prim.GetName(), stage->GetPrimAtPath(paste_path), duplicated_paths);
        const auto new_path = paste_path.AppendChild(new_name);
        SdfCopySpec(prim.GetStage()->GetRootLayer(), prim.GetPath(), stage->GetEditTarget().GetLayer(), new_path);
        rename_map[prim.GetPath().GetString()] = new_path.GetString();
        pasted_prims_paths.push_back(new_path);
    }

    for (auto element : rename_map)
    {
        utils::rename_targets(stage, SdfPath(element.first), SdfPath(element.second));
    }
    Application::instance().set_prim_selection(pasted_prims_paths);
    m_inverse = change_block.take_edits();
    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::PastePrimsCommand::undo()
{
    do_cmd();
}

void commands::PastePrimsCommand::redo()
{
    do_cmd();
}

void commands::PastePrimsCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}

OPENDCC_NAMESPACE_CLOSE
