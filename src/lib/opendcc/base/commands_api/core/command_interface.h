/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/api.h"
#include "opendcc/base/commands_api/core/command.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    class OPENDCC_BASE_COMMANDS_API CommandInterface
    {
    public:
        static CommandResult execute(const std::string& command_name, const CommandArgs& args = CommandArgs(), bool undo_enable = true);
        static CommandResult execute(const std::shared_ptr<Command>& command, const CommandArgs& args = CommandArgs(), bool undo_enable = true);
        static void finalize(const std::shared_ptr<Command>& command, const CommandArgs& args);
        static void finalize(const std::shared_ptr<ToolCommand>& command);

    protected:
        friend class CommandRegistry;

        virtual void register_command(const std::string& name, const CommandSyntax& syntax) = 0;
        virtual void unregister_command(const std::string& name) = 0;
        virtual void on_command_execute(const std::shared_ptr<Command>& cmd, const CommandArgs& args, const CommandResult& result) = 0;
    };

}

OPENDCC_NAMESPACE_CLOSE
