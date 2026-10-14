// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_registry.h"
#include <opendcc/base/logging/logger.h>
#include "pxr/base/vt/array.h"
#include <filesystem>
#include "pxr/base/tf/pyInterpreter.h"
#include "opendcc/base/packaging/core_properties.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/packaging/toml_parser.h"
#include "pxr/base/vt/dictionary.h"
#include "opendcc/base/packaging/package_entry_point.h"
#include <regex>

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN

OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.base.packaging");

OPENDCC_NAMESPACE::PackageRegistry::PackageRegistry()
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
            VtDictionary attributes;
            for (const auto& raw_attr : pkg_data.raw_attributes)
            {
                // These paths can refer to the root computed from base.root.
                if (raw_attr.name == "environment" || raw_attr.name == "python")
                    continue;
                std::string resolved_name;
                VtValue resolved_value;
                std::tie(resolved_name, resolved_value) = resolve_tokens(raw_attr);
                attributes[resolved_name] = resolved_value;
            }

            for (const auto& prop : s_core_property_defaults)
            {
                auto it = attributes.GetValueAtPath(prop.first, ".");
                if (it == nullptr)
                {
                    PackageAttribute attr { prop.first, prop.second };
                    std::string resolved_name;
                    VtValue resolved_value;
                    std::tie(resolved_name, resolved_value) = resolve_tokens(attr);
                    attributes.SetValueAtPath(resolved_name, resolved_value, ".");
                }
            }
            auto root_dir = std::filesystem::u8path(attributes.GetValueAtPath("base.root", ".")->Get<std::string>());
            if (root_dir.is_relative())
            {
                root_dir = std::filesystem::u8path(pkg_data.path) / root_dir;
            }

            for (const auto& raw_attr : pkg_data.raw_attributes)
            {
                if (raw_attr.name == "environment" || raw_attr.name == "python")
                {
                    auto resolved = resolve_tokens(raw_attr, root_dir.lexically_normal().generic_u8string());
                    attributes[std::get<0>(resolved)] = std::get<1>(resolved);
                }
            }

            const auto direct_dependencies = attributes.GetValueAtPath("dependencies", ".")->Get<VtDictionary>();

            auto data = std::make_shared<PackageSharedData>(
                PackageSharedData { attributes, direct_dependencies, root_dir.lexically_normal().generic_u8string(), pkg_data.name });
            m_package_shared_data[pkg_data.name] = data;
        }
    }
}

std::tuple<std::string, VtValue> PackageRegistry::resolve_tokens(const PackageAttribute& raw_attr, const std::string& package_root) const
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

    std::function<VtValue(const VtValue&)> resolve_value = [this, &resolve_token, &resolve_value, &raw_attr](const VtValue& val) {
        VtValue resolved_value;
        if (val.IsHolding<std::string>())
        {
            resolved_value = VtValue(resolve_token(val.UncheckedGet<std::string>()));
        }
        else if (val.IsHolding<VtArray<VtValue>>())
        {
            auto src = val.UncheckedGet<VtArray<VtValue>>();
            VtArray<VtValue> arr;
            for (const auto& val : src)
            {
                arr.push_back(resolve_value(val));
            }
            resolved_value = VtValue(arr);
        }
        else if (val.IsHolding<VtDictionary>())
        {
            const auto& src = val.UncheckedGet<VtDictionary>();
            VtDictionary dict;
            const auto expand = src.GetValueAtPath("expand_tokens");
            const bool keep_literal = expand && expand->IsHolding<bool>() && !expand->UncheckedGet<bool>();
            for (const auto& entry : src)
            {
                const bool literal_value = keep_literal && ((raw_attr.name == "environment" && entry.first == "value") ||
                                                            (raw_attr.name == "python" && entry.first == "path"));
                dict[resolve_token(entry.first)] = literal_value ? entry.second : resolve_value(entry.second);
            }
            resolved_value = VtValue(dict);
        }
        else if (val.IsHolding<VtArray<VtDictionary>>())
        {
            auto src = val.UncheckedGet<VtArray<VtDictionary>>();
            VtArray<VtDictionary> dict_arr;
            for (const auto& val : src)
            {
                dict_arr.push_back(resolve_value(VtValue(val)).UncheckedGet<VtDictionary>());
            }
            resolved_value = VtValue(dict_arr);
        }
        else
        {
            resolved_value = val;
        }
        return resolved_value;
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

OPENDCC_NAMESPACE_CLOSE
