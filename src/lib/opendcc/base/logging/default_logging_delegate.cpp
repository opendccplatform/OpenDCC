// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/logging/default_logging_delegate.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/vendor/spdlog/spdlog.h"
#include <array>

OPENDCC_NAMESPACE_OPEN

namespace base::logging
{

    DefaultLoggingDelegate::DefaultLoggingDelegate()
    {
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$]%v (%@)");
        spdlog::default_logger()->set_level(spdlog::level::trace);
    }

    void DefaultLoggingDelegate::log(const MessageContext& context, const std::string& message)
    {
        const auto format = "[{}]: {}";
        spdlog::source_loc loc;
        loc.filename = context.file;
        loc.funcname = context.function;
        loc.line = context.line;

        using level_enum = spdlog::level::level_enum;
        static const std::unordered_map<LogLevel, level_enum> to_spd_level = {
            { LogLevel::Unknown, level_enum::err },  { LogLevel::Info, level_enum::info }, { LogLevel::Debug, level_enum::debug },
            { LogLevel::Warning, level_enum::warn }, { LogLevel::Error, level_enum::err }, { LogLevel::Fatal, level_enum::critical }
        };

        spdlog::log(loc, to_spd_level.at(context.level), format, context.channel, message);
    }

}

OPENDCC_NAMESPACE_CLOSE
