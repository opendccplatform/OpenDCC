// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/router.h"
#include "opendcc/base/commands_api/core/command_interface.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    class GroupCommand : public UndoCommand
    {
    public:
        friend class CommandRouter;

        GroupCommand(const std::vector<std::shared_ptr<UndoCommand>>& commands)
            : m_commands(commands)
        {
        }
        virtual ~GroupCommand() = default;

        virtual CommandResult execute(const CommandArgs& args) override { return CommandResult(CommandResult::Status::CMD_NOT_REGISTERED); }

        virtual void undo() override
        {
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
            {
                (*it)->undo();
            }
        }

        virtual void redo() override
        {
            for (auto& command : m_commands)
            {
                command->redo();
            }
        }

    private:
        std::vector<std::shared_ptr<UndoCommand>> m_commands;
    };

    //

    CommandRouter& CommandRouter::instance()
    {
        static CommandRouter router;
        return router;
    }

    void CommandRouter::add_command(const std::shared_ptr<Command>& cmd)
    {
        if (auto undo_cmd = std::dynamic_pointer_cast<UndoCommand>(cmd))
        {
            instance().m_commands.push_back(undo_cmd);
        }
    }

    void CommandRouter::create_group_command()
    {
        auto& router = instance();
        const auto command = std::make_shared<GroupCommand>(router.m_commands);
        command->m_command_name = router.m_block_name;
        clear();
        CommandInterface::finalize(command, CommandArgs());
    }

    void CommandRouter::transfer_commands(std::vector<std::shared_ptr<UndoCommand>>& commands)
    {
        auto& router = instance();

        for (auto& command : router.m_commands)
        {
            commands.push_back(command);
        }

        clear();
    }

    bool CommandRouter::lock_execute()
    {
        return instance().m_depth > 0;
    }

    void CommandRouter::clear()
    {
        auto& router = instance();

        router.m_block_name.clear();
        router.m_commands.clear();
    }

}

OPENDCC_NAMESPACE_CLOSE
