/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"

#include <functional>

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    class CommandInterface;

    class OPENDCC_BASE_COMMANDS_API CommandRegistry
    {
    public:
        using ConversionFn = std::function<std::shared_ptr<CommandArgBase>(const std::shared_ptr<CommandArgBase>&)>;
        using FactoryFn = std::function<std::shared_ptr<Command>()>;

        static void register_command(const std::string& name, const CommandSyntax& syntax, FactoryFn factory_fn);
        static void unregister_command(const std::string& name);
        static std::shared_ptr<Command> create_command(const std::string& name);
        template <class T>
        static std::shared_ptr<T> create_command(const std::string& name)
        {
            static_assert(std::is_base_of<Command, T>::value, "Template argument is not derived from Command class.");
            return std::dynamic_pointer_cast<T>(create_command(name));
        }
        static bool get_command_syntax(const std::string& name, CommandSyntax& result);
        static bool is_convertible(const std::type_index& from, const std::type_index& to);

        template <class TFrom, class TTo>
        static void register_conversion(ConversionFn conversion_fn)
        {
            register_conversion_impl(typeid(std::decay_t<TFrom>), typeid(std::decay_t<TTo>), conversion_fn);
        }
        static void register_command_interface(CommandInterface& interface);
        static void unregister_command_interface(CommandInterface& interface);

    private:
        friend class CommandInterface;
        static void command_executed(const std::shared_ptr<Command>& cmd, const CommandArgs& args, const CommandResult& result);

        CommandRegistry();
        static CommandRegistry& instance();
        static void register_conversion_impl(const std::type_index& from, const std::type_index& to, const ConversionFn& conversion_fn);

        struct CommandDescriptor
        {
            FactoryFn factory_fn;
            CommandSyntax syntax;
        };

        std::unordered_map<std::string, CommandDescriptor> m_command_registry;
        std::vector<std::reference_wrapper<CommandInterface>> m_command_APIs;
    };

}

OPENDCC_NAMESPACE_CLOSE
