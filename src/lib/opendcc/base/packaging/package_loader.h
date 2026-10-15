/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/packaging/package.h"
#include "opendcc/base/packaging/package_resolver.h"
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    class PackageLoader
    {
    public:
        PackageLoader(std::shared_ptr<PackageResolver> pkg_resolver, PackageSharedDataMap& pkg_shared_data);
        ~PackageLoader();

        bool load(const std::string& pkg_name);
        bool unload(const std::string& pkg_name);

    private:
        void apply_environment(const std::shared_ptr<PackageSharedData>& pkg_data);
        void apply_python_paths(const std::shared_ptr<PackageSharedData>& pkg_data, const std::string& dependent_package);
        void update_python_path(const std::string& path, const std::string& operation, const std::string& pkg_name,
                                const std::string& dependent_package);
        void remove_package_python_paths();
        bool load_cpp_libs(const std::shared_ptr<PackageSharedData>& pkg_data);
        bool load_python_modules(const std::shared_ptr<PackageSharedData>& pkg_data);

        void unload_cpp_libs(const std::shared_ptr<PackageSharedData>& pkg_data);

        bool extend_python_path(const std::string& path, const std::string& pkg_name, const std::string& dependent_package) const;

        std::shared_ptr<PackageResolver> m_pkg_resolver;
        PackageSharedDataMap& m_pkg_shared_data;
        struct PythonPathEntry
        {
            std::string value;
            bool prepended;
        };
        std::vector<PythonPathEntry> m_python_paths;
    };

}

OPENDCC_NAMESPACE_CLOSE
