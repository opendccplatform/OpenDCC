/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/app/core/undo/stack.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/command_registry.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

using base::commands::UndoCommand;
using base::commands::CommandResult;
using base::commands::CommandArgs;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandInterface;

namespace
{
    class SetValueCommand : public UndoCommand
    {
    public:
        SetValueCommand(int& value, int next, bool merge = false, const std::string& name = "test_set_value")
            : m_value(value)
            , m_before(value)
            , m_after(next)
            , m_merge(merge)
        {
            m_command_name = name;
        }

        CommandResult execute(const CommandArgs& args) override
        {
            const auto next = *args.get_arg<int>(0);
            if (next < 0)
                return CommandResult(CommandResult::Status::FAIL);
            m_after = next;
            redo();
            return CommandResult(CommandResult::Status::SUCCESS);
        }

        void undo() override { m_value = m_before; }
        void redo() override { m_value = m_after; }
        bool merge_with(UndoCommand* command) override
        {
            const auto other = dynamic_cast<SetValueCommand*>(command);
            if (!m_merge || !other || !other->m_merge)
                return false;
            m_after = other->m_after;
            return true;
        }

    private:
        int& m_value;
        int m_before;
        int m_after;
        bool m_merge;
    };

    struct StackFixture
    {
        int value = 0;
        commands::UndoStack stack;

        void push(int next, bool merge = false, const std::string& name = "test_set_value")
        {
            stack.push(std::make_shared<SetValueCommand>(value, next, merge, name), true);
        }
    };

    struct DispatchFixture : StackFixture
    {
        DispatchFixture()
        {
            CommandRegistry::register_command("test_set_value", CommandSyntax().arg<int>("value"),
                                              [this] { return std::make_shared<SetValueCommand>(value, 0); });
        }
        ~DispatchFixture() { CommandRegistry::unregister_command("test_set_value"); }
    };
}

