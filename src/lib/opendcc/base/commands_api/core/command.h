/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/base/commands_api/core/command_syntax.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    class OPENDCC_BASE_COMMANDS_API CommandResult
    {
    public:
        enum class Status
        {
            SUCCESS,
            FAIL,
            INVALID_SYNTAX,
            INVALID_ARG,
            CMD_NOT_REGISTERED
        };

        CommandResult(Status result = Status::FAIL, std::shared_ptr<CommandArgBase> result_value = nullptr);
        template <class T>
        CommandResult(Status result, const T& result_value)
            : CommandResult(result)
        {
            m_result_value = std::make_shared<CommandArg<std::decay_t<T>>>(result_value);
        }

        operator bool() const;
        bool is_successful() const;
        const std::type_info& get_type_info() const;
        bool has_result() const;
        Status get_status() const;

        template <class T>
        std::shared_ptr<CommandArg<T>> get_result() const
        {
            return std::dynamic_pointer_cast<CommandArg<T>>(m_result_value);
        }

        std::shared_ptr<CommandArgBase> get_result() const;

        template <class T>
        bool is_holding() const
        {
            return get_type_info() == typeid(T);
        }

    private:
        std::shared_ptr<CommandArgBase> m_result_value;
        Status m_result = Status::FAIL;
    };

    class OPENDCC_BASE_COMMANDS_API Command
    {
    public:
        Command() = default;
        virtual ~Command() = default;

        virtual CommandResult execute(const CommandArgs& args) = 0;

        CommandSyntax get_syntax() const;
        std::string get_command_name() const;

    protected:
        friend class CommandRegistry;

        std::string m_command_name;
    };

    class OPENDCC_BASE_COMMANDS_API UndoCommand : virtual public Command
    {
    public:
        virtual ~UndoCommand() = default;

        virtual void undo();
        virtual void redo();

        virtual bool merge_with(UndoCommand* command);
    };

    class ToolCommand : virtual public Command
    {
    public:
        virtual CommandArgs make_args() const = 0;
    };

}

OPENDCC_NAMESPACE_CLOSE
