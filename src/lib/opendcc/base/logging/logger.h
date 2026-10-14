/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/logging/api.h"
#include "opendcc/base/vendor/spdlog/fmt/fmt.h"
#include "opendcc/base/utils/debug.h"

#include <mutex>
#include <vector>
#include <unordered_map>

OPENDCC_NAMESPACE_OPEN

class LoggingDelegate;
class DefaultLoggingDelegate;

/**
 * @enum LogLevel
 * @brief Defines the severity levels for log messages.
 *
 * The LogLevel enum specifies different levels of severity for log messages. Each level indicates the importance
 * and type of the message being logged.
 */
enum class LogLevel
{
    /**
     * @brief Unknown log level.
     *
     * This log level indicates that the severity of the log message is not specified or unknown. It is generally used
     * as a fallback value of some utility functions.
     */
    Unknown,
    /**
     * @brief Informational messages.
     *
     * Use this log level for general information about the application's running state. These messages are typically
     * used for confirming that things are working as expected. Examples include startup messages, configuration details,
     * and regular operation messages.
     */
    Info,
    /**
     * @brief Debugging messages.
     *
     * Use this log level for messages that are useful during development and debugging. These messages can include detailed
     * information about the application's internal state, variable values, and flow of execution. They are typically
     * disabled in a production environment due to their verbosity.
     */
    Debug,
    /**
     * @brief Warning messages.
     *
     * Use this log level for messages that indicate a potential problem or something that could cause issues in the future.
     * Warnings do not necessarily mean that the application is currently malfunctioning, but they highlight situations that
     * may need attention. Examples include deprecated API usage, non-critical failures, and recoverable errors.
     */
    Warning,
    /**
     * @brief Error messages.
     *
     * Use this log level for messages that indicate an error that has occurred in the application. These messages represent
     * issues that need to be addressed but do not necessarily stop the application from running. Examples include failed
     * operations, exceptions that are caught and handled, and invalid input.
     */
    Error,
    /**
     * @brief Fatal error messages.
     *
     * Use this log level for messages that indicate a severe error that has caused or will cause the application to terminate.
     * Fatal messages are used for critical issues that require immediate attention and cannot be recovered from. Examples
     * include system crashes, unhandled exceptions, and major failures that prevent the application from continuing.
     */
    Fatal
};

/**
 * @struct MessageContext
 * @brief Holds contextual information about a log message.
 *
 * The MessageContext struct encapsulates metadata associated with a log message, providing details about the source
 * and nature of the log event. This information is used by logging delegates to properly handle and format log messages.
 */
struct MessageContext
{
    /**
     * @brief The channel of the log message.
     *
     * The channel is a string that categorizes the log message. It can be used to group related log messages together,
     * making it easier to filter and analyze logs based on specific parts of the application.
     */
    std::string channel;
    /**
     * @brief The source file where the log message originated.
     *
     * This member stores the name of the source file from which the log message was generated. It is typically set to
     * the value of the __FILE__ macro, providing a reference to the location in the code.
     */
    const char* file = nullptr;
    /**
     * @brief The function name where the log message originated.
     *
     * This member stores the name of the function from which the log message was generated. It is typically set to the
     * value of the __func__ macro, allowing developers to trace the log message back to the specific function.
     */
    const char* function = nullptr;
    /**
     * @brief The line number in the source file where the log message originated.
     *
     * This member stores the line number in the source file from which the log message was generated. It is typically set
     * to the value of the __LINE__ macro, providing a precise location in the code for debugging purposes.
     */
    int line = 0;
    /**
     * @brief The severity level of the log message.
     *
     * This member stores the log level, indicating the severity of the log message. The log level is an enumeration
     * of type LogLevel, which includes levels such as Info, Debug, Warning, Error, and Fatal.
     */
    LogLevel level = LogLevel::Unknown;
};

