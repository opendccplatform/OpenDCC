// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/remove_prims.h"

#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/session.h"

#include "pxr/usd/usdUtils/pipeline.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::RemovePrimsCommand::cmd_name = "remove_prims";

CommandSyntax commands::RemovePrimsCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPath, SdfPathVector>("paths", "The Prim Paths that will need to be removed.")
        .kwarg<UsdStageWeakPtr>("stage", "The stage on which the prims will need to be removed.")
        .result<SdfPathVector>("Removed prim's paths");
}

std::shared_ptr<Command> commands::RemovePrimsCommand::create_cmd()
{
    return std::make_shared<commands::RemovePrimsCommand>();
}

CommandResult commands::RemovePrimsCommand::execute(const CommandArgs& args)
{
    SdfPathVector paths;
    if (auto arg_path = args.get_arg<SdfPath>(0))
    {
        paths.push_back(*arg_path);
    }
    else if (auto arg_paths = args.get_arg<SdfPathVector>(0))
    {
        paths = *arg_paths;
    }

    if (paths.empty())
    {
        OPENDCC_WARN("Failed to remove prims: paths is empty.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    UsdStageWeakPtr stage;
    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
    {
        stage = *stage_kwarg;
    }
    else
    {
        stage = Application::instance().get_session()->get_current_stage();
    }

    const auto& layer = stage->GetEditTarget().GetLayer();

    UsdEditsBlock change_block;
    SdfBatchNamespaceEdit batch;
    {
        SdfChangeBlock sdf_change_block;
        for (const auto& current_path : paths)
        {
            auto prim = stage->GetPrimAtPath(current_path);
            if (!prim)
            {
                OPENDCC_WARN("Failed to remove prim at path '%s': prim doesn't exist.", current_path.GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            const auto remove_edit = SdfNamespaceEdit::Remove(current_path);
            batch.Add(remove_edit);
        }

        SdfNamespaceEditDetailVector details;
        if (layer->CanApply(batch, &details))
        {
            for (const auto& edit : batch.GetEdits())
            {
                utils::delete_targets(stage, edit.currentPath);
            }
            if (!layer->Apply(batch))
            {
                OPENDCC_WARN("Failed to remove prim.");
                return CommandResult(CommandResult::Status::FAIL);
            }
        }
        else
        {
            if (!details.empty())
            {
                OPENDCC_WARN("Failed to remove prim: %s", details.front().reason.c_str());
                return CommandResult(CommandResult::Status::FAIL);
            }
        }
    }

    m_inverse = change_block.take_edits();
    auto selection = Application::instance().get_selection();
    selection.remove_prims(paths);
    Application::instance().set_selection(selection);

    return CommandResult(CommandResult::Status::SUCCESS, paths);
}

void commands::RemovePrimsCommand::undo()
{
    do_cmd();
}

void commands::RemovePrimsCommand::redo()
{
    do_cmd();
}

void commands::RemovePrimsCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}
OPENDCC_NAMESPACE_CLOSE
