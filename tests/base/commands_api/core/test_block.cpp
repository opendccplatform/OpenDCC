// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/block.h"
#include "opendcc/base/commands_api/core/router.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

DOCTEST_TEST_SUITE("CommandBlock/UndoCommandBlock")
{
    class TestCommand : public UndoCommand
    {
    public:
        TestCommand() {}
        virtual ~TestCommand() = default;

        virtual CommandResult execute(const CommandArgs& args) override { return CommandResult(CommandResult::Status::CMD_NOT_REGISTERED); }

        virtual void undo() override {}

        virtual void redo() override {}
    };

    class TestCommandInterface : public CommandInterface
    {
    public:
        virtual void register_command(const std::string& name, const CommandSyntax& syntax) {}

        virtual void unregister_command(const std::string& name) {}

        virtual void on_command_execute(const std::shared_ptr<Command>& cmd, const CommandArgs& args, const CommandResult& result)
        {
            m_commands.push_back(cmd);
        }

        std::vector<std::shared_ptr<Command>> m_commands;

        static TestCommandInterface& instance()
        {
            static TestCommandInterface test_command_interface;
            return test_command_interface;
        }

    private:
        TestCommandInterface() {}

        TestCommandInterface(const TestCommandInterface&) = delete;
        TestCommandInterface(TestCommandInterface&&) = delete;
        TestCommandInterface& operator=(const TestCommandInterface&) = delete;
        TestCommandInterface& operator=(TestCommandInterface&&) = delete;
    };

    class TestCommandInterfaceRegistrator
    {
    public:
        TestCommandInterfaceRegistrator() { CommandRegistry::register_command_interface(TestCommandInterface::instance()); }

        ~TestCommandInterfaceRegistrator() { CommandRegistry::unregister_command_interface(TestCommandInterface::instance()); }
    };

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "one CommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            CommandBlock block;
            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());

            std::vector<std::shared_ptr<UndoCommand>> router_commands;
            CommandRouter::transfer_commands(router_commands);
            DOCTEST_CHECK(router_commands.size() == 1);
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 1);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "more than one CommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            CommandBlock one_block;
            DOCTEST_CHECK(CommandRouter::lock_execute());

            {
                CommandBlock two_block;
                DOCTEST_CHECK(CommandRouter::lock_execute());

                auto block_command = std::make_shared<TestCommand>();
                CommandInterface::finalize(block_command, CommandArgs());
                DOCTEST_CHECK(test_command_interface.m_commands.empty());
            }

            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());

            std::vector<std::shared_ptr<UndoCommand>> router_commands;
            CommandRouter::transfer_commands(router_commands);
            DOCTEST_CHECK(router_commands.size() == 2);
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 1);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "empty UndoCommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            UndoCommandBlock block;
            DOCTEST_CHECK(CommandRouter::lock_execute());
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 1);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "one named UndoCommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        const std::string name = "TestUndoCommandBlock";

        {
            UndoCommandBlock block(name);
            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 2);

        DOCTEST_CHECK(test_command_interface.m_commands[0]->get_command_name() == name);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "more than one named UndoCommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        const std::string one_name = "OneTestUndoCommandBlock";
        const std::string two_name = "TwoTestUndoCommandBlock";

        {
            UndoCommandBlock one_block(one_name);
            DOCTEST_CHECK(CommandRouter::lock_execute());

            {
                UndoCommandBlock two_block(two_name);
                DOCTEST_CHECK(CommandRouter::lock_execute());

                auto block_command = std::make_shared<TestCommand>();
                CommandInterface::finalize(block_command, CommandArgs());
                DOCTEST_CHECK(test_command_interface.m_commands.empty());
            }

            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 2);

        DOCTEST_CHECK(test_command_interface.m_commands[0]->get_command_name() == one_name);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "one UndoCommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            UndoCommandBlock block;
            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 2);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "more than one UndoCommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            UndoCommandBlock one_block;
            DOCTEST_CHECK(CommandRouter::lock_execute());

            {
                UndoCommandBlock two_block;
                DOCTEST_CHECK(CommandRouter::lock_execute());

                auto block_command = std::make_shared<TestCommand>();
                CommandInterface::finalize(block_command, CommandArgs());
                DOCTEST_CHECK(test_command_interface.m_commands.empty());
            }

            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 2);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "mix CommandBlock/UndoCommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            CommandBlock command_block;
            DOCTEST_CHECK(CommandRouter::lock_execute());

            {
                UndoCommandBlock undo_command_block;
                DOCTEST_CHECK(CommandRouter::lock_execute());

                auto block_command = std::make_shared<TestCommand>();
                CommandInterface::finalize(block_command, CommandArgs());
                DOCTEST_CHECK(test_command_interface.m_commands.empty());
            }

            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());

            std::vector<std::shared_ptr<UndoCommand>> router_commands;
            CommandRouter::transfer_commands(router_commands);
            DOCTEST_CHECK(router_commands.size() == 2);
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 1);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }

    DOCTEST_TEST_CASE_FIXTURE(TestCommandInterfaceRegistrator, "mix UndoCommandBlock/CommandBlock")
    {
        auto& test_command_interface = TestCommandInterface::instance();
        test_command_interface.m_commands.clear();

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        {
            UndoCommandBlock undo_command_block;
            DOCTEST_CHECK(CommandRouter::lock_execute());

            {
                CommandBlock command_block;
                DOCTEST_CHECK(CommandRouter::lock_execute());

                auto block_command = std::make_shared<TestCommand>();
                CommandInterface::finalize(block_command, CommandArgs());
                DOCTEST_CHECK(test_command_interface.m_commands.empty());
            }

            DOCTEST_CHECK(CommandRouter::lock_execute());

            auto block_command = std::make_shared<TestCommand>();
            CommandInterface::finalize(block_command, CommandArgs());
            DOCTEST_CHECK(test_command_interface.m_commands.empty());
        }

        DOCTEST_CHECK(!CommandRouter::lock_execute());

        auto command = std::make_shared<TestCommand>();
        CommandInterface::finalize(command, CommandArgs());
        DOCTEST_CHECK(test_command_interface.m_commands.size() == 2);

        std::vector<std::shared_ptr<UndoCommand>> router_commands;
        CommandRouter::transfer_commands(router_commands);
        DOCTEST_CHECK(router_commands.empty());
    }
}

OPENDCC_NAMESPACE_CLOSE
