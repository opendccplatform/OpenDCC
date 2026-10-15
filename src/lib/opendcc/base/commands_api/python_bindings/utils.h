/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/base/commands_api/core/command_syntax.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{
    namespace python
    {
        inline CommandResult::Status convert_args(const std::string& name, const CommandArgs& args, pybind11::tuple& pyargs, pybind11::dict& pykwargs)
        {
            CommandSyntax syntax;
            if (!CommandRegistry::get_command_syntax(name, syntax))
            {
                return CommandResult::Status::CMD_NOT_REGISTERED;
            }

            const auto& syntax_arg_types = syntax.get_arg_descriptors();
            pybind11::list pyargs_list;
            for (int i = 0; i < syntax_arg_types.size(); i++)
            {
                auto py_obj = PythonCommandInterface::to_python(args.get_arg(i), syntax_arg_types[i].type_indices);
                if (!py_obj.is_none())
                {
                    pyargs_list.append(py_obj);
                }
                else
                {
                    return CommandResult::Status::INVALID_SYNTAX;
                }
            }
            pyargs = pybind11::tuple(pyargs_list);

            pykwargs.clear();
            for (const auto& kwarg : args.get_kwargs())
            {
                auto py_obj = PythonCommandInterface::to_python(kwarg.second, syntax.get_kwarg_descriptor(kwarg.first).type_indices);
                if (!py_obj.is_none())
                {
                    pykwargs[pybind11::str(kwarg.first)] = py_obj;
                }
                else
                {
                    return CommandResult::Status::INVALID_SYNTAX;
                }
            }

            return CommandResult::Status::SUCCESS;
        }
    };
}

OPENDCC_NAMESPACE_CLOSE
