/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include <QObject>
#include <vector>

OPENDCC_NAMESPACE_OPEN

class MoveAction;
class UsdGraphModel;

class MoveItemsCommand : public base::commands::UndoCommand
{
public:
    MoveItemsCommand(UsdGraphModel& model, std::vector<std::shared_ptr<MoveAction>>&& move_actions);
    ~MoveItemsCommand();
    virtual void undo() override;
    virtual void redo() override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    std::vector<std::shared_ptr<MoveAction>> m_move_actions;
    QMetaObject::Connection m_connection;
};

OPENDCC_NAMESPACE_CLOSE
