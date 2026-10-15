// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/sentry_logging_delegate.h"
#include "opendcc/base/logging/logging_utils.h"
#include <sentry.h>

OPENDCC_NAMESPACE_OPEN

using base::logging::Logger;
using base::logging::MessageContext;
using base::logging::LogLevel;

SentryLoggingDelegate::SentryLoggingDelegate()
{
    Logger::add_logging_delegate(this);
}

SentryLoggingDelegate::~SentryLoggingDelegate()
{
    Logger::remove_logging_delegate(this);
}

void SentryLoggingDelegate::log(const MessageContext& context, const std::string& message)
{
    // format according to https://docs.sentry.io/enriching-error-data/breadcrumbs/

    sentry_value_t msg_crumb = sentry_value_new_breadcrumb("default", message.c_str());
    if (!context.channel.empty())
    {
        sentry_value_set_by_key(msg_crumb, "category", sentry_value_new_string(context.channel.c_str()));
    }

    auto log_level_str = base::logging::log_level_to_str(context.level);
    if (context.level == LogLevel::Unknown)
    {
        log_level_str = "error";
    }
    else
    {
        log_level_str[0] = tolower(log_level_str[0]);
    }
    sentry_value_set_by_key(msg_crumb, "level", sentry_value_new_string(log_level_str.c_str()));
    sentry_add_breadcrumb(msg_crumb);
}

OPENDCC_NAMESPACE_CLOSE
