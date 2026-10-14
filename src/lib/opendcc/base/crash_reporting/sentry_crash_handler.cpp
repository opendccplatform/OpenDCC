// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/crash_reporting/sentry_crash_handler.h"
#include "opendcc/base/logging/logger.h"
#include <opendcc/base/vendor/tiny_process/process.hpp>
#include <opendcc/base/utils/process.h>
#include <filesystem>
#include <sentry.h>
#include <iostream>
#ifndef OPENDCC_OS_WINDOWS

#include <unistd.h>
#include <signal.h>

#endif

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.crash_reporting");

namespace
{
#ifdef OPENDCC_OS_WINDOWS
    static const std::string s_crash_reporter_basename = "crash_reporter.exe";
#else
    static const std::string s_crash_reporter_basename = "crash_reporter";
#endif
    struct CrashHandlerState
    {
        std::string crash_reporter_path;
        sentry_options_t* options = nullptr;
    };

    CrashHandlerState* s_handler_state = nullptr;

    std::string get_crash_reporter_path()
    {
        auto exe_path = get_executable_path();
        auto bin_dir = std::filesystem::path(exe_path).parent_path();
        return (bin_dir / s_crash_reporter_basename).string();
    }
};

CrashHandlerSession::CrashHandlerSession(const ApplicationConfig& config, const std::string_view& program_name)
{
    if (CrashHandler::is_enabled())
    {
        OPENDCC_WARN("Failed to start new crash tracking session: session already started.");
        return;
    }

    CrashHandler::init_session(config, program_name);
    m_is_valid = true;
}

CrashHandlerSession::~CrashHandlerSession()
{
    if (m_is_valid)
    {
        CrashHandler::close_session();
    }
}

void CrashHandler::init_session(const ApplicationConfig& config, const std::string_view& program_name)
{
    if (is_enabled())
    {
        return;
    }

    const auto enabled = config.get<bool>("sentry.enabled", false);
    if (!enabled)
    {
        return;
    }

    auto options = sentry_options_new();
    s_handler_state = new CrashHandlerState;
    s_handler_state->crash_reporter_path = get_crash_reporter_path();
    s_handler_state->options = options;

    sentry_options_set_dsn(options, config.get<std::string>("sentry.dsn").c_str());
    const auto tmp_dir_path = std::filesystem::temp_directory_path() / (std::string(program_name) + "_sentry_db");
    sentry_options_set_database_path(options, tmp_dir_path.string().c_str());
    sentry_options_set_release(options, "opendcc-v" OPENDCC_VERSION_STRING);

    sentry_options_set_require_user_consent(options, true);
    sentry_init(options);

    if (config.get<bool>("sentry.user_consent", false))
    {
        sentry_user_consent_give();
    }
    else
    {
        sentry_user_consent_revoke();

        sentry_options_set_on_crash(
            options,
            [](const sentry_ucontext_t* uctx, sentry_value_t event, void* closure) {
                CrashHandler::run_crash_sender();
                return event;
            },
            nullptr);
    }
    set_tag("program", program_name);
}

void CrashHandler::close_session()
{
    if (is_enabled())
    {
        s_handler_state->options = nullptr;
        sentry_close();
    }
}

void CrashHandler::set_tag(const std::string_view& tag_name, const std::string_view& value)
{
    if (is_enabled())
    {
        sentry_set_tag(tag_name.data(), value.data());
    }
}

void CrashHandler::set_extra(const std::string_view& tag_name, const std::string_view& value)
{
    if (is_enabled())
    {
        sentry_set_extra(tag_name.data(), sentry_value_new_string(value.data()));
    }
}

void CrashHandler::set_user(const std::string_view& username)
{
    if (is_enabled())
    {
        sentry_value_t user = sentry_value_new_object();
        sentry_value_set_by_key(user, "username", sentry_value_new_string(username.data()));
        sentry_set_user(user);
    }
}

void CrashHandler::run_crash_sender()
{
#ifdef OPENDCC_OS_WINDOWS
    STARTUPINFO startup_info;
    std::memset(&startup_info, 0, sizeof(STARTUPINFO));
    startup_info.cb = sizeof(STARTUPINFO);

    PROCESS_INFORMATION proc_info;
    bool create_proc_result = false;

    // In case if a crash has occurred, it is very important to avoid any unnecessary allocations.
    // If a monitoring session is running and crash has occurred then handler state already has pre-cached path to crash_sender binary,
    // otherwise just compute path to crash_sender
    if (s_handler_state)
    {
        create_proc_result = CreateProcess(s_handler_state->crash_reporter_path.c_str(), nullptr, 0, 0, 1, 0, 0, nullptr, &startup_info, &proc_info);
    }
    else
    {
        const auto crash_reporter_path = get_crash_reporter_path();
        create_proc_result = CreateProcess(crash_reporter_path.c_str(), nullptr, 0, 0, 1, 0, 0, nullptr, &startup_info, &proc_info);
    }

    if (create_proc_result)
    {
        CloseHandle(proc_info.hThread);
        CloseHandle(proc_info.hProcess);
    }
#else

    const auto pid = fork();
    if (pid == 0)
    {
        int res = -1;
        if (s_handler_state)
        {
            res = execl("/bin/sh", "sh", "-c", s_handler_state->crash_reporter_path.c_str(), nullptr);
        }
        else
        {
            const auto crash_reporter_path = get_crash_reporter_path();
            res = execl("/bin/sh", "sh", "-c", crash_reporter_path.c_str(), nullptr);
        }
        exit(res);
    }
#endif
}

bool CrashHandler::is_enabled()
{
    return s_handler_state != nullptr;
}

OPENDCC_NAMESPACE_CLOSE
