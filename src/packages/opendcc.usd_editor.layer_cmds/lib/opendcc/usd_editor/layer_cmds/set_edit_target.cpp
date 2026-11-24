// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/set_edit_target.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"

#include <pxr/usd/usd/editTarget.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

std::string commands::SetEditTargetCommand::cmd_name = "set_edit_target";

CommandSyntax commands::SetEditTargetCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("layer_identifier", "Identifier of the layer to make the edit target")
        .kwarg<UsdStageWeakPtr>("stage", "Stage to operate on (defaults to the current stage)")
        .description("Sets the stage's current edit target to the layer with the given identifier.");
}

std::shared_ptr<Command> commands::SetEditTargetCommand::create_cmd()
{
    return std::make_shared<commands::SetEditTargetCommand>();
}

CommandResult commands::SetEditTargetCommand::execute(const CommandArgs& args)
{
    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
        m_stage = *stage_kwarg;
    else
        m_stage = Application::instance().get_session()->get_current_stage();

    if (!m_stage)
    {
        OPENDCC_WARN("Failed to set edit target: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto identifier_arg = args.get_arg<std::string>(0);
    if (!identifier_arg)
    {
        OPENDCC_WARN("Failed to set edit target: no layer identifier specified.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }
    const std::string& identifier = identifier_arg->get_value();

    m_new_layer = SdfLayer::Find(identifier);
    if (!m_new_layer)
        m_new_layer = SdfLayer::FindOrOpen(identifier);
    if (!m_new_layer)
    {
        OPENDCC_WARN("Failed to set edit target: layer '{}' could not be found.", identifier.c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (m_stage->IsLayerMuted(m_new_layer->GetIdentifier()))
    {
        OPENDCC_WARN("Failed to set edit target: layer '{}' is muted.", identifier.c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (!m_stage->HasLocalLayer(m_new_layer))
        return CommandResult(CommandResult::Status::INVALID_ARG);
    m_old_target = m_stage->GetEditTarget();
    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void commands::SetEditTargetCommand::redo()
{
    if (m_stage && m_new_layer)
        m_stage->SetEditTarget(m_stage->GetEditTargetForLocalLayer(m_new_layer));
}

void commands::SetEditTargetCommand::undo()
{
    if (m_stage && m_old_target.IsValid())
        m_stage->SetEditTarget(m_old_target);
}

OPENDCC_NAMESPACE_CLOSE
