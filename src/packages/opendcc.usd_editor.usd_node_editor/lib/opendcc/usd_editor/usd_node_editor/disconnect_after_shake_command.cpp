// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/disconnect_after_shake_command.h"

#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/undo/block.h"

#include <pxr/pxr.h>
#include "pxr/base/tf/type.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<DisconnectAfterShakeCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command("node_editor_shake_disconnect", CommandSyntax(),
                                      [] { return std::make_shared<DisconnectAfterShakeCommand>(); });
}

void DisconnectAfterShakeCommand::undo()
{
    if (m_inverse)
    {
        m_inverse->invert();
    }
}

CommandArgs DisconnectAfterShakeCommand::make_args() const
{
    return CommandArgs();
}

CommandResult DisconnectAfterShakeCommand::execute(const CommandArgs& args)
{
    return CommandResult(CommandResult::Status::SUCCESS);
}

void DisconnectAfterShakeCommand::redo()
{
    if (m_inverse)
    {
        m_inverse->invert();
    }
}

void DisconnectAfterShakeCommand::start_block()
{
    m_change_block = std::make_unique<commands::UsdEditsBlock>();
}

void DisconnectAfterShakeCommand::end_block()
{
    m_inverse = m_change_block->take_edits();
    m_change_block = nullptr;
}

OPENDCC_NAMESPACE_CLOSE
