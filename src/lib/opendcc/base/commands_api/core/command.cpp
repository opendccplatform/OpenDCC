// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    CommandSyntax Command::get_syntax() const
    {
        CommandSyntax result;
        CommandRegistry::get_command_syntax(m_command_name, result);
        return result;
    }

    std::string Command::get_command_name() const
    {
        return m_command_name;
    }

    CommandResult::CommandResult(Status result, std::shared_ptr<CommandArgBase> result_value /*= nullptr*/)
        : m_result(result)
        , m_result_value(result_value)
    {
    }

    CommandResult::operator bool() const
    {
        return is_successful();
    }

    bool CommandResult::is_successful() const
    {
        return m_result == Status::SUCCESS;
    }

    const std::type_info& CommandResult::get_type_info() const
    {
        if (!m_result_value)
            return typeid(nullptr);

        return m_result_value->get_type_info();
    }

    bool CommandResult::has_result() const
    {
        return get_type_info() != typeid(nullptr);
    }

    CommandResult::Status CommandResult::get_status() const
    {
        return m_result;
    }

    std::shared_ptr<CommandArgBase> CommandResult::get_result() const
    {
        return m_result_value;
    }

    void UndoCommand::undo() {}

    void UndoCommand::redo() {}

    bool UndoCommand::merge_with(UndoCommand* command)
    {
        return false;
    }

}

OPENDCC_NAMESPACE_CLOSE
