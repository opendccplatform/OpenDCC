/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"

#include "opendcc/base/packaging/package_attributes.h"
#include "opendcc/base/logging/logger.h"
#include <memory>
#include <unordered_map>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    class Package;
    class PackageEntryPoint;

    struct PackageAttribute
    {
        std::string name;
        std::any value;

        bool is_valid() const { return !name.empty(); }
        operator bool() const { return is_valid(); }
    };

    using PackageAttributeList = std::vector<PackageAttribute>;

    struct PackageSharedData
    {
        PackageAnyDictionary resolved_attributes;
        PackageAnyDictionary direct_dependencies;
        std::string root_dir;
        std::string name;
        bool loaded = false;

        template <class T>
        T get_resolved(const std::string& path, const T& default_ = T()) const
        {
            const auto res = find_package_attribute(resolved_attributes, path);
            if (!res)
                return default_;
            if (const auto value = std::any_cast<T>(&res->get()))
                return *value;
            OPENDCC_WARN("Invalid type for attribute '{}' in package '{}'; using default.", path, name);
            return default_;
        }
        std::string get_resolved(const std::string& path, const char* default_ = "") const { return get_resolved<std::string>(path, default_); }

        struct LoadedEntity
        {
            virtual bool close() = 0;
            virtual void uninitialize(const Package& package) = 0;
        };
        std::vector<std::unique_ptr<LoadedEntity>> loaded_entities;
    };

    using PackageSharedDataMap = std::unordered_map<std::string, std::shared_ptr<PackageSharedData>>;

    class OPENDCC_BASE_PACKAGING_API Package
    {
    public:
        Package() = default;
        Package(const std::shared_ptr<PackageSharedData>& data);
        const std::string& get_name() const;

        const PackageAnyDictionary& get_all_attributes() const;
        const std::any& get_attribute(const std::string& name) const;
        bool is_loaded() const;
        const PackageAnyDictionary& get_direct_dependencies() const;
        const std::string& get_root_dir() const;

        bool is_valid() const;
        operator bool() const { return is_valid(); }

    private:
        std::shared_ptr<PackageSharedData> m_data;
    };

}

OPENDCC_NAMESPACE_CLOSE
