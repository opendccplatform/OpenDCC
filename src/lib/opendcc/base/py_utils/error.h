/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/py_utils/api.h"
#include <string>
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN

OPENDCC_PY_UTILS_API std::string py_get_error_str();

inline void py_log_error(const std::string& err_str)
{
    const auto channel = g_global_log_channel ? g_global_log_channel : "opendcc.base.py_utils";
    if (!err_str.empty())
    {
        OPENDCC_ERROR_CHANNEL(channel, "{}", err_str);
    }
    else
    {
        OPENDCC_ERROR_CHANNEL(channel, "A Python error happened during handling of another exception, see stderr for more information.");
    }
}

OPENDCC_NAMESPACE_CLOSE
