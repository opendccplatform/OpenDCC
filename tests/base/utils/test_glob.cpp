/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/fs_utils/glob.h"
#include "opendcc/base/utils/scope_guard.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

namespace
{
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (unsigned int attempt = 0;; ++attempt)
            {
                m_path = std::filesystem::temp_directory_path() / ("opendcc-glob-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                if (std::filesystem::create_directory(m_path))
                    break;
            }
        }
        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        const std::filesystem::path& path() const { return m_path; }
        std::string write(const std::string& relative_path, const std::string& contents) const
        {
            const auto filename = m_path / std::filesystem::u8path(relative_path);
            std::filesystem::create_directories(filename.parent_path());
            std::ofstream stream(filename);
            stream << contents;
            if (!stream)
                throw std::runtime_error("Could not write test fixture: " + filename.u8string());
            return filename.generic_u8string();
        }

    private:
        std::filesystem::path m_path;
    };
}

DOCTEST_TEST_SUITE("FileSystemGlob")
{
    DOCTEST_TEST_CASE("filesystem_glob_forms_and_order")
    {
        const auto no_patterns = glob_paths({});
        DOCTEST_REQUIRE(no_patterns);
        DOCTEST_CHECK(no_patterns->empty());
        const auto empty_pattern = glob_paths({ "" });
        DOCTEST_REQUIRE(empty_pattern);
        DOCTEST_CHECK(empty_pattern->empty());
        TemporaryDirectory directory;
        const auto a = directory.write("a1/package.toml", "");
        const auto b = directory.write("b2/package.toml", "");
        const auto hidden = directory.write(".hidden/package.toml", "");
        directory.write("name/package.toml", "");
        directory.write("name.txt/package.toml", "");
        const auto unicode = directory.write(u8"\u0442\u0435\u0441\u0442/package.toml", "");
        const auto glob = [&directory](const std::string& pattern) {
            const auto found = glob_paths({ (directory.path() / std::filesystem::u8path(pattern)).generic_u8string() });
            DOCTEST_REQUIRE(found);
            return *found;
        };
        DOCTEST_CHECK(glob("a?/package.toml") == std::vector<std::string> { a });
#ifndef OPENDCC_OS_WINDOWS
        DOCTEST_CHECK(glob("[a-b][1-2]/package.toml") == std::vector<std::string> { a, b });
        DOCTEST_CHECK(glob("[[:alpha:]][[:digit:]]/package.toml") == std::vector<std::string> { a, b });
        DOCTEST_CHECK(glob("a[[:digit:]]/package.toml") == std::vector<std::string> { a });
        DOCTEST_CHECK(glob("[!a]2/package.toml") == std::vector<std::string> { b });
#else
        DOCTEST_CHECK(glob("[a-b][1-2]/package.toml").empty());
#endif
        DOCTEST_CHECK(glob("a*/pack*.to?l") == std::vector<std::string> { a });
        DOCTEST_CHECK(glob("missing*/package.toml").empty());
        DOCTEST_CHECK(glob("a1/package.toml") == std::vector<std::string> { a });
        DOCTEST_CHECK(glob(u8"\u0442*/package.toml") == std::vector<std::string> { unicode });
        DOCTEST_CHECK(glob(u8"\u0442???"
                           "/package.toml") == std::vector<std::string> { unicode });
        DOCTEST_CHECK(glob(".*/package.toml") == std::vector<std::string> { hidden });
        DOCTEST_CHECK(glob("a1") == std::vector<std::string> { (directory.path() / "a1").generic_u8string() + "/" });
        const auto dot_directories = glob(".*");
        DOCTEST_CHECK(std::find(dot_directories.begin(), dot_directories.end(), directory.path().generic_u8string() + "/./") !=
                      dot_directories.end());
        DOCTEST_CHECK(std::find(dot_directories.begin(), dot_directories.end(), directory.path().generic_u8string() + "/../") !=
                      dot_directories.end());
        auto all = glob("*/package.toml");
#ifdef OPENDCC_OS_WINDOWS
        DOCTEST_CHECK(std::find(all.begin(), all.end(), hidden) != all.end());
        DOCTEST_CHECK(glob("A1/package.toml").size() == 1);
        const auto literal_bracket = directory.write("bracket[x]/package.toml", "");
        DOCTEST_CHECK(glob("bracket[x]/package.toml") == std::vector<std::string> { literal_bracket });
        DOCTEST_CHECK(glob("bracket[x]*/package.toml") == std::vector<std::string> { literal_bracket });
        DOCTEST_CHECK(glob("bracket[xy]*/package.toml").empty());
        DOCTEST_CHECK(glob(u8"\u0422*/package.toml") == std::vector<std::string> { unicode });
        DOCTEST_CHECK(glob("name.*/package.toml").size() == 2);
        const auto extensionless = glob("*./package.toml");
        DOCTEST_CHECK(std::find(extensionless.begin(), extensionless.end(), a) != extensionless.end());
        DOCTEST_CHECK(std::find(extensionless.begin(), extensionless.end(), (directory.path() / "name.txt/package.toml").generic_u8string()) ==
                      extensionless.end());
        DOCTEST_CHECK(glob("a?"
                           "?/package.toml") == std::vector<std::string> { a });
#else
        DOCTEST_CHECK(std::find(all.begin(), all.end(), hidden) == all.end());
        DOCTEST_CHECK(glob("A1/package.toml").empty());
        const auto escaped_path = directory.write("star*/package.toml", "");
        DOCTEST_CHECK(glob("star\\*/package.toml") == std::vector<std::string> { escaped_path });
#endif
        const auto ordered_result = glob_paths({ b, a, a });
        DOCTEST_REQUIRE(ordered_result);
        const auto& ordered = *ordered_result;
#ifdef OPENDCC_OS_WINDOWS
        DOCTEST_CHECK(ordered == std::vector<std::string> { a, a, b });
#else
        DOCTEST_CHECK(ordered == std::vector<std::string> { b, a, a });
#endif
        std::error_code error;
        std::filesystem::create_directory_symlink(directory.path() / "a1", directory.path() / "link", error);
        if (!error)
            DOCTEST_CHECK(glob("link/package.toml").size() == 1);
#ifndef OPENDCC_OS_WINDOWS
        std::filesystem::create_symlink(directory.path() / "missing", directory.path() / "dangling", error);
        if (!error)
            DOCTEST_CHECK(glob("dangling") == std::vector<std::string> { (directory.path() / "dangling").generic_u8string() });
        const auto blocked = directory.path() / "blocked";
        std::filesystem::create_directory(blocked);
        const ScopeGuard restore_permissions([&] { std::filesystem::permissions(blocked, std::filesystem::perms::owner_all); });
        std::filesystem::permissions(blocked, std::filesystem::perms::none);
        std::filesystem::directory_iterator inaccessible(blocked, error);
        if (error)
        {
            const auto found = glob_paths({ (blocked / "*").generic_u8string(), a, (blocked / "*").generic_u8string() });
            DOCTEST_REQUIRE_FALSE(found);
            DOCTEST_CHECK(found.error().partial_matches == std::vector<std::string> { a });
            const auto& diagnostics = found.error().diagnostics;
            DOCTEST_REQUIRE(diagnostics.size() == 2);
            for (const auto& diagnostic : diagnostics)
            {
                DOCTEST_CHECK(diagnostic.path == blocked.generic_u8string());
                DOCTEST_CHECK(diagnostic.system_error == std::errc::permission_denied);
                DOCTEST_CHECK(diagnostic.code == GlobErrorCode::PERMISSION_DENIED);
            }
            DOCTEST_REQUIRE(glob_paths({ a }));
        }
#endif
        const auto original_directory = std::filesystem::current_path();
        const ScopeGuard restore_directory([&] { std::filesystem::current_path(original_directory); });
        std::filesystem::current_path(directory.path());
        const auto relative = std::filesystem::path("a1/package.toml");
        const auto relative_result = glob_paths({ relative.generic_u8string() });
        DOCTEST_REQUIRE(relative_result);
        DOCTEST_CHECK(*relative_result == std::vector<std::string> { relative.generic_u8string() });
    }
}

OPENDCC_NAMESPACE_CLOSE
