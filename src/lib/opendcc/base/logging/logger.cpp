// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/logging/logger.h"
#include "opendcc/base/logging/default_logging_delegate.h"
#include <cstdarg>
#include <stdexcept>

OPENDCC_NAMESPACE_OPEN

namespace base::logging
{

    OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.logging");

    namespace
    {
        int severity(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Unknown:
                return 0;
            case LogLevel::Debug:
                return 1;
            case LogLevel::Info:
                return 2;
            case LogLevel::Warning:
                return 3;
            case LogLevel::Error:
                return 4;
            case LogLevel::Fatal:
                return 5;
            }
            return 0;
        }
    }

    Logger& Logger::instance()
    {
        static Logger logger;
        return logger;
    }

    void Logger::log_impl(const MessageContext& context, const std::string& message)
    {
        if (context.channel.empty())
        {
            OPENDCC_WARN("Logging channel of message '{}' is null.", message);
            return;
        }

        auto& logger = instance();
        Lock lock(logger.m_mutex);
        if (severity(context.level) < severity(logger.get_channel_log_level_impl(context.channel)))
        {
            return;
        }
        for (auto delegate : logger.m_delegates)
        {
            delegate->log(context, message);
        }
    }

    LogLevel Logger::get_log_level()
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        return logger.m_log_level;
    }

    Logger::Logger()
        : m_default_delegate { new DefaultLoggingDelegate }
    {
        m_delegates.push_back(m_default_delegate);
    }

    void Logger::add_logging_delegate(LoggingDelegate* delegate)
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        for (const auto entry : logger.m_delegates)
        {
            if (entry == delegate)
            {
                OPENDCC_WARN("LoggingDelegate ({}) already added.", static_cast<void*>(delegate));
                return;
            }
        }
        logger.m_delegates.push_back(delegate);
    }

    void Logger::set_log_level(LogLevel level)
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        logger.m_log_level = level;
    }

    void Logger::set_channel_log_level(const std::string& channel, LogLevel level)
    {
        if (channel.empty())
            throw std::invalid_argument("Logging channel must not be empty.");
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        logger.m_channel_log_levels[channel] = level;
    }

    void Logger::clear_channel_log_level(const std::string& channel)
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        logger.m_channel_log_levels.erase(channel);
    }

    LogLevel Logger::get_channel_log_level(const std::string& channel)
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        return logger.get_channel_log_level_impl(channel);
    }

    bool Logger::register_source_channel(const char* file, const char* channel)
    {
        if (!file || !channel || !*channel)
            throw std::invalid_argument("Source file and logging channel must not be empty.");
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        const auto result = logger.m_source_channels.emplace(file, channel);
        if (!result.second && result.first->second != channel)
            throw std::logic_error("Source file already has a different logging channel.");
        return result.second;
    }

    const char* Logger::get_source_channel(const char* file, const char* fallback)
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        const auto iter = logger.m_source_channels.find(file ? file : "");
        return iter == logger.m_source_channels.end() ? fallback : iter->second.c_str();
    }

    LogLevel Logger::get_channel_log_level_impl(std::string channel) const
    {
        while (!channel.empty())
        {
            const auto iter = m_channel_log_levels.find(channel);
            if (iter != m_channel_log_levels.end())
                return iter->second;
            const auto separator = channel.rfind('.');
            if (separator == std::string::npos)
                break;
            channel.resize(separator);
        }
        return m_log_level;
    }

    void Logger::remove_logging_delegate(LoggingDelegate* delegate)
    {
        auto& logger = instance();
        Lock lock(logger.m_mutex);
        auto& delegates = logger.m_delegates;
        auto it = std::find(delegates.begin(), delegates.end(), delegate);
        if (it != delegates.end())
        {
            delegates.erase(it);
        }
    }

    DefaultLoggingDelegate* Logger::get_default_logging_delegate()
    {
        return instance().m_default_delegate;
    }

    Logger::~Logger()
    {
        m_delegates.clear();
        delete m_default_delegate;
    }

    bool fail_assert(const MessageContext& ctx, const char* condition)
    {
        Logger::log(ctx, "Failed assert '{}' in function '{}'.", condition, ctx.function);
#if __cplusplus >= 201703L || _MSVC_LANG >= 201703L
        if (utils::is_debugged())
        {
            utils::trap_debugger();
        }
#endif

        abort();
    }

}

OPENDCC_NAMESPACE_CLOSE
