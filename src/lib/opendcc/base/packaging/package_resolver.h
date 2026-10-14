/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/packaging/package.h"
#include "opendcc/base/vendor/nonstd/optional.hpp"
#include <functional>

OPENDCC_NAMESPACE_OPEN

class PackageResolver
{
public:
    void set_packages(const PackageSharedDataMap& pkg_data);
    std::vector<std::string> get_dependencies(const std::string& pkg_name) const;
    std::vector<std::string> get_dependees(const std::string& package_name) const;

private:
    using DependencyAccessor = std::function<const std::vector<std::string>&(const std::string&)>;

    std::vector<std::string> traverse(const std::string& pkg_name, const DependencyAccessor& get_deps_fn) const;

    // Package to outcoming deps
    struct PackageRelations
    {
        std::vector<std::string> dependencies;
        std::vector<std::string> dependees;
        bool circular = false;
    };
    std::unordered_map<std::string, PackageRelations> m_package_relations;
};

OPENDCC_NAMESPACE_CLOSE
