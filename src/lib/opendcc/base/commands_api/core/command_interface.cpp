// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/command_syntax.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include <opendcc/base/logging/logger.h>

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{
    OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.commands");

    namespace
    {
        bool validate_args(const CommandSyntax& syntax, const CommandArgs& args)
        {
            const auto& syntax_arg_types = syntax.get_arg_descriptors();
            const auto& target_args = args.get_args();

            if (syntax_arg_types.size() != target_args.size())
            {
                OPENDCC_ERROR("Unexpected argument count. Expected {}, got {}.", syntax_arg_types.size(), target_args.size());
                return false;
            }

            for (int i = 0; i < syntax_arg_types.size(); i++)
            {
                const auto& arg = syntax_arg_types[i];
                if (!target_args[i]->is_convertible(syntax_arg_types[i].type_indices))
                {
                    OPENDCC_ERROR("Cannot convert arg at pos '{}'.", i);
                    return false;
                }
            }

            const auto& syntax_kwarg_types = syntax.get_kwarg_descriptors();
            for (const auto& kwarg : args.get_kwargs())
            {
                auto reference = syntax_kwarg_types.find(kwarg.first);
                if (reference == syntax_kwarg_types.end())
                {
                    OPENDCC_ERROR("Unknown option \"{}\".", kwarg.first);
                    return false;
                }
                if (!kwarg.second->is_convertible(reference->second.type_indices))
                {
                    OPENDCC_ERROR("Incorrect type of \"{}\" argument.", kwarg.first);
                    return false;
                }
            }
            return true;
        }
    };

    CommandResult CommandInterface::execute(const std::string& command_name, const CommandArgs& args /*= CommandArgs()*/, bool undo_enable /*= true*/)
    {
        CommandSyntax syntax;
        if (!CommandRegistry::get_command_syntax(command_name, syntax))
            return CommandResult(CommandResult::Status::CMD_NOT_REGISTERED);

        if (!validate_args(syntax, args))
            return CommandResult(CommandResult::Status::INVALID_SYNTAX);

        auto command = CommandRegistry::create_command(command_name);
        const auto result = command->execute(args);
        if (result && undo_enable)
            CommandRegistry::command_executed(command, args, result);
        return result;
    }

    CommandResult CommandInterface::execute(const std::shared_ptr<Command>& command, const CommandArgs& args /*= CommandArgs()*/,
                                            bool undo_enable /*= true*/)
    {
        if (!validate_args(command->get_syntax(), args))
            return CommandResult(CommandResult::Status::INVALID_SYNTAX);

        const auto result = command->execute(args);
        if (result && undo_enable)
            CommandRegistry::command_executed(command, args, result);
        return result;
    }

    void CommandInterface::finalize(const std::shared_ptr<ToolCommand>& command)
    {
        finalize(command, command->make_args());
    }

    void CommandInterface::finalize(const std::shared_ptr<Command>& command, const CommandArgs& args)
    {
        if (validate_args(command->get_syntax(), args))
            CommandRegistry::command_executed(command, args, CommandResult(CommandResult::Status::SUCCESS));
    }

}

OPENDCC_NAMESPACE_CLOSE
