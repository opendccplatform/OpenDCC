/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
OPENDCC_NAMESPACE_OPEN

namespace base::commands
{
    class CommandArgs;
    class CommandResult;
    class CommandSyntax;

    namespace python
    {
        std::string generate_python_cmd_str(const std::string& command_name, const CommandArgs& args);
        std::string generate_python_result_str(const CommandResult& result);
        std::string generate_python_help_str(const std::string& command_name, const CommandSyntax& syntax);
        void register_py_command(const std::string& name, const CommandSyntax& syntax);
        void unregister_py_command(const std::string& name);
    };
}

OPENDCC_NAMESPACE_CLOSE
