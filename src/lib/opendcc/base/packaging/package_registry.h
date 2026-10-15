/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/packaging/package_provider.h"
#include "opendcc/base/packaging/package.h"
#include "opendcc/base/packaging/package_parser.h"
#include "opendcc/base/packaging/package_resolver.h"
#include "opendcc/base/packaging/package_loader.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    class OPENDCC_BASE_PACKAGING_API PackageRegistry
    {
    public:
        PackageRegistry();
        PackageRegistry(const PackageRegistry&) = delete;
        PackageRegistry(PackageRegistry&&) = delete;
        PackageRegistry& operator=(const PackageRegistry&) = delete;
        PackageRegistry& operator=(PackageRegistry&&) = delete;
        ~PackageRegistry() = default;

        void fetch_packages(bool load_fetched = false);
        std::vector<Package> get_all_packages() const;
        Package get_package(const std::string& name) const;
        void add_package_provider(const std::shared_ptr<PackageProvider>& package_provider);
        void remove_package_provider(const std::shared_ptr<PackageProvider>& package_provider);
        bool load(const Package& package);
        bool unload(const Package& package);
        bool load(const std::string& pkg_name);
        bool unload(const std::string& pkg_name);
        bool define_token(const std::string& token_name, const std::string& token_value);
        bool undefine_token(const std::string& token_name);

    private:
        void initialize_package_data();
        std::tuple<std::string, std::any> resolve_tokens(const PackageAttribute& raw_attr, const std::string& package_root = "") const;

        std::shared_ptr<PackageResolver> m_package_resolver;
        std::unique_ptr<PackageLoader> m_package_loader;
        std::vector<std::shared_ptr<PackageProvider>> m_package_providers;
        mutable PackageSharedDataMap m_package_shared_data;
        std::unordered_map<std::string, std::string> m_tokens;
    };

}

OPENDCC_NAMESPACE_CLOSE
