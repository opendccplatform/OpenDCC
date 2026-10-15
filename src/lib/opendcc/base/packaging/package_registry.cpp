// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_registry.h"
#include <opendcc/base/logging/logger.h>
#include <filesystem>
#include "opendcc/base/packaging/core_properties.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/packaging/toml_parser.h"
#include "opendcc/base/packaging/package_entry_point.h"
#include <regex>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.packaging");

    PackageRegistry::PackageRegistry()
    {
        m_package_resolver = std::make_shared<PackageResolver>();
        m_package_loader = std::make_unique<PackageLoader>(m_package_resolver, m_package_shared_data);

#if defined(OPENDCC_OS_WINDOWS)
        define_token("LIB_PREFIX", "");
        define_token("LIB_EXT", ".dll");
#elif defined(OPENDCC_OS_LINUX)
        define_token("LIB_PREFIX", "lib");
        define_token("LIB_EXT", ".so");
#elif defined(OPENDCC_OS_MAC)
        define_token("LIB_PREFIX", "lib");
        define_token("LIB_EXT", ".dylib");
#endif
    }

    void PackageRegistry::fetch_packages(bool load_fetched)
    {
        for (const auto& provider : m_package_providers)
        {
            provider->fetch();
        }

        initialize_package_data();
        m_package_resolver->set_packages(m_package_shared_data);

        for (const auto& pkg_data : m_package_shared_data)
        {
            if (load_fetched || pkg_data.second->get_resolved("base.autoload", false))
            {
                load(Package(pkg_data.second));
            }
        }
    }

    bool PackageRegistry::undefine_token(const std::string& token_name)
    {
        if (m_tokens.erase(token_name) == 0)
        {
            OPENDCC_WARN("Tried to undefine non-existing token '{}'.", token_name);
            return false;
        }
        return true;
    }

    void PackageRegistry::initialize_package_data()
    {
        for (const auto& provider : m_package_providers)
        {
            const auto& packages = provider->get_cached_packages();

            for (const auto& pkg_data : packages)
            {
                PackageAttributeList raw_attributes;
                bool valid_values = true;
                for (const auto& attribute : pkg_data.raw_attributes)
                {
                    auto value = copy_package_value(attribute.value);
                    if (!value)
                    {
                        OPENDCC_ERROR("Invalid values for package '{}': unsupported packaging value type.", pkg_data.name);
                        valid_values = false;
                        break;
                    }
                    raw_attributes.push_back({ attribute.name, std::move(*value) });
                }
                if (!valid_values)
                    continue;
                PackageAnyDictionary attributes;
                for (const auto& raw_attr : raw_attributes)
                {
                    // These paths can refer to the root computed from base.root.
                    if (raw_attr.name == "environment" || raw_attr.name == "python")
                        continue;
                    std::string resolved_name;
                    std::any resolved_value;
                    std::tie(resolved_name, resolved_value) = resolve_tokens(raw_attr);
                    attributes[resolved_name] = resolved_value;
                }

                for (const auto& prop : s_core_property_defaults)
                {
                    auto it = find_package_attribute(attributes, prop.first);
                    if (!it)
                    {
                        PackageAttribute attr { prop.first, prop.second };
                        std::string resolved_name;
                        std::any resolved_value;
                        std::tie(resolved_name, resolved_value) = resolve_tokens(attr);
                        if (!set_package_attribute(attributes, resolved_name, resolved_value))
                        {
                            OPENDCC_ERROR("Invalid default attribute '{}' for package '{}'.", prop.first, pkg_data.name);
                            valid_values = false;
                            break;
                        }
                    }
                }
                if (!valid_values)
                    continue;
                const auto root_value = find_package_attribute(attributes, "base.root");
                const auto root = root_value ? std::any_cast<std::string>(&root_value->get()) : nullptr;
                const auto dependencies_value = find_package_attribute(attributes, "dependencies");
                const auto dependencies = dependencies_value ? std::any_cast<PackageAnyDictionary>(&dependencies_value->get()) : nullptr;
                if (!root || !dependencies)
                {
                    OPENDCC_ERROR("Invalid root or dependencies for package '{}'.", pkg_data.name);
                    continue;
                }
                auto root_dir = std::filesystem::u8path(*root);
                if (root_dir.is_relative())
                {
                    root_dir = std::filesystem::u8path(pkg_data.path) / root_dir;
                }

                for (const auto& raw_attr : raw_attributes)
                {
                    if (raw_attr.name == "environment" || raw_attr.name == "python")
                    {
                        auto resolved = resolve_tokens(raw_attr, root_dir.lexically_normal().generic_u8string());
                        attributes[std::get<0>(resolved)] = std::get<1>(resolved);
                    }
                }

                const auto direct_dependencies = *dependencies;

                auto data = std::make_shared<PackageSharedData>(
                    PackageSharedData { attributes, direct_dependencies, root_dir.lexically_normal().generic_u8string(), pkg_data.name });
                m_package_shared_data[pkg_data.name] = data;
            }
        }
    }

    std::tuple<std::string, std::any> PackageRegistry::resolve_tokens(const PackageAttribute& raw_attr, const std::string& package_root) const
    {
        const std::regex re("\\$\\{([^}]+)\\}", std::regex_constants::ECMAScript | std::regex_constants::icase);

        std::smatch match;
        auto resolve_token = [&match, &re, &package_root, this](const std::string& value) {
            std::sregex_iterator it(value.begin(), value.end(), re);
            std::sregex_iterator end;
            if (it == end)
            {
                return value;
            }

            std::string result;
            while (it != end)
            {
                const auto& match = *it;
                result += match.prefix();
                auto token_it = m_tokens.find(match[1].str());
                if (match[1].str() == "PACKAGE_ROOT" && !package_root.empty())
                {
                    result += package_root;
                }
                else if (token_it != m_tokens.end())
                {
                    result += token_it->second;
                }
                else
                {
                    OPENDCC_WARN("Found token '{}' but it is not defined.", match[0].str());
                    result += match[0].str();
                }
                if (std::next(it) == end)
                {
                    result += match.suffix();
                }
                ++it;
            }
            return result;
        };

        std::function<std::any(const std::any&)> resolve_value = [&resolve_token, &resolve_value, &raw_attr](const std::any& val) -> std::any {
            if (const auto value = std::any_cast<std::string>(&val))
                return resolve_token(*value);
            if (const auto array = std::any_cast<PackageAnyArray>(&val))
            {
                PackageAnyArray result;
                result.reserve(array->size());
                for (const auto& value : *array)
                    result.push_back(resolve_value(value));
                return result;
            }
            if (const auto dictionary = std::any_cast<PackageAnyDictionary>(&val))
            {
                PackageAnyDictionary result;
                const auto expand = find_package_attribute(*dictionary, "expand_tokens");
                const auto expand_value = expand ? std::any_cast<bool>(&expand->get()) : nullptr;
                const bool keep_literal = expand_value && !*expand_value;
                for (const auto& entry : *dictionary)
                {
                    const bool literal_value = keep_literal && ((raw_attr.name == "environment" && entry.first == "value") ||
                                                                (raw_attr.name == "python" && entry.first == "path"));
                    result[resolve_token(entry.first)] = literal_value ? entry.second : resolve_value(entry.second);
                }
                return result;
            }
            return val;
        };

        const auto resolved_name = resolve_token(raw_attr.name);
        const auto resolved_value = resolve_value(raw_attr.value);
        return { resolved_name, resolved_value };
    }

    bool PackageRegistry::define_token(const std::string& token_name, const std::string& token_value)
    {
        auto it = m_tokens.find(token_name);
        if (it != m_tokens.end())
        {
            OPENDCC_WARN("Overriding token value '{}': '{}' -> '{}'", token_name, it->second, token_value);
            it->second = token_value;
        }
        else
        {
            m_tokens[token_name] = token_value;
        }

        return true;
    }

    bool PackageRegistry::unload(const Package& package)
    {
        return m_package_loader->unload(package.get_name());
    }

    bool PackageRegistry::unload(const std::string& pkg_name)
    {
        return m_package_loader->unload(pkg_name);
    }

    bool PackageRegistry::load(const Package& package)
    {
        return m_package_loader->load(package.get_name());
    }

    bool PackageRegistry::load(const std::string& pkg_name)
    {
        return m_package_loader->load(pkg_name);
    }

    void PackageRegistry::remove_package_provider(const std::shared_ptr<PackageProvider>& package_provider)
    {
        auto it = std::find(m_package_providers.begin(), m_package_providers.end(), package_provider);
        if (it == m_package_providers.end())
        {
            OPENDCC_WARN("Failed to remove package provider: provider doesn't exists.");
            return;
        }

        m_package_providers.erase(it);
    }

    void PackageRegistry::add_package_provider(const std::shared_ptr<PackageProvider>& package_provider)
    {
        if (!package_provider)
        {
            OPENDCC_ERROR("Failed to add package provider: provider is empty.");
            return;
        }

        for (const auto& provider : m_package_providers)
        {
            if (provider == package_provider)
            {
                OPENDCC_WARN("Failed to add package provider: provider already exists.");
                return;
            }
        }
        m_package_providers.push_back(package_provider);
    }

    std::vector<Package> PackageRegistry::get_all_packages() const
    {
        std::vector<Package> result;
        for (const auto& entry : m_package_shared_data)
        {
            result.emplace_back(Package { entry.second });
        }
        return result;
    }

    Package PackageRegistry::get_package(const std::string& name) const
    {
        auto it = m_package_shared_data.find(name);
        if (it == m_package_shared_data.end())
        {
            return Package();
        }

        return Package(it->second);
    }

}

OPENDCC_NAMESPACE_CLOSE
