// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/fs_utils/glob.h"
#include "opendcc/base/defines.h"
#include "opendcc/base/utils/scope_guard.h"
#include <system_error>
#include <utility>
#ifdef OPENDCC_OS_WINDOWS
#include <algorithm>
#include <filesystem>
#include <Windows.h>
#else
#include <glob.h>
#include <cerrno>
#endif

OPENDCC_NAMESPACE_OPEN

namespace
{
    void add_diagnostic(GlobError& failure, const std::string& path, const std::error_code& error)
    {
        auto code = GlobErrorCode::IO_ERROR;
        if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
            code = GlobErrorCode::PERMISSION_DENIED;
        else if (error == std::errc::not_enough_memory)
            code = GlobErrorCode::OUT_OF_MEMORY;
        failure.diagnostics.push_back({ code, path, error });
    }
}

#ifdef OPENDCC_OS_WINDOWS
namespace
{
    void report_error(const std::filesystem::path& path, const std::error_code& error, GlobError& failure)
    {
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
        {
            const auto utf8_path = path.generic_u8string();
            add_diagnostic(failure, std::string(utf8_path.begin(), utf8_path.end()), error);
        }
    }
}

#else
namespace
{
    thread_local GlobError* glob_error = nullptr;

    int report_glob_error(const char* path, int error)
    {
        if (error != ENOENT && error != ENOTDIR)
            add_diagnostic(*glob_error, path, std::error_code(error, std::generic_category()));
        return 0;
    }
}
#endif

GlobResult glob_paths(const GlobPaths& patterns)
{
    GlobError failure;
    GlobPaths result;
#ifdef OPENDCC_OS_WINDOWS
    for (const auto& pattern : patterns)
    {
        if (pattern.empty())
            continue;
        const auto path = std::filesystem::u8path(pattern);
        std::vector<std::filesystem::path> candidates { path.root_path() };
        for (const auto& part : path.relative_path())
        {
            std::vector<std::filesystem::path> next;
            for (const auto& parent : candidates)
            {
                if (part.native().find_first_of(L"*?") == std::wstring::npos)
                {
                    next.push_back(parent / part);
                    continue;
                }
                const auto directory = parent.empty() ? std::filesystem::path(".") : parent;
                WIN32_FIND_DATAW data {};
                const auto handle = FindFirstFileW((directory / part).c_str(), &data);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    report_error(directory, std::error_code(GetLastError(), std::system_category()), failure);
                    continue;
                }
                const ScopeGuard close_handle([&] { FindClose(handle); });
                do
                {
                    next.push_back(parent / data.cFileName);
                } while (FindNextFileW(handle, &data));
                const auto find_error = GetLastError();
                if (find_error != ERROR_NO_MORE_FILES)
                    report_error(directory, std::error_code(find_error, std::system_category()), failure);
            }
            candidates = std::move(next);
        }
        for (const auto& candidate : candidates)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(candidate, error);
            report_error(candidate, error, failure);
            if (error || !std::filesystem::exists(status))
                continue;
            const auto utf8_path = candidate.generic_u8string();
            std::string value(utf8_path.begin(), utf8_path.end());
            const auto directory = std::filesystem::is_directory(candidate, error);
            report_error(candidate, error, failure);
            if (directory && !value.empty() && value.back() != '/')
                value += '/';
            result.push_back(std::move(value));
        }
    }
    std::sort(result.begin(), result.end());
#else
    const auto previous_error = glob_error;
    glob_error = &failure;
    const ScopeGuard restore_error([&] { glob_error = previous_error; });
    for (const auto& pattern : patterns)
    {
        if (pattern.empty())
            continue;
        glob_t matches {};
        const ScopeGuard free_matches([&] { globfree(&matches); });
        const auto status = glob(pattern.c_str(), GLOB_MARK, report_glob_error, &matches);
        if (status != 0 && status != GLOB_NOMATCH)
            add_diagnostic(failure, pattern, std::make_error_code(status == GLOB_NOSPACE ? std::errc::not_enough_memory : std::errc::io_error));
        for (size_t index = 0; index < matches.gl_pathc; ++index)
            result.emplace_back(matches.gl_pathv[index]);
    }
#endif
    if (!failure.diagnostics.empty())
    {
        failure.partial_matches = std::move(result);
        return Unexpected<GlobError>(std::move(failure));
    }
    return result;
}

OPENDCC_NAMESPACE_CLOSE
