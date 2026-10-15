/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/api.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    /**
     * Saves all executed or finalized commands while CommandBlock or UndoCommandBlock are active.
     */
    class OPENDCC_BASE_COMMANDS_API CommandRouter
    {
    public:
        friend class CommandBlock;
        friend class UndoCommandBlock;
        friend class CommandRegistry;

        /**
         * Creates UndoCommand with name from highest UndoCommandBlock and calls CommandInterface::finalize,
         * then clears CommandRouter
         */
        static void create_group_command();

        /**
         * Transfers the Commands saved by UndoCommandBlock or CommandBlock from CommandRouter
         * to the vector "commands",
         * then clears CommandRouter
         */
        static void transfer_commands(std::vector<std::shared_ptr<UndoCommand>>& commands);

        /**
         * returns true if UndoCommandBlock or CommandBlock are active
         */
        static bool lock_execute();

    private:
        static CommandRouter& instance();

        CommandRouter() = default;
        CommandRouter(const CommandRouter&) = delete;
        CommandRouter(CommandRouter&&) = delete;
        CommandRouter& operator=(const CommandRouter&) = delete;
        CommandRouter& operator=(CommandRouter&&) = delete;

        /**
         * clears m_block_name and m_commands
         */
        static void clear();

        /**
         * adds "cmd" to m_commands
         */
        static void add_command(const std::shared_ptr<Command>& cmd);

        int32_t m_depth = 0;

        std::string m_block_name;
        std::vector<std::shared_ptr<UndoCommand>> m_commands;
    };

}

OPENDCC_NAMESPACE_CLOSE