DOCTEST_TEST_SUITE("UndoStack")
{
    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "empty_operations_and_null_push")
    {
        stack.undo();
        stack.redo();
        stack.push(nullptr, true);
        DOCTEST_CHECK(stack.get_size() == 0);
        DOCTEST_CHECK_FALSE(stack.can_undo());
        DOCTEST_CHECK_FALSE(stack.can_redo());
        DOCTEST_CHECK(value == 0);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "recording_does_not_execute")
    {
        auto command = std::make_shared<SetValueCommand>(value, 1);
        command->redo();
        value = 2;
        stack.push(command);
        DOCTEST_CHECK(value == 2);
        DOCTEST_CHECK(stack.get_size() == 1);
        stack.undo();
        DOCTEST_CHECK(value == 0);
        stack.redo();
        DOCTEST_CHECK(value == 1);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "undo_redo_order_and_boundaries")
    {
        push(1);
        push(2);
        DOCTEST_CHECK(stack.get_size() == 2);
        DOCTEST_CHECK_FALSE(stack.can_redo());
        const int undo_redo_cycles = 3;
        for (int cycle = 0; cycle != undo_redo_cycles; ++cycle)
        {
            stack.undo();
            DOCTEST_CHECK(value == 1);
            stack.undo();
            stack.undo();
            DOCTEST_CHECK(value == 0);
            DOCTEST_CHECK_FALSE(stack.can_undo());
            stack.redo();
            DOCTEST_CHECK(value == 1);
            stack.redo();
            stack.redo();
            DOCTEST_CHECK(value == 2);
            DOCTEST_CHECK_FALSE(stack.can_redo());
            DOCTEST_CHECK(stack.get_size() == 2);
        }
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "clear_and_disable_discard_history")
    {
        push(1);
        stack.undo();
        stack.clear();
        DOCTEST_CHECK(stack.get_size() == 0);
        DOCTEST_CHECK_FALSE(stack.can_redo());
        push(2);
        stack.set_enabled(false);
        DOCTEST_CHECK_FALSE(stack.is_enabled());
        DOCTEST_CHECK(stack.get_size() == 0);
        push(3);
        DOCTEST_CHECK(value == 3);
        DOCTEST_CHECK_FALSE(stack.can_undo());
        stack.set_enabled(true);
        push(4);
        stack.undo();
        DOCTEST_CHECK(value == 3);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "finite_limit_evicts_oldest_entries")
    {
        stack.set_undo_limit(2);
        push(1);
        push(2);
        push(3);
        DOCTEST_CHECK(stack.get_size() == 2);
        stack.undo();
        stack.undo();
        DOCTEST_CHECK(value == 1);
        DOCTEST_CHECK_FALSE(stack.can_undo());
        stack.redo();
        stack.redo();
        DOCTEST_CHECK(value == 3);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "shrinking_limit_preserves_history_cursor")
    {
        push(1);
        push(2);
        push(3);
        stack.undo();
        stack.set_undo_limit(1);
        DOCTEST_CHECK(stack.get_size() == 1);
        DOCTEST_CHECK_FALSE(stack.can_undo());
        DOCTEST_CHECK(stack.can_redo());
        stack.redo();
        DOCTEST_CHECK(value == 3);
        stack.undo();
        DOCTEST_CHECK(value == 2);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "zero_limit_is_unlimited")
    {
        const auto command_count = stack.get_undo_limit() + 1;
        stack.set_undo_limit(0);
        for (size_t next = 1; next <= command_count; ++next)
            push(static_cast<int>(next));
        DOCTEST_CHECK(stack.get_undo_limit() == 0);
        DOCTEST_CHECK(stack.get_size() == command_count);
        while (stack.can_undo())
            stack.undo();
        DOCTEST_CHECK(value == 0);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "accepted_and_rejected_merges")
    {
        push(1, true);
        push(2, true);
        DOCTEST_CHECK(stack.get_size() == 1);
        stack.undo();
        DOCTEST_CHECK(value == 0);
        stack.redo();
        DOCTEST_CHECK(value == 2);
        push(3, false);
        push(4, true, "different_command");
        DOCTEST_CHECK(stack.get_size() == 3);
        stack.undo();
        DOCTEST_CHECK(value == 3);
        stack.undo();
        DOCTEST_CHECK(value == 2);
    }

    DOCTEST_TEST_CASE_FIXTURE(StackFixture, "new_command_discards_redo_history")
    {
        push(1);
        push(2);
        stack.undo();
        push(3);
        DOCTEST_CHECK(stack.get_size() == 2);
        DOCTEST_CHECK_FALSE(stack.can_redo());
        stack.undo();
        DOCTEST_CHECK(value == 1);
        stack.redo();
        DOCTEST_CHECK(value == 3);
    }

    DOCTEST_TEST_CASE_FIXTURE(DispatchFixture, "dispatch_records_only_successful_undo_enabled_commands")
    {
        const auto success = CommandInterface::execute("test_set_value", CommandArgs().arg(1));
        DOCTEST_CHECK(success.is_successful());
        DOCTEST_CHECK(value == 1);
        DOCTEST_CHECK(stack.get_size() == 1);
        DOCTEST_CHECK(CommandInterface::execute("test_set_value").get_status() == CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(CommandInterface::execute("test_set_value", CommandArgs().arg(std::string("bad"))).get_status() ==
                      CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(CommandInterface::execute("test_set_value", CommandArgs().arg(-1)).get_status() == CommandResult::Status::FAIL);
        DOCTEST_CHECK(CommandInterface::execute("test_missing_command").get_status() == CommandResult::Status::CMD_NOT_REGISTERED);
        DOCTEST_CHECK(value == 1);
        DOCTEST_CHECK(stack.get_size() == 1);
        DOCTEST_CHECK(CommandInterface::execute("test_set_value", CommandArgs().arg(2), false).is_successful());
        DOCTEST_CHECK(value == 2);
        DOCTEST_CHECK(stack.get_size() == 1);
        stack.undo();
        DOCTEST_CHECK(value == 0);
        stack.redo();
        DOCTEST_CHECK(value == 1);
    }
}

OPENDCC_NAMESPACE_CLOSE
