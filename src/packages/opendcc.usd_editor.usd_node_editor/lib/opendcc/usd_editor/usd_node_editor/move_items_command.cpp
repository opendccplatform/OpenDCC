// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/move_items_command.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/usd_editor/usd_node_editor/graph_model.h"
#include <pxr/base/tf/type.h>

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::CommandResult;

MoveItemsCommand::MoveItemsCommand(UsdGraphModel& model, std::vector<std::shared_ptr<MoveAction>>&& move_actions)
    : m_move_actions(std::move(move_actions))
{
    m_command_name = "move_node_editor_items";
    m_connection = QObject::connect(&model, &QObject::destroyed, [this] {
        m_move_actions.clear();
        QObject::disconnect(m_connection);
    });
}

MoveItemsCommand::~MoveItemsCommand()
{
    if (m_connection)
        QObject::disconnect(m_connection);
}

void MoveItemsCommand::undo()
{
    for (const auto& action : m_move_actions)
        action->undo();
}

CommandResult MoveItemsCommand::execute(const CommandArgs& args)
{
    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void MoveItemsCommand::redo()
{
    for (const auto& action : m_move_actions)
        action->redo();
}

OPENDCC_NAMESPACE_CLOSE