/**
 * @class Logger
 * @brief Logger singleton class.
 *
 * Logger is a singleton responsible for managing log messages in an application. It serves as the central hub for logging,
 * ensuring thread-safe operations and flexible message handling through various logging delegates. It can be used immediately after the application
 * with help of special macros.
 *
 * Supports multiple LoggingDelegate handlers, which allows different parts of the application to handle log messages in diverse ways.
 * Examples of delegates include logging to a console, writing to a file, updating a status bar, etc. The Logger provides a default logging delegate,
 * which can be configured for common tasks such as console output and file logging.
 *
 * It is also possible to register a channel that will be used by logging macro by default (see OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL).
 *
 * Logger ensures thread safety, so multiple threads can log and process messages concurrently without conflict.
 * While this class manages the list of logging delegates, it does not own them. It is the client's responsibility to manage the lifetime of these
 * delegates.
 *
 * It is also possible to remove the default logging delegate and use a custom one. Note that removing the default logging delegate doesn't destroy
 * its instance, so it is possible to add it back later if needed.
 *
 * Example Usage:
 * @code
 * OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("Example");
 *
 * // Custom logging delegate example
 * class CustomLoggingDelegate : public LoggingDelegate {
 * public:
 *     void log(const MessageContext& context, const std::string& message) override {
 *         std::cout << context.channel << ": " << message << std::endl;
 *     }
 * };
 *
 * int main() {
 *     // Create and add a custom logging delegate
 *     CustomLoggingDelegate* custom_delegate = new CustomLoggingDelegate;
 *     Logger::add_logging_delegate(custom_delegate);
 *     Logger::remove_loggig_delegate(Logger::get_default_logging_delegate());
 *
 *     // Log messages with different log levels
 *     OPENDCC_INFO("This is an informational message in Example channel.");
 *     OPENDCC_DEBUG("This is a debug message.");
 *     OPENDCC_WARN_CHANNEL("Custom", "This is a warning message in Custom channel.");
 *     OPENDCC_ERROR("This is an error message.");
 *     OPENDCC_FATAL_CHANNEL("Application", "This is a fatal error message in Application channel.");
 *
 *     // Remove the custom logging delegate
 *     Logger::remove_logging_delegate(custom_delegate);
 *
 *     return 0;
 * }
 * @endcode
 */
class Logger
{
public:
    /**
     * @brief Logs a message with the given context.
     *
     * This method logs a message using the provided MessageContext and a specified format string. It supports variadic template arguments
     * for formatting the message. The message is dispatched to all added logging delegates.
     *
     * @param context The context of the message, including details such as log level, source file, function, and line number.
     * @param format The format string of the message to log. For available formatting options see documentation of `fmt` library.
     * @param args Additional arguments for the message format.
     */
    template <class... TArgs>
    static void log(const MessageContext& context, const std::string& format, TArgs&&... args)
    {
        log_impl(context, fmt::format(format, std::forward<TArgs>(args)...));
    }
    static void log(const MessageContext& context, const std::string& msg) { log_impl(context, msg); }

    /**
     * @brief Gets the current log level.
     *
     * This method returns the current log level of the Logger. Messages with a severity below this level will be ignored.
     *
     * @return The current log level.
     */
    OPENDCC_LOGGING_API static LogLevel get_log_level();
    /**
     * @brief Sets the log level.
     *
     * This method sets the log level of the Logger. Only messages with a severity equal to or higher than this level
     * will be logged.
     *
     * @param level The log level to set.
     */
    OPENDCC_LOGGING_API static void set_log_level(LogLevel level);

