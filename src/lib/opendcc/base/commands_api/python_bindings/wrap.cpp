// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/python_bindings/utils.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/block.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands::python
{

    using namespace pybind11;

    namespace
    {
        struct CommandWrap : Command
        {
            using Command::Command;

            static object raw_execute(tuple args, dict kwargs) { return pybind11::bool_(true); }

            virtual CommandResult execute(const CommandArgs& args) override
            {
                pybind11::gil_scoped_acquire lock;

                tuple pyargs;
                dict pykwargs;
                auto cmd_status = convert_args(this->get_command_name(), args, pyargs, pykwargs);
                if (cmd_status != CommandResult::Status::SUCCESS)
                    return CommandResult(cmd_status);

                OPENDCC_PYBIND11_OVERRIDE_PURE_EXCEPTION_SAFE(CommandResult, Command, execute, *pyargs, **pykwargs);

                // FOR SOME STRANGE REASON the line below fails with error:
                // No to_python (by-value) converter found for C++ type: class boost::python::detail::kwds_proxy
                // As a workaround we manually resolve virtual method call.
                // return call_method<bool>(m_self, "execute", *pyargs, **pykwargs);

                // auto self = object(handle<>(m_self));
                // const auto result = extract<CommandResult>(self.attr("execute")(*pyargs, **pykwargs))();

                // if (auto result_obj = result.get_result<object>())
                //     return CommandResult(result.get_status(), PythonCommandInterface::from_python(
                //                                                   *result.get_result<object>(),
                //                                                   this->get_syntax().get_result_descriptor().type_indices));
                // return CommandResult(result.get_status());
            }
        };

        class PythonUndoCommandBlock
        {
        public:
            explicit PythonUndoCommandBlock()
                : _block(nullptr)
            {
            }

            void Open() { _block = new UndoCommandBlock(m_block_name); }

            void Close(object, object, object) { Close(); }

            void Close()
            {
                delete _block;
                _block = nullptr;
            }

            ~PythonUndoCommandBlock() { delete _block; }

        private:
            friend PythonUndoCommandBlock* python_undo_command_block_constructor(const std::string& block_name);

            UndoCommandBlock* _block = nullptr;
            std::string m_block_name;
        };

        PythonUndoCommandBlock* python_undo_command_block_constructor(const std::string& block_name)
        {
            auto block = new PythonUndoCommandBlock;
            block->m_block_name = block_name;
            return block;
        }

        struct UndoCommandWrap : UndoCommand
        {
            using UndoCommand::UndoCommand;

            virtual CommandResult execute(const CommandArgs& args) override
            {
                gil_scoped_acquire gil;
                tuple pyargs;
                dict pykwargs;
                const auto status = convert_args(get_command_name(), args, pyargs, pykwargs);
                if (status != CommandResult::Status::SUCCESS)
                    return CommandResult(status);
                OPENDCC_PYBIND11_OVERRIDE_PURE_EXCEPTION_SAFE(CommandResult, UndoCommand, execute, *pyargs, **pykwargs);
            }

            virtual void undo() override { OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, UndoCommand, undo); }

            virtual void redo() override { OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, UndoCommand, redo); }

            virtual bool merge_with(UndoCommand* command) override
            {
                OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(bool, UndoCommand, merge_with, command);
            }
        };

        void wrap_register_py_command(std::string name, object result_type, tuple args, dict kwargs,
                                      std::function<std::shared_ptr<Command>()> factory_fn)
        {
            CommandSyntax syntax;
            for (int i = 0; i < len(args); i++)
            {
                const auto type_name = args[i].cast<std::string>(); // extract<std::string>(args[i])();
                auto syntax_arg_types = PythonCommandInterface::get_syntax_arg_types({ type_name });
                TypeIndices validated_arg_types;
                for (const auto& type : syntax_arg_types)
                {
                    if (type != typeid(nullptr))
                    {
                        validated_arg_types.emplace_back(type);
                    }
                    else
                    {
                        PyErr_SetString(PyExc_TypeError, ("The C++ converters for type '" + type_name + "' is not registered.\n").c_str());
                        throw error_already_set();
                    }
                }
                // dummy for now
                // TODO: reconsider python command registration
                syntax.arg("arg" + std::to_string(i + 1), validated_arg_types);
            }

            for (const auto& [k, v] : kwargs)
            {
                const auto key = k.cast<std::string>();
                const auto value = v.cast<std::string>();

                auto syntax_arg_types = PythonCommandInterface::get_syntax_arg_types({ value });
                TypeIndices validated_arg_types;
                for (const auto& type : syntax_arg_types)
                {
                    if (type != typeid(nullptr))
                    {
                        validated_arg_types.emplace_back(type);
                    }
                    else
                    {
                        PyErr_SetString(PyExc_TypeError, ("The C++ converters for type '" + value + "' is not registered.\n").c_str());
                        throw error_already_set();
                    }
                }
                syntax.kwarg(key, validated_arg_types);
            }

            if (!result_type.is_none())
            {
                auto result_type_str = result_type.cast<std::string>();
                auto res_types = PythonCommandInterface::get_syntax_arg_types({ result_type_str });
                if (!res_types.empty() && res_types[0] != typeid(nullptr))
                {
                    syntax.result(res_types[0]);
                }
                else
                {
                    PyErr_SetString(PyExc_TypeError, ("The C++ converters for type '" + result_type_str + "' is not registered.\n").c_str());
                    throw error_already_set();
                }
            }
            CommandRegistry::register_command(name, syntax, factory_fn);
        }

        void wrap_unregister_py_command(const std::string& name)
        {
            CommandRegistry::unregister_command(name);
        }

        std::shared_ptr<Command> create_python_command(const std::function<object()>& factory_fn)
        {
            gil_scoped_acquire gil;
            auto instance = factory_fn();
            std::shared_ptr<Command> command;
            // UndoCommand virtually inherits Command; let C++ adjust the base pointer.
            if (isinstance<UndoCommand>(instance))
                command = instance.cast<std::shared_ptr<UndoCommand>>();
            else
                command = instance.cast<std::shared_ptr<Command>>();
            // Keep the Python subclass alive so C++ virtual calls can find its overrides.
            auto owner = std::shared_ptr<object>(new object(std::move(instance)), [](object* instance) {
                gil_scoped_acquire gil;
                delete instance;
            });
            return std::shared_ptr<Command>(owner, command.get());
        }

        CommandResult __execute(std::string name, tuple pyargs, dict pykwargs)
        {
            CommandSyntax syntax;
            if (!CommandRegistry::get_command_syntax(name, syntax))
            {
                PyErr_SetString(PyExc_TypeError, ("Command with name '" + name + "' is not registered.").c_str());
                throw error_already_set();
            }

            if (syntax.get_arg_descriptors().size() != len(pyargs))
            {
                PyErr_SetString(PyExc_TypeError, ("Unexpected argument count. Expected " + std::to_string(syntax.get_arg_descriptors().size()) +
                                                  ", got " + std::to_string(len(pyargs)) + ".\n")
                                                     .c_str());
                throw error_already_set();
            }

            auto command = CommandRegistry::create_command(name);

            // Convert Python args to C++ equivalents
            CommandArgs args;
            for (int i = 0; i < len(pyargs); i++)
            {
                if (auto arg = PythonCommandInterface::from_python(pyargs[i], syntax.get_arg_descriptor(i).type_indices))
                {
                    args.arg(arg);
                }
                else
                {
                    PyErr_SetString(PyExc_TypeError,
                                    ("The C++ from-python converter is not registered for arg at position '" + std::to_string(i) + "'.\n").c_str());
                    throw error_already_set();
                }
            }

            for (const auto& [k, v] : pykwargs)
            {
                auto key = k.cast<std::string>();

                auto kwarg_type = syntax.get_kwarg_descriptor(key);
                if (kwarg_type.is_valid())
                {
                    if (auto cpp_kwarg = PythonCommandInterface::from_python(reinterpret_borrow<object>(v), kwarg_type.type_indices))
                    {
                        args.kwarg(key, cpp_kwarg);
                    }
                    else
                    {
                        PyErr_SetString(PyExc_TypeError, ("The C++ from-python converter is not registered for kwarg '" + key + "'.\n").c_str());
                        throw error_already_set();
                    }
                }
                else
                {
                    PyErr_SetString(PyExc_TypeError, ("Unknown option \"" + key + "\".\n").c_str());
                    throw error_already_set();
                }
            }

            // In case if we call Python command from Python context
            // we don't have to convert all C++ args from CommandArgs again to their Python equivalents
            // instead we can just call the command directly
            if (auto py_command = std::dynamic_pointer_cast<CommandWrap>(command))
            {
                auto self = cast(py_command);
                const auto result = self.attr("execute")(*pyargs, **pykwargs).cast<CommandResult>();
                CommandInterface::finalize(command, args);
                return result;
            }
            const auto result = CommandInterface::execute(command, args);
            std::type_index return_type = result.get_type_info();
            return CommandResult(result.get_status(), PythonCommandInterface::to_python(result.get_result(), { return_type }));
        }

        CommandResult* cmd_result_constructor(CommandResult::Status status, object result)
        {
            return new CommandResult(status, result);
        }

        object cmd_result_get_result(const CommandResult& self)
        {
            return self.has_result() ? self.get_result<object>()->get_value() : object();
        }
    };

    PYBIND11_MODULE(PYMODULE_NAME, m)
    {
        enum_<CommandResult::Status>(m, "CommandStatus")
            .value("SUCCESS", CommandResult::Status::SUCCESS)
            .value("FAIL", CommandResult::Status::FAIL)
            .value("INVALID_SYNTAX", CommandResult::Status::INVALID_SYNTAX)
            .value("CMD_NOT_REGISTERED", CommandResult::Status::CMD_NOT_REGISTERED)
            .export_values();

        class_<CommandResult>(m, "CommandResult")
            .def(init([](CommandResult::Status status = CommandResult::Status::FAIL, object result = none()) {
                     return new CommandResult(status, result);
                 }),
                 arg("status") = CommandResult::Status::FAIL, arg("result") = none())
            .def("is_successful", &CommandResult::is_successful)
            .def("get_status", &CommandResult::get_status)
            .def("get_result", &cmd_result_get_result);

        m.def("__execute", &__execute);
        class_<CommandRegistry, std::unique_ptr<CommandRegistry, nodelete>>(m, "Registry")
            .def_static("register_command",
                        [](std::string name, object result_type, tuple args, dict kwargs, std::function<object()> factory_fn) {
                            const std::function<std::shared_ptr<Command>()> command_factory = [factory_fn] {
                                return create_python_command(factory_fn);
                            };
                            return wrap_register_py_command(std::move(name), result_type, args, kwargs,
                                                            pybind_bridge::pybind_safe_callback(command_factory));
                        })
            .def_static("unregister_command", &wrap_unregister_py_command);

        class_<PythonUndoCommandBlock>(m, "UndoCommandBlock")
            .def(init(&python_undo_command_block_constructor), arg("block_name") = "CommandBlock")
            .def("__enter__", &PythonUndoCommandBlock::Open)
            .def("__exit__", overload_cast<object, object, object>(&PythonUndoCommandBlock::Close))
            .def("enter", &PythonUndoCommandBlock::Open)
            .def("exit", overload_cast<>(&PythonUndoCommandBlock::Close))
            .def("exit", overload_cast<object, object, object>(&PythonUndoCommandBlock::Close));

        // TODO: extend python command bindings
        auto c = class_<Command, CommandWrap, std::shared_ptr<Command>>(m, "Command").def(init<>()).def("execute", &CommandWrap::raw_execute);

        class_<UndoCommand, Command, UndoCommandWrap, std::shared_ptr<UndoCommand>>(m, "UndoCommand")
            .def(init<>())
            .def("undo", &UndoCommand::undo)
            .def("redo", &UndoCommand::redo)
            .def("merge_with", &UndoCommand::merge_with);

        // TODO: wrap PythonCommandInterface
    }
}

OPENDCC_NAMESPACE_CLOSE
