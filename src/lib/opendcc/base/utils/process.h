/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

#include "opendcc/base/utils/api.h"

#include <string>

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    /**
     * @brief Returns the process ID of the current process.
     *
     * @return The process ID.
     */
    OPENDCC_BASE_UTILS_API int get_pid();

    /**
     * @brief Returns the process ID of the current process as a string.
     *
     * @return The process ID as a string.
     */
    OPENDCC_BASE_UTILS_API std::string get_pid_string();

    /**
     * @brief Checks if a process with the given process ID (PID) exists.
     *
     * This function verifies whether a process with the specified PID exists in the system.
     *
     * @param pid The process ID (PID) to check.
     * @return true if a process with the given PID exists, false otherwise.
     */
    OPENDCC_BASE_UTILS_API bool process_exist(const int pid);

    /**
     * @brief Checks if a process with the given name or path exists.
     *
     * This function verifies whether a process with the specified name or path exists in the system.
     *
     * @param string The name or path of the process to check.
     * @return true if a process with the given name or path exists, false otherwise.
     */
    OPENDCC_BASE_UTILS_API bool process_exist(const std::string& string);

    OPENDCC_BASE_UTILS_API std::string get_executable_path();

}

OPENDCC_NAMESPACE_CLOSE