    /** @brief Sets a channel's minimum severity. Descendants inherit it unless they have an override. */
    OPENDCC_LOGGING_API static void set_channel_log_level(const std::string& channel, LogLevel level);
    /** @brief Removes a channel override, restoring its inherited minimum severity. */
    OPENDCC_LOGGING_API static void clear_channel_log_level(const std::string& channel);
    /** @brief Returns the nearest ancestor's level, or the global level if no override exists. */
    OPENDCC_LOGGING_API static LogLevel get_channel_log_level(const std::string& channel);
    /** @brief Registers the default channel for messages emitted by a source file. */
    OPENDCC_LOGGING_API static bool register_source_channel(const char* file, const char* channel);
    /** @brief Resolves a file's channel, falling back to its library default. */
    OPENDCC_LOGGING_API static const char* get_source_channel(const char* file, const char* fallback);

    /**
     * @brief Adds a logging delegate.
     *
     * This method adds a logging delegate to the Logger. Logging delegates handle the actual logging of messages.
     * The Logger does not take ownership of the delegate; it is the client's responsibility to manage the delegate's lifetime.
     *
     * @param delegate The logging delegate to add.
     */
    OPENDCC_LOGGING_API static void add_logging_delegate(LoggingDelegate* delegate);
    /**
     * @brief Removes a logging delegate.
     *
     * This method removes a logging delegate from the Logger. After removal, the delegate will no longer receive log messages.
     *
     * @param delegate The logging delegate to remove.
     */
    OPENDCC_LOGGING_API static void remove_logging_delegate(LoggingDelegate* delegate);

    /**
     * @brief Gets the default logging delegate.
     *
     * This method returns the default logging delegate used by the Logger. The default delegate is used for common logging
     * tasks such as console output or file logging. Which can be configured via its methods.
     *
     * @return The default logging delegate.
     */
    OPENDCC_LOGGING_API static DefaultLoggingDelegate* get_default_logging_delegate();
    /**
     * @brief Destructor.
     */
    OPENDCC_LOGGING_API ~Logger();

private:
    using Lock = std::lock_guard<std::recursive_mutex>;

    Logger();
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;
    static Logger& instance();
    OPENDCC_LOGGING_API static void log_impl(const MessageContext& context, const std::string& message);
    LogLevel get_channel_log_level_impl(std::string channel) const;

    std::recursive_mutex m_mutex;
    std::vector<LoggingDelegate*> m_delegates;
    DefaultLoggingDelegate* m_default_delegate = nullptr;
    LogLevel m_log_level = LogLevel::Info;
    std::unordered_map<std::string, LogLevel> m_channel_log_levels;
    std::unordered_map<std::string, std::string> m_source_channels;
};

/**
 * @defgroup LoggingMacros Logging Macros
 * @brief Macros for convenient logging with the Logger class.
 *
 * The LoggingMacros group provides macros that simplify logging operations using the Logger class. These macros
 * encapsulate the process of creating a MessageContext and invoking the Logger's log method with formatted messages.
 * They support variadic arguments for message formatting and provide a convenient way to log messages at different
 * severity levels.
 */
// Commentary for developers:
// This variable (and others weak symbols) MUST NOT be initialized here, as `man nm` states:
//
// "The symbol is a weak object. When a weak defined symbol
// is linked with a normal defined symbol, the normal
// defined symbol is used with no error. When a weak
// undefined symbol is linked and the symbol is not defined,
// the value of the weak symbol becomes zero with no error."
//
// This also means that if this variable is initialized to some value then compiler is
// free to make any weird assumptions about its value. The compiler has no distinction between weak
// and normal symbols; it is all (dynamic) linker magic.
OPENDCC_API_HIDDEN OPENDCC_WEAK_LINKAGE const char* g_global_log_channel;

static inline bool initialize_library_log_channel(const char* name, const char* filename = nullptr, int line = 0)
{
    using namespace OPENDCC_NAMESPACE;
    if (g_global_log_channel)
    {
        MessageContext ctx { "opendcc.base.logging", filename, nullptr, line, LogLevel::Warning };
        Logger::log(ctx, "Library log channel was already initialized. There can be only one library log channel.");
        return false;
    }
    g_global_log_channel = name;
    return true;
}
/**
 * @def OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(channel_name)
 * @ingroup LoggingMacros
 * @brief Registers default logging channel inside current binary.
 *
 * This macro is used to register specified channel name as default channel that will be used when call
 * to logging macro is occurred. There can be only one default channel per library or executable. Usually a client
 * should call this macro during initialization. This macro must be placed inside OPENDCC_NAMESPACE.
 *
 * @param channel_name Default channel name to log.
 */
