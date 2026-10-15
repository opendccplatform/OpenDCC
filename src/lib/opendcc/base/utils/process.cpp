// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/utils/process.h"

#include "opendcc/base/defines.h"
#include "opendcc/base/utils/allocation.h"
#include "opendcc/base/fs_utils/defines.h"
#include "opendcc/base/utils/library.h"

#include <limits>

#if defined OPENDCC_OS_WINDOWS
#include <windows.h>
#include <process.h>
#define get_pid_impl() _getpid()
#elif defined OPENDCC_OS_LINUX || defined OPENDCC_OS_MAC
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#define get_pid_impl() ::getpid()
#else
#error unknown platform
#endif

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{
    namespace
    {
#if defined OPENDCC_OS_WINDOWS
        bool process_exist_impl(const int pid)
        {
            HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
            const auto exist = process != NULL;
            if (exist)
            {
                CloseHandle(process);
            }
            return exist;
        }
#elif defined OPENDCC_OS_LINUX || defined OPENDCC_OS_MAC
        bool process_exist_impl(const int pid)
        {
            return kill(pid, 0) == 0;
        }
#else
#error unknown platform
#endif
    }

    int get_pid()
    {
        return get_pid_impl();
    }

    std::string get_pid_string()
    {
        return std::to_string(get_pid());
    }

    bool process_exist(const int pid)
    {
        return process_exist_impl(pid);
    }

    bool process_exist(const std::string& string)
    {
        try
        {
            return process_exist_impl(std::stoi(string.c_str()));
        }
        catch (...)
        {
            return false;
        }
    }

    OPENDCC_BASE_UTILS_API std::string get_executable_path()
    {
#ifdef OPENDCC_OS_WINDOWS
        return std::get<0>(dynamic_alloc_read(OPENDCC_MAX_PATH, [](char* buf, size_t& size) {
            const auto old_size = size;
            size = GetModuleFileName(nullptr, buf, size);
            const auto err = GetLastError();
            if (err == 0)
            {
                return true;
            }
            else if (err == ERROR_INSUFFICIENT_BUFFER)
            {
                size *= 2;
                return false;
            }
            else
            {
                const auto error_str = dl_error_str();
                // todo: log
                size = std::numeric_limits<size_t>::max();
                return false;
            }
        }));
#else
        return std::get<0>(dynamic_alloc_read(OPENDCC_MAX_PATH, [](char* buf, size_t& size) {
            const ssize_t read_bytes = readlink("/proc/self/exe", buf, size);
            if (read_bytes == -1)
            {
                // todo: log
                // ARCH_WARNING("Unable to read /proc/self/exe to obtain "
                //                 "executable path");
                size = std::numeric_limits<size_t>::max();
                return false;
            }
            else if (static_cast<size_t>(read_bytes) >= size)
            {
                struct stat stat_info;
                if (lstat("/proc/self/exe", &stat_info) == 0)
                {
                    size = stat_info.st_size + 1;
                }
                else
                {
                    size *= 2;
                }
                return false;
            }
            else
            {
                buf[read_bytes] = '\0';
                return true;
            }
        }));
#endif
    }

}

OPENDCC_NAMESPACE_CLOSE
