/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/base/logging/api.h"
#include "opendcc/base/logging/logging_delegate.h"

OPENDCC_NAMESPACE_OPEN

namespace base::logging
{

    /**
     * @class DefaultLoggingDelegate
     * @brief Default logging delegate for handling log messages in a default manner.
     *
     * The DefaultLoggingDelegate class provides default behavior for logging messages, including methods to configure
     * logging parameters such as log file location, size limit, and message formatting, etc. It implements the LoggingDelegate
     * interface to handle log messages from the Logger class.
     */
    class OPENDCC_BASE_LOGGING_API DefaultLoggingDelegate : public LoggingDelegate
    {
    public:
        /**
         * @brief Constructs a DefaultLoggingDelegate with default settings.
         *
         */
        DefaultLoggingDelegate();
        /**
         * @brief Logs a message with the default logging behavior.
         *
         * This method implements the LoggingDelegate interface to handle log messages in a default manner.
         *
         * @param context The context of the message, including details such as log level, source file, function, and line number.
         * @param message The message to log.
         */
        void log(const MessageContext& context, const std::string& message) override;
    };

}

OPENDCC_NAMESPACE_CLOSE
