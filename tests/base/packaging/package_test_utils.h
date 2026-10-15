/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/package_attributes.h"
#include <cstdint>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <string>
#include <stdexcept>
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace packaging_tests
{
    inline bool values_equal(const std::any& actual, const std::any& expected)
    {
        if (actual.type() != expected.type())
            return false;
        if (!actual.has_value())
            return true;
        if (const auto value = std::any_cast<std::string>(&actual))
            return *value == std::any_cast<const std::string&>(expected);
        if (const auto value = std::any_cast<int64_t>(&actual))
            return *value == std::any_cast<int64_t>(expected);
        if (const auto value = std::any_cast<double>(&actual))
            return *value == std::any_cast<double>(expected);
        if (const auto value = std::any_cast<bool>(&actual))
            return *value == std::any_cast<bool>(expected);
        if (const auto values = std::any_cast<base::packaging::PackageAnyArray>(&actual))
        {
            const auto& other = std::any_cast<const base::packaging::PackageAnyArray&>(expected);
            if (values->size() != other.size())
                return false;
            for (size_t index = 0; index < values->size(); ++index)
                if (!values_equal((*values)[index], other[index]))
                    return false;
            return true;
        }
        if (const auto values = std::any_cast<base::packaging::PackageAnyDictionary>(&actual))
        {
            const auto& other = std::any_cast<const base::packaging::PackageAnyDictionary&>(expected);
            if (values->size() != other.size())
                return false;
            for (const auto& entry : *values)
            {
                const auto it = other.find(entry.first);
                if (it == other.end() || !values_equal(entry.second, it->second))
                    return false;
            }
            return true;
        }
        return false;
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (unsigned int attempt = 0;; ++attempt)
            {
                m_path = std::filesystem::temp_directory_path() / ("opendcc-packaging-" + std::to_string(stamp) + "-" + std::to_string(attempt));
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

    inline std::vector<std::string> read_events(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        std::vector<std::string> events;
        for (std::string line; std::getline(stream, line);)
            events.push_back(line);
        return events;
    }
}

OPENDCC_NAMESPACE_CLOSE