#define OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(channel_name) \
    OPENDCC_API_HIDDEN auto library_log_channel_already_initialized = initialize_library_log_channel(channel_name, __FILE__, __LINE__);

#define OPENDCC_LOG_CHANNEL_JOIN_IMPL(a, b) a##b
#define OPENDCC_LOG_CHANNEL_JOIN(a, b) OPENDCC_LOG_CHANNEL_JOIN_IMPL(a, b)
/** @brief Declares a file-scoped default channel without changing its library default. */
#define OPENDCC_INITIALIZE_LOG_CHANNEL(channel_name)                                   \
    static const bool OPENDCC_LOG_CHANNEL_JOIN(opendcc_source_channel_, __COUNTER__) = \
        OPENDCC_NAMESPACE::Logger::register_source_channel(__FILE__, channel_name);

/**
 * @def OPENDCC_MSG(channel, level, message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a message with a specified channel and log level.
 *
 * This macro creates a MessageContext with the given channel, source file, function, and line number, and then
 * invokes the Logger's log method with the specified log level and formatted message. This macro can also be useful when
 * log level is dynamic.
 *
 * @param channel The channel of the log message.
 * @param level The severity level of the log message (LogLevel).
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_MSG(channel, level, message, ...)                                                                 \
    do                                                                                                            \
    {                                                                                                             \
        const auto opendcc_log_channel = (channel);                                                               \
        if (opendcc_log_channel)                                                                                  \
        {                                                                                                         \
            OPENDCC_NAMESPACE::MessageContext ctx { opendcc_log_channel, __FILE__, __func__, __LINE__, (level) }; \
            OPENDCC_NAMESPACE::Logger::log(ctx, message, ##__VA_ARGS__);                                          \
        }                                                                                                         \
    } while (false)

/**
 * @def OPENDCC_INFO(message, ...)
 * @ingroup LoggingMacros
 * @brief Logs an informational message to the local channel, falling back to the library default.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging informational messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_INFO(message, ...)                                                                                                                   \
    OPENDCC_MSG(OPENDCC_NAMESPACE::Logger::get_source_channel(__FILE__, OPENDCC_NAMESPACE::g_global_log_channel), OPENDCC_NAMESPACE::LogLevel::Info, \
                message, ##__VA_ARGS__)

/**
 * @def OPENDCC_INFO_CHANNEL(channel, message, ...)
 * @ingroup LoggingMacros
 * @brief Logs an informational message with a specified channel.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging informational messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_INFO_CHANNEL(channel, message, ...) OPENDCC_MSG(channel, OPENDCC_NAMESPACE::LogLevel::Info, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_DEBUG(message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a debug message to the local channel, falling back to the library default.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging debug messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_DEBUG(message, ...)                                                                               \
    OPENDCC_MSG(OPENDCC_NAMESPACE::Logger::get_source_channel(__FILE__, OPENDCC_NAMESPACE::g_global_log_channel), \
                OPENDCC_NAMESPACE::LogLevel::Debug, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_DEBUG_CHANNEL(channel, message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a debug message with a specified channel.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging debug messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_DEBUG_CHANNEL(channel, message, ...) OPENDCC_MSG(channel, OPENDCC_NAMESPACE::LogLevel::Debug, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_WARN(message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a warning message to the local channel, falling back to the library default.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging warning messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_WARN(message, ...)                                                                                \
    OPENDCC_MSG(OPENDCC_NAMESPACE::Logger::get_source_channel(__FILE__, OPENDCC_NAMESPACE::g_global_log_channel), \
                OPENDCC_NAMESPACE::LogLevel::Warning, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_WARN_CHANNEL(message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a warning message with a specified channel.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging warning messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_WARN_CHANNEL(channel, message, ...) OPENDCC_MSG(channel, OPENDCC_NAMESPACE::LogLevel::Warning, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_ERROR(message, ...)
 * @ingroup LoggingMacros
 * @brief Logs an error message to the local channel, falling back to the library default.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging error messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_ERROR(message, ...)                                                                               \
    OPENDCC_MSG(OPENDCC_NAMESPACE::Logger::get_source_channel(__FILE__, OPENDCC_NAMESPACE::g_global_log_channel), \
                OPENDCC_NAMESPACE::LogLevel::Error, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_ERROR_CHANNEL(channel, message, ...)
 * @ingroup LoggingMacros
 * @brief Logs an error message with a specified channel.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging error messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_ERROR_CHANNEL(channel, message, ...) OPENDCC_MSG(channel, OPENDCC_NAMESPACE::LogLevel::Error, message, ##__VA_ARGS__)

/**
 * @def OPENDCC_FATAL(message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a fatal error message to the local channel, falling back to the library default.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging fatal error messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_FATAL(message, ...)                                                                               \
    OPENDCC_MSG(OPENDCC_NAMESPACE::Logger::get_source_channel(__FILE__, OPENDCC_NAMESPACE::g_global_log_channel), \
                OPENDCC_NAMESPACE::LogLevel::Fatal, message, ##__VA_ARGS__)

/**
 * @def LOG_FATAL_CHANNEL(channel, message, ...)
 * @ingroup LoggingMacros
 * @brief Logs a fatal error message with a specified channel.
 *
 * This macro is a convenience wrapper around OPENDCC_MSG for logging fatal error messages.
 *
 * @param message The format string of the message to log.
 * @param ... Additional arguments for the message format.
 */
