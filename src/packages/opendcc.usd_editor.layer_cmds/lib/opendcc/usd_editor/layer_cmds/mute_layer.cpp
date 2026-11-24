// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/mute_layer.h"
#include "opendcc/usd_editor/layer_cmds/sublayer_utils.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"

#include <pxr/usd/sdf/layer.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

std::string commands::MuteLayerCommand::cmd_name = "mute_layer";

CommandSyntax commands::MuteLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("layer_identifier", "Identifier of the layer to mute/unmute")
        .kwarg<bool>("mute", "Whether to mute (true) or unmute (false) the layer. Defaults to toggling the current state.")
        .kwarg<UsdStageWeakPtr>("stage", "Stage to operate on (defaults to the current stage)")
        .description("Mutes or unmutes the layer with the given identifier on a stage.");
}

std::shared_ptr<Command> commands::MuteLayerCommand::create_cmd()
{
    return std::make_shared<commands::MuteLayerCommand>();
}

CommandResult commands::MuteLayerCommand::execute(const CommandArgs& args)
{
    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
        m_stage = *stage_kwarg;
    else
        m_stage = Application::instance().get_session()->get_current_stage();

    if (!m_stage)
    {
        OPENDCC_WARN("Failed to mute layer: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto identifier_arg = args.get_arg<std::string>(0);
    if (!identifier_arg)
    {
        OPENDCC_WARN("Failed to mute layer: no layer identifier specified.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }
    m_identifier = identifier_arg->get_value();

    const auto root_layer = m_stage->GetRootLayer();
    const auto session_layer = m_stage->GetSessionLayer();
    if ((root_layer && root_layer->GetIdentifier() == m_identifier) || (session_layer && session_layer->GetIdentifier() == m_identifier))
    {
        OPENDCC_WARN("Failed to mute layer: the root and session layers cannot be muted.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    m_layers = commands::utils::retain_layer_tree(SdfLayer::Find(m_identifier));
    m_old_muted = m_stage->IsLayerMuted(m_identifier);
    if (auto mute_kwarg = args.get_kwarg<bool>("mute"))
        m_new_muted = mute_kwarg->get_value();
    else
        m_new_muted = !m_old_muted;

    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void commands::MuteLayerCommand::set_muted(bool muted)
{
    if (!m_stage)
        return;
    if (muted)
        m_stage->MuteLayer(m_identifier);
    else
        m_stage->UnmuteLayer(m_identifier);
}

void commands::MuteLayerCommand::redo()
{
    set_muted(m_new_muted);
}

void commands::MuteLayerCommand::undo()
{
    set_muted(m_old_muted);
}

OPENDCC_NAMESPACE_CLOSE
