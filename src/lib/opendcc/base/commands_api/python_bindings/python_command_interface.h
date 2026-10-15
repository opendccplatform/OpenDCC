/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/python_bindings/api.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#include <opendcc/base/vendor/eventpp/eventdispatcher.h>

OPENDCC_NAMESPACE_OPEN

namespace base::commands::python
{

    class OPENDCC_BASE_PY_COMMANDS_API PythonCommandInterface : public CommandInterface
    {
    public:
        enum class EventType
        {
            COMMAND_EXECUTE
        };

        using ToPythonFn = std::function<pybind11::object(const std::shared_ptr<CommandArgBase>&)>;
        using FromPythonFn = std::function<std::shared_ptr<CommandArgBase>(const pybind11::object&)>;
        using CallbackFn = void(const std::shared_ptr<Command>&, const CommandArgs&, const CommandResult&);
        using EventDispatcher = eventpp::EventDispatcher<EventType, CallbackFn>;
        using EventDispatcherHandle = EventDispatcher::Handle;

        static pybind11::object to_python(const std::shared_ptr<CommandArgBase>& arg, const TypeIndices& arg_types);
        static std::shared_ptr<CommandArgBase> from_python(const pybind11::object& arg, const TypeIndices& arg_types);

        EventDispatcherHandle register_event_callback(const std::function<CallbackFn>& callback);
        void unregister_event_callback(const EventDispatcherHandle& handle);

        template <class T>
        void register_conversion(const std::string& type_name)
        {
            register_conversion_impl(
                type_name, typeid(T),
                [](const std::shared_ptr<CommandArgBase>& arg) {
                    if (auto downcasted = std::dynamic_pointer_cast<CommandArg<T>>(arg))
                    {
                        return pybind11::cast(downcasted->get_value());
                    }
                    else
                    {
                        return pybind11::object();
                    }
                },
                [](const pybind11::object& arg) -> std::shared_ptr<CommandArgBase> {
                    try
                    {
                        return std::make_shared<CommandArg<T>>(arg.cast<T>());
                    }
                    catch (const pybind11::cast_error& err)
                    {
                        return nullptr;
                    }
                });
        }

        static std::string generate_python_cmd_str(const std::shared_ptr<Command>& command, const CommandArgs& args);
        static std::string generate_python_result_str(const CommandResult& result);
        static std::string generate_help_str(const std::string& command_name, const CommandSyntax& syntax);
        static TypeIndices get_syntax_arg_types(const std::vector<std::string>& pytypes);
        static std::vector<std::string> get_syntax_arg_types(const TypeIndices& cpp_types);
        static PythonCommandInterface& instance();

        PythonCommandInterface(const PythonCommandInterface&) = delete;
        PythonCommandInterface(PythonCommandInterface&&) = delete;
        PythonCommandInterface& operator=(const PythonCommandInterface&) = delete;
        PythonCommandInterface& operator=(PythonCommandInterface&&) = delete;

    protected:
        void register_command(const std::string& name, const CommandSyntax& syntax) override;
        void unregister_command(const std::string& name) override;
        void on_command_execute(const std::shared_ptr<Command>& cmd, const CommandArgs& args, const CommandResult& result) override;

    private:
        PythonCommandInterface();
        ~PythonCommandInterface();

        void register_conversion_impl(const std::string& py_type_name, const std::type_index& cpp_type, ToPythonFn to_python_fn,
                                      FromPythonFn from_python_fn);

        struct Pimpl;
        std::unique_ptr<Pimpl> m_pimpl;
    };
}

OPENDCC_NAMESPACE_CLOSE
