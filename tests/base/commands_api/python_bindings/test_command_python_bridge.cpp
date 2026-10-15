/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

using base::commands::Command;
using base::commands::CommandResult;
using base::commands::CommandArgs;
using base::commands::CommandSyntax;
using base::commands::CommandRegistry;
using base::commands::CommandInterface;

namespace
{
    namespace py = pybind11;

    class RepeatCommand : public Command
    {
    public:
        explicit RepeatCommand(int& calls)
            : m_calls(calls)
        {
        }

        CommandResult execute(const CommandArgs& args) override
        {
            ++m_calls;
            const auto text = args.get_arg<std::string>(0)->get_value();
            if (text.empty())
                return CommandResult(CommandResult::Status::FAIL);
            const auto repeat_arg = args.get_kwarg<int>("repeat");
            const int repeat = repeat_arg ? repeat_arg->get_value() : 1;
            std::string result;
            for (int i = 0; i < repeat; ++i)
                result += text;
            return CommandResult(CommandResult::Status::SUCCESS, result);
        }

    private:
        int& m_calls;
    };

    struct PythonBridgeFixture
    {
        py::gil_scoped_acquire gil;
        py::module_ cmds = py::module_::import("opendcc.cmds");
        py::dict scope;
        int native_calls = 0;
        const std::string native_name = "test_native_repeat";
        const std::string python_name = "test_python_echo";

        PythonBridgeFixture() { scope["cmds"] = cmds; }
        ~PythonBridgeFixture()
        {
            for (const auto& name : { native_name, python_name })
            {
                CommandSyntax syntax;
                if (CommandRegistry::get_command_syntax(name, syntax))
                    CommandRegistry::unregister_command(name);
            }
        }

        void register_native()
        {
            CommandRegistry::register_command(native_name, CommandSyntax().arg<std::string>("text").kwarg<int>("repeat").result<std::string>(),
                                              [this] { return std::make_shared<RepeatCommand>(native_calls); });
        }

        void register_python()
        {
            py::exec(R"(
import weakref
calls = []
instances = []
class Echo(cmds.Command):
    def __init__(self):
        super().__init__()
        instances.append(weakref.ref(self))

    def execute(self, text, enabled, amount, *, suffix=''):
        calls.append((text, enabled, amount, suffix))
        if not enabled:
            return cmds.CommandResult(cmds.FAIL)
        return cmds.CommandResult(cmds.SUCCESS, text + suffix)

cmds.Registry.register_command('test_python_echo', 'str', ('str', 'bool', 'double'),
                               {'suffix': 'str'}, Echo)
)",
                     scope);
        }
    };
}

DOCTEST_TEST_SUITE("CommandPythonBridge")
{
    DOCTEST_TEST_CASE_FIXTURE(PythonBridgeFixture, "native_registration_creates_python_callable_and_converts_arguments_and_result")
    {
        register_native();
        DOCTEST_REQUIRE(py::hasattr(cmds, native_name.c_str()));
        const auto function = cmds.attr(native_name.c_str());
        const auto repeated = function("hello", py::arg("repeat") = 2);
        DOCTEST_CHECK(repeated.attr("is_successful")().cast<bool>());
        DOCTEST_CHECK(repeated.attr("get_result")().cast<std::string>() == "hellohello");
        DOCTEST_CHECK(function("hello").attr("get_result")().cast<std::string>() == "hello");
        const auto failure = function("");
        DOCTEST_CHECK(failure.attr("get_status")().equal(cmds.attr("FAIL")));
        DOCTEST_CHECK(failure.attr("get_result")().is_none());
        DOCTEST_CHECK(native_calls == 3);
        CommandRegistry::unregister_command(native_name);
        DOCTEST_CHECK_FALSE(py::hasattr(cmds, native_name.c_str()));
        DOCTEST_CHECK(CommandRegistry::create_command(native_name) == nullptr);
    }

    DOCTEST_TEST_CASE_FIXTURE(PythonBridgeFixture, "python_registered_command_executes_from_cpp_with_instance_lifetime_preserved")
    {
        register_python();
        std::shared_ptr<Command> command;
        {
            py::gil_scoped_release release;
            command = CommandRegistry::create_command(python_name);
        }
        DOCTEST_REQUIRE(command);
        DOCTEST_REQUIRE(py::len(scope["instances"]) == 1);
        const auto instance_ref = scope["instances"].cast<py::list>()[0].cast<py::object>();
        DOCTEST_CHECK_FALSE(instance_ref().is_none());
        const auto args = CommandArgs().arg("hello").arg(true).arg(1.5).kwarg("suffix", " world");
        const auto result = CommandInterface::execute(command, args);
        DOCTEST_REQUIRE(result.is_successful());
        // Python commands return their Python object through CommandResult.
        DOCTEST_REQUIRE(result.get_result<py::object>());
        DOCTEST_CHECK(result.get_result<py::object>()->get_value().cast<std::string>() == "hello world");
        DOCTEST_CHECK(py::len(scope["calls"]) == 1);
        const auto call = scope["calls"].cast<py::list>()[0].cast<py::tuple>();
        DOCTEST_CHECK(call[0].cast<std::string>() == "hello");
        DOCTEST_CHECK(call[1].cast<bool>());
        DOCTEST_CHECK(call[2].cast<double>() == 1.5);
        DOCTEST_CHECK(call[3].cast<std::string>() == " world");
        const auto failure = CommandInterface::execute(python_name, CommandArgs().arg("hello").arg(false).arg(1.5));
        DOCTEST_CHECK(failure.get_status() == CommandResult::Status::FAIL);
        DOCTEST_CHECK(py::len(scope["calls"]) == 2);
        CommandRegistry::unregister_command(python_name);
        DOCTEST_CHECK_FALSE(py::hasattr(cmds, python_name.c_str()));
        DOCTEST_CHECK(CommandRegistry::create_command(python_name) == nullptr);
        DOCTEST_CHECK_FALSE(instance_ref().is_none());
        {
            py::gil_scoped_release release;
            command.reset();
        }
        DOCTEST_CHECK(instance_ref().is_none());
    }

    DOCTEST_TEST_CASE_FIXTURE(PythonBridgeFixture, "cpp_rejects_invalid_python_command_arguments_before_execution")
    {
        register_python();
        DOCTEST_CHECK(CommandInterface::execute(python_name).get_status() == CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(CommandInterface::execute(python_name, CommandArgs().arg(1).arg(true).arg(1.5)).get_status() ==
                      CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(CommandInterface::execute(python_name, CommandArgs().arg("hello").arg(true).arg(1.5).kwarg("unknown", "value")).get_status() ==
                      CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(py::len(scope["calls"]) == 0);
    }

    DOCTEST_TEST_CASE_FIXTURE(PythonBridgeFixture, "python_rejects_invalid_native_command_arguments_before_execution")
    {
        register_native();
        const auto function = cmds.attr(native_name.c_str());
        DOCTEST_CHECK_THROWS_AS(function(), py::error_already_set);
        DOCTEST_CHECK_THROWS_AS(function(py::dict()), py::error_already_set);
        DOCTEST_CHECK_THROWS_AS(function("hello", py::arg("unknown") = 1), py::error_already_set);
        DOCTEST_CHECK_THROWS_AS(function("hello", py::arg("repeat") = py::dict()), py::error_already_set);
        DOCTEST_CHECK(native_calls == 0);
    }
}

OPENDCC_NAMESPACE_CLOSE
