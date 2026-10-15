// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/clear_edits.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/prim.h>
#include "opendcc/base/commands_api/core/command_registry.h"

#include <usd_fallback_proxy/core/usd_prim_fallback_proxy.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

CommandSyntax commands::ClearEditsCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPathVector>("paths", "Paths to properties or prims")
        .kwarg<UsdStageWeakPtr>("stage", "Stage")
        .kwarg<std::vector<TfToken>>("metadata_tokens", "Metadata tokens")
        .description("The clear_edits command allows you to clear property edits on stage edit layer.");
}

std::string commands::ClearEditsCommand::cmd_name = "clear_edits";

std::shared_ptr<Command> commands::ClearEditsCommand::create_cmd()
{
    return std::make_shared<commands::ClearEditsCommand>();
}

CommandResult commands::ClearEditsCommand::execute(const CommandArgs& args)
{
    SdfPathVector paths;
    std::vector<TfToken> metadata_tokens;

    if (auto prims_arg = args.get_arg<SdfPathVector>(0))
    {
        paths = *prims_arg;
    }
    else
    {
        OPENDCC_WARN("Failed to clear edits: empty properties paths.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (auto metadata_kwarg = args.get_kwarg<std::vector<TfToken>>("metadata_tokens"))
    {
        metadata_tokens = *metadata_kwarg;
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
    {
        m_stage = *stage_kwarg;
    }
    else
    {
        m_stage = Application::instance().get_session()->get_current_stage();
    }

    if (!m_stage)
    {
        OPENDCC_WARN("Failed to clear edits: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto edit_layer = m_stage->GetEditTarget().GetLayer();

    UsdEditsBlock change_block;

    for (const auto& path : paths)
    {
        auto property = m_stage->GetPropertyAtPath(path);
        if (property.IsValid())
        {
            property.GetPrim().RemoveProperty(property.GetName());
            continue;
        }

        auto sdf_prim = edit_layer->GetPrimAtPath(path.GetPrimPath());
        if (sdf_prim)
        {
            for (auto meta_token : metadata_tokens)
            {
                if (sdf_prim->HasField(meta_token))
                {
                    sdf_prim->ClearField(meta_token);
                }
                else if (sdf_prim->HasInfo(meta_token))
                {
                    sdf_prim->ClearInfo(meta_token);
                }
            }
        }
    }

    m_inverse = change_block.take_edits();
    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::ClearEditsCommand::undo()
{
    do_cmd();
}

void commands::ClearEditsCommand::redo()
{
    do_cmd();
}

void commands::ClearEditsCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}
OPENDCC_NAMESPACE_CLOSE
