/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/logging/logger.h"
#include <string>

OPENDCC_NAMESPACE_OPEN

namespace base::logging
{

    class MessageContext;

    /**
     * @class LoggingDelegate
     * @brief Abstract base class for logging delegates.
     *
     * A LoggingDelegate processes log messages. Custom logging handlers should inherit from this class and implement the log method.
     * It is designed to allow different logging implementations to be plugged into the Logger.
     */
    class LoggingDelegate
    {
    public:
        /**
         * @brief Virtual destructor. Base implementation removes the logging delegate from the Logger when delegate is about to be deleted.
         */
        virtual ~LoggingDelegate() { Logger::remove_logging_delegate(this); }

        /**
         * @brief Pure virtual function to log a message.
         *
         * This method is called when arbitrary log message is sent via Logger class and its
         * log level is greater or equal than Logger log level. This function will potentially be called in multithreaded environment, but
         * its call is already synchronized.
         *
         * @param context The context of the log message.
         * @param message The log message in UTF8 encoding.
         */
        virtual void log(const MessageContext& context, const std::string& message) = 0;
    };

}

OPENDCC_NAMESPACE_CLOSE
