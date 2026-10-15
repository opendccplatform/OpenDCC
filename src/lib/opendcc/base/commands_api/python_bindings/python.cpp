// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/python_bindings/python.h"
#include "opendcc/base/commands_api/python_bindings/utils.h"
#include <opendcc/base/commands_api/core/args.h>
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include <iomanip>
#include <numeric>
#include "opendcc/base/py_utils/lock.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    using base::py_utils::PyLock;

    using namespace pybind11;

    namespace
    {
        int get_strings_length(const std::vector<std::string>& vals)
        {
            int result = 0;
            for (const auto& v : vals)
                result += v.size();
            result += vals.size() - 1;
            return result;
        }
    };

    namespace python
    {
        std::string generate_python_cmd_str(const std::string& command_name, const CommandArgs& args)
        {
            pybind11::gil_scoped_acquire gil;
            // PyLock lock;
            tuple pyargs;
            dict pykwargs;
            if (convert_args(command_name, args, pyargs, pykwargs) != CommandResult::Status::SUCCESS)
                return "";

            auto main = pybind11::module_::import(PYMODULE_NAME_STR);
            try
            {
                auto str_args = main.attr("__to_str")(*pyargs, **pykwargs).cast<std::string>();
                return str_args.empty() ? "" : PYMODULE_NAME_STR "." + command_name + str_args;
            }
            catch (error_already_set)
            {
                PyErr_Print();
                PyErr_Clear();
                return "";
            }
        }

        std::string generate_python_result_str(const CommandResult& result)
        {
            if (!result.has_result())
                return "";

            PyLock lock;
            auto result_type = std::type_index(result.get_type_info());
            const auto pyobj = PythonCommandInterface::to_python(result.get_result(), { result_type });

            PyTypeObject* pyobj_type = pyobj.ptr()->ob_type;
            auto bo = PyBaseObject_Type;
            if (pyobj_type->tp_repr == bo.tp_repr)
                return "";

            try
            {
                return pybind11::repr(pyobj);
            }
            catch (error_already_set)
            {
                PyErr_Print();
                PyErr_Clear();
                return "";
            }
        }

        void register_py_command(const std::string& name, const CommandSyntax& syntax)
        {
            if (!Py_IsInitialized())
                return;

            pybind11::gil_scoped_acquire lock;

            try
            {
                auto mod = pybind11::module_::import(PYMODULE_NAME_STR);
                const auto def_func = "def " + name + "(*args, **kwargs):\n" + "    \"\"\"" + generate_python_help_str(name, syntax) + "\"\"\"\n" +
                                      "    return __execute('" + name + "', args, kwargs)";
                const auto cmds_ns = mod.attr("__dict__");
                pybind11::exec(def_func, cmds_ns, cmds_ns);
            }
            catch (error_already_set)
            {
                std::cerr << "Failed to register Python command '" + name + "': module " PYMODULE_NAME_STR " wasn't found.\n";
                PyErr_Print();
                PyErr_Clear();
            }
        }

        void unregister_py_command(const std::string& name)
        {
            if (!Py_IsInitialized())
                return;

            PyLock lock;
            try
            {
                auto mod = pybind11::module_::import(PYMODULE_NAME_STR);
                const auto del_func = "del " + name;

                const auto cmds_ns = pybind11::module_::import(PYMODULE_NAME_STR).attr("__dict__");
                pybind11::exec(del_func.c_str(), cmds_ns, cmds_ns);
            }
            catch (error_already_set)
            {
                std::cerr << "Failed to unregister Python command '" + name + "': module " PYMODULE_NAME_STR " wasn't found.\n";
                PyErr_Print();
                PyErr_Clear();
            }
        }

        std::string generate_python_help_str(const std::string& command_name, const CommandSyntax& syntax)
        {
            std::stringstream ss;
            const std::string tab = "  ";
            const auto has_pos_args = !syntax.get_arg_descriptors().empty();
            const auto has_kwargs = !syntax.get_kwarg_descriptors().empty();
            if (!syntax.get_command_description().empty())
                ss << "Description:\n" << tab << syntax.get_command_description() << "\n\n";
            ss << "Usage:\n" << tab << command_name << "(";

            if (has_pos_args)
            {
                const auto descrs = syntax.get_arg_descriptors();
                auto iter = descrs.begin();
                ss << iter->name;
                for (++iter; iter != descrs.end(); ++iter)
                    ss << ", " << iter->name;
            }
            if (has_kwargs)
            {
                const auto descrs = syntax.get_kwarg_descriptors();
                const auto sorted = std::map<std::string, CommandSyntax::ArgDescriptor>(descrs.begin(), descrs.end());
                auto iter = sorted.begin();
                if (has_pos_args)
                    ss << ", ";
                ss << "[" << iter->second.name << "]";
                for (++iter; iter != sorted.end(); ++iter)
                    ss << ", [" << iter->second.name << "]";
            }
            ss << ")\n";

            int widest_name = -1;
            int widest_type_name = -1;
            if (has_pos_args)
            {
                for (const auto& descr : syntax.get_arg_descriptors())
                {
                    widest_name = std::max((int)descr.name.size(), widest_name);

                    widest_type_name =
                        std::max(get_strings_length(PythonCommandInterface::get_syntax_arg_types(descr.type_indices)), widest_type_name);
                }
            }
            if (has_kwargs)
            {
                for (const auto& entry : syntax.get_kwarg_descriptors())
                {
                    const auto& descr = entry.second;
                    widest_name = std::max((int)descr.name.size(), widest_name);
                    widest_type_name =
                        std::max(get_strings_length(PythonCommandInterface::get_syntax_arg_types(descr.type_indices)), widest_type_name);
                }
            }
            widest_name = std::min(widest_name, 25);
            widest_type_name = std::min(widest_type_name, 25);

            auto print_arg_descr_info = [&tab, widest_name, widest_type_name, &ss](const CommandSyntax::ArgDescriptor& descr) {
                if (descr.name.size() <= widest_name)
                    ss << tab << std::left << std::setw(widest_name) << descr.name;
                else
                    ss << tab << std::left << descr.name << '\n' << tab << std::setw(widest_name) << " ";

                const auto pytypes = PythonCommandInterface::get_syntax_arg_types(descr.type_indices);
                if (!pytypes.empty())
                {
                    std::string concat_types = pytypes[0];
                    for (int i = 1; i < pytypes.size(); i++)
                        concat_types += '/' + pytypes[i];

                    if (get_strings_length(pytypes) <= widest_type_name)
                        ss << tab << std::setw(widest_type_name) << concat_types;
                    else
                        ss << tab << concat_types << '\n' << tab << std::setw(widest_name) << ' ' << tab << std::setw(widest_type_name) << ' ';
                }
                if (!descr.description.empty())
                    ss << tab << descr.description;
                ss << '\n';
            };

            if (has_pos_args)
            {
                ss << "\nPositional arguments:\n";
                for (const auto& descr : syntax.get_arg_descriptors())
                    print_arg_descr_info(descr);
            }
            if (has_kwargs)
            {
                ss << "\nOptions:\n";
                for (const auto& entry : syntax.get_kwarg_descriptors())
                    print_arg_descr_info(entry.second);
            }

            const auto& result_descr = syntax.get_result_descriptor();
            if (result_descr.is_valid())
            {
                ss << "\nReturns:\n";
                const auto pytypes = PythonCommandInterface::get_syntax_arg_types(result_descr.type_indices);
                if (!pytypes.empty())
                    ss << tab << pytypes[0] << '\n';
                if (!result_descr.description.empty())
                    ss << tab << result_descr.description << '\n';
            }

            return ss.str();
        }

    };
}

OPENDCC_NAMESPACE_CLOSE
