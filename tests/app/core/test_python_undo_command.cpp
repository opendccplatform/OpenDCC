/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/app/core/undo/stack.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandSyntax;
using base::commands::CommandRegistry;
using base::commands::CommandResult;
using base::commands::UndoCommand;
using base::commands::CommandArgs;
using base::commands::CommandInterface;

namespace
{
    namespace py = pybind11;

    struct PythonUndoFixture
    {
        py::gil_scoped_acquire gil;
        py::module_ cmds = py::module_::import("opendcc.cmds");
        py::dict scope;
        commands::UndoStack stack;
        const std::string name = "test_python_set_value";

        PythonUndoFixture()
        {
            scope["cmds"] = cmds;
            py::exec(R"(
import weakref
state = {'value': 0}
events = []
instances = []

class SetValue(cmds.UndoCommand):
    def __init__(self):
        super().__init__()
        instances.append(weakref.ref(self))

    def execute(self, value, *, offset=0):
        self.before = state['value']
        self.after = value + offset
        state['value'] = self.after
        events.append('execute')
        return cmds.CommandResult(cmds.SUCCESS)

    def undo(self):
        state['value'] = self.before
        events.append('undo')

    def redo(self):
        state['value'] = self.after
        events.append('redo')

cmds.Registry.register_command('test_python_set_value', None, ('int',), {'offset': 'int'}, SetValue)
)",
                     scope);
        }

        ~PythonUndoFixture()
        {
            stack.clear();
            CommandSyntax syntax;
            if (CommandRegistry::get_command_syntax(name, syntax))
                CommandRegistry::unregister_command(name);
        }

        int value() const { return scope["state"].cast<py::dict>()["value"].cast<int>(); }
        std::vector<std::string> events() const { return scope["events"].cast<std::vector<std::string>>(); }
        py::object instance_ref() const { return scope["instances"].cast<py::list>()[0].cast<py::object>(); }

        CommandResult execute(const std::shared_ptr<UndoCommand>& command, const CommandArgs& args)
        {
            // Record in the isolated stack instead of the application's global history.
            const auto result = CommandInterface::execute(command, args, false);
            if (result.is_successful())
                stack.push(command);
            return result;
        }
    };
}

DOCTEST_TEST_SUITE("PythonUndoCommand")
{
    DOCTEST_TEST_CASE_FIXTURE(PythonUndoFixture, "cpp_undo_redo_keeps_python_command_alive_in_history")
    {
        std::shared_ptr<UndoCommand> command;
        {
            py::gil_scoped_release release;
            command = CommandRegistry::create_command<UndoCommand>(name);
        }
        DOCTEST_REQUIRE(command);
        DOCTEST_CHECK(command->get_command_name() == name);
        DOCTEST_CHECK(command->get_syntax().get_arg_descriptors().size() == 1);
        const auto weak_command = instance_ref();
        DOCTEST_CHECK_FALSE(weak_command().is_none());
        DOCTEST_REQUIRE(execute(command, CommandArgs().arg(10).kwarg("offset", 2)).is_successful());
        DOCTEST_CHECK(value() == 12);
        DOCTEST_CHECK(events() == std::vector<std::string> { "execute" });
        DOCTEST_CHECK(stack.get_size() == 1);
        DOCTEST_CHECK(stack.can_undo());
        DOCTEST_CHECK_FALSE(stack.can_redo());

        CommandRegistry::unregister_command(name);
        DOCTEST_CHECK_FALSE(py::hasattr(cmds, name.c_str()));
        command.reset();
        DOCTEST_CHECK_FALSE(weak_command().is_none());
        const int undo_redo_cycles = 2;
        for (int cycle = 0; cycle < undo_redo_cycles; ++cycle)
        {
            stack.undo();
            DOCTEST_CHECK(value() == 0);
            DOCTEST_CHECK_FALSE(stack.can_undo());
            DOCTEST_CHECK(stack.can_redo());
            stack.redo();
            DOCTEST_CHECK(value() == 12);
            DOCTEST_CHECK(stack.can_undo());
            DOCTEST_CHECK_FALSE(stack.can_redo());
            DOCTEST_CHECK(stack.get_size() == 1);
        }
        DOCTEST_CHECK(events() == std::vector<std::string> { "execute", "undo", "redo", "undo", "redo" });
        {
            py::gil_scoped_release release;
            stack.clear();
        }
        DOCTEST_CHECK(weak_command().is_none());
        DOCTEST_CHECK(stack.get_size() == 0);
    }

    DOCTEST_TEST_CASE_FIXTURE(PythonUndoFixture, "clearing_history_preserves_a_command_retained_by_cpp")
    {
        auto command = CommandRegistry::create_command<UndoCommand>(name);
        DOCTEST_REQUIRE(command);
        const auto weak_command = instance_ref();
        DOCTEST_REQUIRE(execute(command, CommandArgs().arg(7)).is_successful());
        DOCTEST_CHECK(value() == 7);
        stack.clear();
        DOCTEST_CHECK_FALSE(weak_command().is_none());
        command->undo();
        DOCTEST_CHECK(value() == 0);
        command->redo();
        DOCTEST_CHECK(value() == 7);
        DOCTEST_CHECK(events() == std::vector<std::string> { "execute", "undo", "redo" });
        CommandRegistry::unregister_command(name);
        {
            py::gil_scoped_release release;
            command.reset();
        }
        DOCTEST_CHECK(weak_command().is_none());
    }
}

OPENDCC_NAMESPACE_CLOSE
