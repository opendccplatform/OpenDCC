/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/api.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    /**
     * Blocks call of CommandInterface::on_command_execute
     * for all Commands in current scope
     * and saves executed commands in CommandRouter.
     * After exit from current scope,
     * UndoCommandBlock creates UndoCommand with name "block_name" and
     * calls CommandInterface::finalize.
     * Newly created UndoCommand contains all commands executed in current scope.
     *
     * If number of UndoCommandBlock instances is more than one, saves "block_name"
     * of UndoCommandBlock in the highest scope.
     * Creates UndoCommand and calls CommandInterface::finalize
     * after exit from the highest scope.
     */
    class OPENDCC_BASE_COMMANDS_API UndoCommandBlock
    {
    public:
        UndoCommandBlock(const std::string& block_name = "UndoCommandBlock");
        ~UndoCommandBlock();
    };

    /**
     * Blocks call of CommandInterface::on_command_execute
     * for all Commands in current scope
     * and saves executed commands in CommandRouter.
     * After exit from current scope, clears CommandRouter.
     * You can get access to commands in CommandRouter by CommandRouter::transfer_commands
     * or CommandRouter::create_group_command before exit from current scope.
     *
     * If number of CommandBlock instances is more than one,
     * clears CommandRouter after exit from the highest scope.
     */
    class OPENDCC_BASE_COMMANDS_API CommandBlock
    {
    public:
        CommandBlock();
        ~CommandBlock();
    };

}

OPENDCC_NAMESPACE_CLOSE
