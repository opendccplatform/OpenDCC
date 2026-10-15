/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/crash_reporting/api.h"
#include "opendcc/base/app_config/config.h"

OPENDCC_NAMESPACE_OPEN

namespace base::crash_reporting
{

    class OPENDCC_BASE_CRASH_REPORTING_API CrashHandlerSession
    {
    public:
        CrashHandlerSession(const base::app_config::ApplicationConfig& config, const std::string_view& program_name);
        ~CrashHandlerSession();

    private:
        bool m_is_valid = false;
    };

    class OPENDCC_BASE_CRASH_REPORTING_API CrashHandler
    {
    public:
        static void init_session(const base::app_config::ApplicationConfig& config, const std::string_view& program_name);
        static void close_session();
        static void set_tag(const std::string_view& tag_name, const std::string_view& value);
        static void set_extra(const std::string_view& tag_name, const std::string_view& value);
        static void set_user(const std::string_view& username);
        static void run_crash_sender();
        static bool is_enabled();
    };

}

OPENDCC_NAMESPACE_CLOSE
