/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include <string>
#include <vector>
OPENDCC_NAMESPACE_OPEN
namespace py_interp
{
    void init_py_interp(std::vector<std::string>& args);
    void run_init();
    void run_init_ui();
    void init_shell();
    int run_script(const std::string& filepath);
    int run_command(const std::string& code);
}
OPENDCC_NAMESPACE_CLOSE
