// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/usd_edit_undo_command.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/base/commands_api/core/command_registry.h"

#include <pxr/base/tf/type.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdEditUndoCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command("usd_edit_undo", CommandSyntax().arg<std::shared_ptr<commands::UndoInverse>>("undo_inverse"),
                                      [] { return std::make_shared<UsdEditUndoCommand>(); });
};

void UsdEditUndoCommand::redo()
{
    m_inverse->invert();
}

void UsdEditUndoCommand::undo()
{
    m_inverse->invert();
}

CommandResult UsdEditUndoCommand::execute(const CommandArgs& args)
{
    m_inverse = *args.get_arg<std::shared_ptr<commands::UndoInverse>>(0);
    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void UsdEditUndoCommand::set_state(std::shared_ptr<commands::UndoInverse> inverse)
{
    m_inverse = inverse;
}
OPENDCC_NAMESPACE_CLOSE
