// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_resolver.h"
#include "package_registry.h"
#include "opendcc/base/logging/logger.h"
#include <unordered_set>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    std::vector<std::string> PackageResolver::get_dependees(const std::string& pkg_name) const
    {
        return traverse(
            pkg_name, [this](const std::string& pkg_name) -> const std::vector<std::string>& { return m_package_relations.at(pkg_name).dependees; });
    }

    std::vector<std::string> PackageResolver::traverse(const std::string& pkg_name, const DependencyAccessor& get_deps_fn) const
    {
        auto it = m_package_relations.find(pkg_name);
        if (it == m_package_relations.end())
        {
            return {};
        }

        if (it->second.circular)
        {
            return {};
        }

        std::vector<std::string> result;
        std::unordered_map<std::string, bool> visited;
        std::function<void(const std::string&)> dfs = [this, &dfs, &visited, &result, &get_deps_fn](const std::string& package_name) {
            auto& already_visited = visited[package_name];
            if (already_visited)
            {
                return;
            }

            const auto& deps = get_deps_fn(package_name);
            for (const auto& dep : deps)
            {
                dfs(dep);
            }
            already_visited = true;
            result.push_back(package_name);
        };

        dfs(pkg_name);
        return result;
    }

    std::vector<std::string> PackageResolver::get_dependencies(const std::string& pkg_name) const
    {
        return traverse(pkg_name, [this](const std::string& pkg_name) -> const std::vector<std::string>& {
            return m_package_relations.at(pkg_name).dependencies;
        });
    }

    void PackageResolver::set_packages(const PackageSharedDataMap& packages)
    {
        m_package_relations.clear();

        struct PackageEntry
        {
            bool visited = false;
            bool visiting = false;
        };
        std::unordered_map<std::string, PackageEntry> entries;
        std::unordered_set<std::string> ignored;
        entries.reserve(packages.size());

        std::function<bool(const std::string&, std::string& circular_dep, std::unordered_set<std::string>&)> dfs =
            [this, &dfs, &entries](const std::string& package_name, std::string& circular_dep, std::unordered_set<std::string>& visited_packages) {
                auto& package_entry = entries[package_name];
                if (package_entry.visited)
                {
                    return true;
                }
                if (package_entry.visiting)
                {
                    circular_dep = package_name;
                    return false;
                }

                visited_packages.insert(package_name);

                const auto& deps = m_package_relations.at(package_name).dependencies;
                package_entry.visiting = true;
                for (const auto& dep : deps)
                {
                    if (!dfs(dep, circular_dep, visited_packages))
                    {
                        return false;
                    }
                }
                package_entry.visited = true;
                return true;
            };

        for (const auto& package : packages)
        {
            auto deps = package.second->direct_dependencies;

            const auto package_name = package.first;
            entries.emplace(package_name, PackageEntry { false, false });

            // TODO: for now accept all packages, but there might be situations where
            // some dependencies are optional or require exact versions
            auto& cur_pkg_rel = m_package_relations[package_name];
            for (const auto& dep : deps)
            {
                cur_pkg_rel.dependencies.push_back(dep.first);
                m_package_relations[dep.first].dependees.push_back(package_name);
            }
        }

        std::unordered_set<std::string> visited_packages;
        std::unordered_map<std::string, std::unordered_set<std::string>> circular_dependencies;
        for (const auto& package : packages)
        {
            if (visited_packages.find(package.second->name) != visited_packages.end())
            {
                continue;
            }

            std::unordered_set<std::string> visited_on_this_iteration;
            std::string circular_dep;
            if (!dfs(package.second->name, circular_dep, visited_on_this_iteration))
            {
                for (const auto& p : visited_on_this_iteration)
                {
                    m_package_relations.at(p).circular = true;
                    circular_dependencies[circular_dep].insert(p);
                }
                continue;
            }
            visited_packages.insert(visited_on_this_iteration.begin(), visited_on_this_iteration.end());
        }

        if (!circular_dependencies.empty())
        {
            for (const auto& circular_pkg : circular_dependencies)
            {
                std::string affected;
                for (const auto& pkg : circular_pkg.second)
                {
                    affected += " " + pkg;
                }

                OPENDCC_ERROR("Package '{}' produces circular dependency. Following affected packages will be ignored:{}", circular_pkg.first,
                              affected);
            }
        }
    }

}

OPENDCC_NAMESPACE_CLOSE
