/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"

#include "opendcc/base/logging/logger.h"
#include <unordered_map>

OPENDCC_NAMESPACE_OPEN

namespace base::logging
{

    /**
     * @brief Converts a LogLevel enum to its string representation.
     * @param level The log level to convert.
     * @return The string representation of the log level.
     */
    inline const std::string& log_level_to_str(LogLevel level)
    {
        static const std::unordered_map<LogLevel, std::string> mappings = {
            { LogLevel::Unknown, "Unknown" }, { LogLevel::Info, "Info" },   { LogLevel::Debug, "Debug" },
            { LogLevel::Warning, "Warning" }, { LogLevel::Error, "Error" }, { LogLevel::Fatal, "Fatal" },
        };
        return mappings.at(level);
    }

    /**
     * @brief Converts a string representation of a log level to the LogLevel enum.
     * @param str The string to convert.
     * @return The LogLevel enum corresponding to the string.
     */
    inline LogLevel str_to_log_level(const std::string& str)
    {
        static const std::unordered_map<std::string, LogLevel> mappings = { { "Unknown", LogLevel::Unknown }, { "Info", LogLevel::Info },
                                                                            { "Debug", LogLevel::Debug },     { "Warning", LogLevel::Warning },
                                                                            { "Error", LogLevel::Error },     { "Fatal", LogLevel::Fatal } };

        if (str.empty())
        {
            OPENDCC_ERROR("Failed to convert Log level string to enum: string is empty.");
            return LogLevel::Unknown;
        }
        for (const auto& entry : mappings)
        {
            if (entry.first == str)
            {
                return entry.second;
            }
        }

        OPENDCC_ERROR("Failed to convert Log level string to enum: string '{}' is unknown log level.", str);
        return LogLevel::Unknown;
    }

}

OPENDCC_NAMESPACE_CLOSE
