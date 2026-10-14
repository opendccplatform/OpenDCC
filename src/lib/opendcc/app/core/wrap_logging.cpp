/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/app/core/wrap_logging.h"
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN

namespace
{
    LogLevel python_log_level(int level)
    {
        return level >= 50   ? LogLevel::Fatal
               : level >= 40 ? LogLevel::Error
               : level >= 30 ? LogLevel::Warning
               : level >= 20 ? LogLevel::Info
                             : LogLevel::Debug;
    }

    void log_python(int level, const std::string& channel, const std::string& message, const std::string& file, const std::string& function, int line)
    {
        MessageContext context { channel, file.c_str(), function.c_str(), line, python_log_level(level) };
        Logger::log(context, message);
    }

    int channel_log_level(const std::string& channel)
    {
        switch (Logger::get_channel_log_level(channel))
        {
        case LogLevel::Debug:
            return 10;
        case LogLevel::Info:
            return 20;
        case LogLevel::Warning:
            return 30;
        case LogLevel::Error:
            return 40;
        case LogLevel::Fatal:
            return 50;
        case LogLevel::Unknown:
            return 0;
        }
        return 0;
    }
}

void py_interp::bind::wrap_logging(pybind11::module& m)
{
    m.def("_log_python", &log_python);
    m.def("_set_channel_log_level", [](const std::string& channel, int level) { Logger::set_channel_log_level(channel, python_log_level(level)); });
    m.def("_clear_channel_log_level", &Logger::clear_channel_log_level);
    m.def("_get_channel_log_level", &channel_log_level);
}

OPENDCC_NAMESPACE_CLOSE
