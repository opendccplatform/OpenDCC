/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include <unordered_map>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

using base::commands::Command;
using base::commands::CommandResult;
using base::commands::CommandArgs;
using base::commands::CommandInterface;
using base::commands::CommandSyntax;
using base::commands::CommandRegistry;
using base::commands::TypeIndices;

namespace
{
    class EchoCommand : public Command
    {
    public:
        CommandResult execute(const CommandArgs& args) override
        {
            return CommandResult(CommandResult::Status::SUCCESS, args.get_arg<std::string>(0)->get_value());
        }
    };

    class RecordingInterface : public CommandInterface
    {
    public:
        void register_command(const std::string& name, const CommandSyntax& syntax) override { registered[name] = syntax; }
        void unregister_command(const std::string& name) override { registered.erase(name); }
        void on_command_execute(const std::shared_ptr<Command>& command, const CommandArgs&, const CommandResult& result) override
        {
            executed.push_back(command->get_command_name());
            last_status = result.get_status();
        }

        std::unordered_map<std::string, CommandSyntax> registered;
        std::vector<std::string> executed;
        CommandResult::Status last_status = CommandResult::Status::FAIL;
    };

    struct RegistryFixture
    {
        const std::string name = "test_registry_echo";
        RecordingInterface observer;

        RegistryFixture() { CommandRegistry::register_command_interface(observer); }
        ~RegistryFixture()
        {
            CommandRegistry::unregister_command_interface(observer);
            CommandSyntax syntax;
            if (CommandRegistry::get_command_syntax(name, syntax))
                CommandRegistry::unregister_command(name);
        }

        void register_echo()
        {
            CommandRegistry::register_command(name, CommandSyntax().arg<std::string>("text").result<std::string>(),
                                              [] { return std::make_shared<EchoCommand>(); });
        }
    };
}

DOCTEST_TEST_SUITE("CommandRegistry")
{
    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "registration_exposes_syntax_and_creates_distinct_commands")
    {
        register_echo();
        CommandSyntax syntax;
        DOCTEST_REQUIRE(CommandRegistry::get_command_syntax(name, syntax));
        DOCTEST_CHECK(syntax.get_arg_descriptor(0).name == "text");
        DOCTEST_CHECK(syntax.get_arg_descriptor(0).type_indices == TypeIndices { typeid(std::string) });
        DOCTEST_CHECK(syntax.get_result_descriptor().type_indices == TypeIndices { typeid(std::string) });
        DOCTEST_CHECK(observer.registered.count(name) == 1);
        const auto first = CommandRegistry::create_command(name);
        const auto second = CommandRegistry::create_command(name);
        DOCTEST_REQUIRE(first);
        DOCTEST_REQUIRE(second);
        DOCTEST_CHECK(first != second);
        DOCTEST_CHECK(first->get_command_name() == name);
        DOCTEST_CHECK(first->get_syntax().has_arg("text"));
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "late_interface_receives_existing_commands_and_detaches")
    {
        CommandRegistry::unregister_command_interface(observer);
        observer.registered.clear();
        register_echo();
        DOCTEST_CHECK(observer.registered.count(name) == 0);
        CommandRegistry::register_command_interface(observer);
        DOCTEST_REQUIRE(observer.registered.count(name) == 1);
        DOCTEST_CHECK(observer.registered.at(name).has_arg("text"));
        CommandRegistry::unregister_command_interface(observer);
        CommandRegistry::unregister_command(name);
        DOCTEST_CHECK(observer.registered.count(name) == 1);
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "unregistration_removes_factory_syntax_and_interface_command")
    {
        register_echo();
        CommandRegistry::unregister_command(name);
        CommandSyntax syntax = CommandSyntax().arg<int>("old_argument");
        DOCTEST_CHECK_FALSE(CommandRegistry::get_command_syntax(name, syntax));
        DOCTEST_CHECK(syntax.get_arg_descriptors().empty());
        DOCTEST_CHECK(CommandRegistry::create_command(name) == nullptr);
        DOCTEST_CHECK(observer.registered.count(name) == 0);
        DOCTEST_CHECK(CommandInterface::execute(name).get_status() == CommandResult::Status::CMD_NOT_REGISTERED);
    }

    DOCTEST_TEST_CASE_FIXTURE(RegistryFixture, "successful_dispatch_notifies_interfaces_but_invalid_arguments_do_not")
    {
        register_echo();
        const auto result = CommandInterface::execute(name, CommandArgs().arg("hello"));
        DOCTEST_REQUIRE(result.is_successful());
        DOCTEST_REQUIRE(result.get_result<std::string>());
        DOCTEST_CHECK(result.get_result<std::string>()->get_value() == "hello");
        DOCTEST_CHECK(observer.executed == std::vector<std::string> { name });
        DOCTEST_CHECK(observer.last_status == CommandResult::Status::SUCCESS);
        DOCTEST_CHECK(CommandInterface::execute(name).get_status() == CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(CommandInterface::execute(name, CommandArgs().arg(1)).get_status() == CommandResult::Status::INVALID_SYNTAX);
        DOCTEST_CHECK(observer.executed.size() == 1);
    }
}

OPENDCC_NAMESPACE_CLOSE