#define OPENDCC_FATAL_CHANNEL(channel, message, ...) OPENDCC_MSG(channel, OPENDCC_NAMESPACE::LogLevel::Fatal, message, ##__VA_ARGS__)

/**
 * @brief Convenient function to execute abort with logging
 *
 * This function logs condition as a Fatal error and tries to trap a debugger if it is attached. Executes abort() eventually.
 *
 * @return Function doesn't return any value.
 */
[[noreturn]] OPENDCC_LOGGING_API bool fail_assert(const MessageContext& ctx, const char* condition);

/**
 * @def OPENDCC_ASSERT(condition)
 * @ingroup LoggingMacros
 * @brief Logs a fatal error message to the local channel, falling back to the library default.
 *
 * This macro checks condition and calls fail_assert function to abort the program, if condition is false.
 *
 * @param condition The condition expression to check.
 */
#define OPENDCC_ASSERT(condition)                                                                                                                    \
    ((condition) ||                                                                                                                                  \
     OPENDCC_NAMESPACE::fail_assert(                                                                                                                 \
         MessageContext { OPENDCC_NAMESPACE::Logger::get_source_channel(                                                                             \
                              __FILE__, OPENDCC_NAMESPACE::g_global_log_channel ? OPENDCC_NAMESPACE::g_global_log_channel : "opendcc.base.logging"), \
                          __FILE__, __FUNCTION__, __LINE__, OPENDCC_NAMESPACE::LogLevel::Fatal },                                                    \
         #condition))

/**
 * @def OPENDCC_DEBUG_ASSERT(condition)
 * @ingroup LoggingMacros
 * @brief Same as 'OPENDCC_ASSERT' but works only in debug builds.
 */
#ifdef OPENDCC_DEBUG_BUILD
#define OPENDCC_DEBUG_ASSERT(condition) OPENDCC_ASSERT(condition)
#else
#define OPENDCC_DEBUG_ASSERT(condition)
#endif

OPENDCC_NAMESPACE_CLOSE
