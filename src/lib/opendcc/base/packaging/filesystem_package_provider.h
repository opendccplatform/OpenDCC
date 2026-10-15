/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include <unordered_map>
#include "opendcc/base/packaging/package_parser.h"
#include "opendcc/base/packaging/package_provider.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    class OPENDCC_BASE_PACKAGING_API FileSystemPackageProvider final : public PackageProvider
    {
    public:
        FileSystemPackageProvider();
        FileSystemPackageProvider(const FileSystemPackageProvider&) = delete;
        FileSystemPackageProvider(FileSystemPackageProvider&&) = delete;
        FileSystemPackageProvider& operator=(const FileSystemPackageProvider&) = delete;
        FileSystemPackageProvider& operator=(FileSystemPackageProvider&&) = delete;

        void fetch() override;
        const PackageDataList& get_cached_packages() const override;

        void register_package_parser(const std::string& extension, const std::shared_ptr<PackageParser>& parser);
        void add_path(const char* package_directory);
        void remove_path(const char* package_directory);

    private:
        std::unordered_map<std::string, std::shared_ptr<PackageParser>> m_package_parsers;
        std::vector<std::string> m_package_directories;
        PackageDataList m_cached_packages;
    };

}

OPENDCC_NAMESPACE_CLOSE
