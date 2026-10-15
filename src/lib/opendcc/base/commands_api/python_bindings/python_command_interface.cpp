// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"
#include "opendcc/base/commands_api/python_bindings/python.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands::python
{

    struct PythonCommandInterface::Pimpl
    {
        std::unordered_map<std::string, std::type_index> pytype_to_cpp_type;
        std::unordered_map<std::type_index, std::string> cpptype_to_pytype;
        std::unordered_map<std::type_index, PythonCommandInterface::ToPythonFn> to_python_converters;
        std::unordered_map<std::type_index, PythonCommandInterface::FromPythonFn> from_python_converters;
        PythonCommandInterface::EventDispatcher dispatcher;
    };

    pybind11::object PythonCommandInterface::to_python(const std::shared_ptr<CommandArgBase>& arg, const TypeIndices& arg_types)
    {
        auto& pimpl = instance().m_pimpl;
        for (const auto& type : arg_types)
        {
            auto converter_fn = pimpl->to_python_converters.find(type);
            if (converter_fn != pimpl->to_python_converters.end())
            {
                const auto result = converter_fn->second(arg);
                if (!result.is_none())
                    return result;
            }
        }
        return pybind11::none();
    }

    std::shared_ptr<CommandArgBase> PythonCommandInterface::from_python(const pybind11::object& arg, const TypeIndices& arg_types)
    {
        auto& pimpl = instance().m_pimpl;
        for (const auto& type : arg_types)
        {
            auto converter_fn = pimpl->from_python_converters.find(type);
            if (converter_fn != pimpl->from_python_converters.end())
            {
                if (auto result = converter_fn->second(arg))
                    return result;
            }
        }
        return nullptr;
    }

    void PythonCommandInterface::register_command(const std::string& name, const CommandSyntax& syntax)
    {
        register_py_command(name, syntax);
    }

    void PythonCommandInterface::unregister_command(const std::string& name)
    {
        unregister_py_command(name);
    }

    void PythonCommandInterface::on_command_execute(const std::shared_ptr<Command>& cmd, const CommandArgs& args, const CommandResult& result)
    {
        instance().m_pimpl->dispatcher.dispatch(EventType::COMMAND_EXECUTE, cmd, args, result);
    }

    PythonCommandInterface::EventDispatcherHandle PythonCommandInterface::register_event_callback(const std::function<CallbackFn>& callback)
    {
        return instance().m_pimpl->dispatcher.appendListener(EventType::COMMAND_EXECUTE, callback);
    }

    void PythonCommandInterface::unregister_event_callback(const EventDispatcherHandle& handle)
    {
        instance().m_pimpl->dispatcher.removeListener(EventType::COMMAND_EXECUTE, handle);
    }

    std::string PythonCommandInterface::generate_python_cmd_str(const std::shared_ptr<Command>& command, const CommandArgs& args)
    {
        return python::generate_python_cmd_str(command->get_command_name(), args);
    }

    std::string PythonCommandInterface::generate_python_result_str(const CommandResult& result)
    {
        return python::generate_python_result_str(result);
    }

    std::string PythonCommandInterface::generate_help_str(const std::string& command_name, const CommandSyntax& syntax)
    {
        return python::generate_python_help_str(command_name, syntax);
    }

    TypeIndices PythonCommandInterface::get_syntax_arg_types(const std::vector<std::string>& pytypes)
    {
        std::type_index null_typeid = typeid(nullptr);
        TypeIndices result;
        auto& pimpl = instance().m_pimpl;
        for (const auto& type : pytypes)
        {
            auto iter = pimpl->pytype_to_cpp_type.find(type);
            if (iter != pimpl->pytype_to_cpp_type.end())
                result.emplace_back(iter->second);
            else
                result.emplace_back(null_typeid);
        }
        return result;
    }

    std::vector<std::string> PythonCommandInterface::get_syntax_arg_types(const TypeIndices& cpp_types)
    {
        std::vector<std::string> result;
        auto& pimpl = instance().m_pimpl;
        for (const auto& type : cpp_types)
        {
            auto iter = pimpl->cpptype_to_pytype.find(type);
            if (iter != pimpl->cpptype_to_pytype.end())
                result.emplace_back(iter->second);
            else
                result.emplace_back("");
        }
        return result;
    }

    PythonCommandInterface& PythonCommandInterface::instance()
    {
        static PythonCommandInterface _instance;
        return _instance;
    }

    void PythonCommandInterface::register_conversion_impl(const std::string& py_type_name, const std::type_index& cpp_type, ToPythonFn to_python_fn,
                                                          FromPythonFn from_python_fn)
    {
        m_pimpl->to_python_converters[cpp_type] = to_python_fn;
        m_pimpl->from_python_converters[cpp_type] = from_python_fn;

        auto py_to_cpp_iter = m_pimpl->pytype_to_cpp_type.find(py_type_name);
        if (py_to_cpp_iter == m_pimpl->pytype_to_cpp_type.end())
            m_pimpl->pytype_to_cpp_type.emplace(py_type_name, cpp_type);
        else
            py_to_cpp_iter->second = cpp_type;

        auto cpp_to_py_iter = m_pimpl->cpptype_to_pytype.find(cpp_type);
        if (cpp_to_py_iter == m_pimpl->cpptype_to_pytype.end())
            m_pimpl->cpptype_to_pytype.emplace(cpp_type, py_type_name);
        else
            cpp_to_py_iter->second = py_type_name;
    }

    PythonCommandInterface::PythonCommandInterface()
    {
        m_pimpl = std::make_unique<PythonCommandInterface::Pimpl>();
        register_conversion<bool>("bool");
        register_conversion<int>("int");
        register_conversion<int64_t>("long");
        register_conversion<float>("float");
        register_conversion<double>("double");
        register_conversion<std::string>("str");
        register_conversion<std::string>("string");
    }
    PythonCommandInterface::~PythonCommandInterface() {}
}

OPENDCC_NAMESPACE_CLOSE
