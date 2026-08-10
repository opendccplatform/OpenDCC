// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "shader_node_registry.h"
#include "pxr/base/plug/registry.h"
#if PXR_VERSION >= 2508
#include "pxr/usd/sdr/discoveryPlugin.h"
#else
#include "pxr/usd/ndr/discoveryPlugin.h"
PXR_NAMESPACE_OPEN_SCOPE
using SdrDiscoveryPluginContext = NdrDiscoveryPluginContext;
using SdrDiscoveryPluginFactoryBase = NdrDiscoveryPluginFactoryBase;
PXR_NAMESPACE_CLOSE_SCOPE
#endif

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    // The discovery-plugin base type was renamed as USD deprecated the legacy Ndr library in favor
    // of Sdr: older USD registers plugins under "NdrDiscoveryPlugin", newer USD under
    // "SdrDiscoveryPlugin", and during the deprecation window both names can be present. Looking up
    // only the wrong one yields an unknown TfType, so GetAllDerivedTypes() returns nothing and the
    // shader palette comes up empty (the symptom in the USD 26.05 build). Query both names and union
    // the results — an absent name resolves to an invalid TfType whose GetAllDerivedTypes() is a
    // harmless no-op, and the std::set dedups if a build aliases one name to the other.
    void collect_discovery_plugin_types(std::set<PXR_NS::TfType>& out)
    {
        for (const char* type_name : { "SdrDiscoveryPlugin", "NdrDiscoveryPlugin" })
        {
            const auto base = PXR_NS::PlugRegistry::FindTypeByName(type_name);
            if (!base)
                continue;
            std::set<PXR_NS::TfType> derived;
            base.GetAllDerivedTypes(&derived);
            out.insert(derived.begin(), derived.end());
        }
    }
}

std::string OPENDCC_NAMESPACE::ShaderNodeRegistry::get_node_plugin_name(const PXR_NS::TfToken& node_name)
{
    auto& self = instance();
    self.init();

    auto iter = self.m_node_to_plugin.find(node_name);
    return iter == self.m_node_to_plugin.end() ? "" : iter->second;
}

PXR_NS::SdrShaderNodeDiscoveryResultVec OPENDCC_NAMESPACE::ShaderNodeRegistry::get_ndr_plugin_nodes(const std::string& plugin_name)
{
    auto& self = instance();
    self.init();

    auto iter = self.m_plugin_nodes.find(plugin_name);
    return iter == self.m_plugin_nodes.end() ? SdrShaderNodeDiscoveryResultVec() : iter->second;
}

OPENDCC_NAMESPACE::ShaderNodeRegistry::ShaderNodeRegistry()
{
    m_watcher = std::make_unique<PluginWatcher>();
    collect_discovery_plugin_types(m_ndr_plugins);
    for (const auto& plugin : m_ndr_plugins)
    {
        auto plug = PlugRegistry::GetInstance().GetPluginForType(plugin);
        if (plug)
            plug->Load();
    }
}

std::vector<std::string> OPENDCC_NAMESPACE::ShaderNodeRegistry::get_loaded_node_plugin_names()
{
    auto& self = instance();
    self.init();

    std::vector<std::string> result(self.m_loaded_plugins.size());
    std::transform(self.m_loaded_plugins.begin(), self.m_loaded_plugins.end(), result.begin(), [](const PluginEntry& plugin) { return plugin.name; });
    return result;
}

OPENDCC_NAMESPACE::ShaderNodeRegistry& ShaderNodeRegistry::instance()
{
    static ShaderNodeRegistry instance;
    return instance;
}

void OPENDCC_NAMESPACE::ShaderNodeRegistry::PluginWatcher::on_did_register_plugins(const PXR_NS::PlugNotice::DidRegisterPlugins& notice)
{
    auto& self = ShaderNodeRegistry::instance();
    self.m_loaded_plugins.clear();
    collect_discovery_plugin_types(self.m_ndr_plugins);
    self.init();
}

void OPENDCC_NAMESPACE::ShaderNodeRegistry::init()
{
    std::set<PluginEntry> loaded_plugins;
    for (const auto& plugin : m_ndr_plugins)
    {
        auto plug = PlugRegistry::GetInstance().GetPluginForType(plugin);
        if (plug && plug->IsLoaded())
        {
            const PluginEntry entry = { plugin, plug->GetName() };
            loaded_plugins.emplace(std::move(entry));
        }
    }
    if (loaded_plugins == m_loaded_plugins)
        return;

    m_node_to_plugin.clear();
    m_plugin_nodes.clear();
    m_loaded_plugins = loaded_plugins;

    class CustomCtx : public SdrDiscoveryPluginContext
    {
    public:
        CustomCtx() = default;

        virtual TfToken GetSourceType(const TfToken& discoveryType) const override { return TfToken(); }
    } ctx;

    for (const auto plugin_entry : m_loaded_plugins)
    {
        if (const auto discovery_plug_factory = plugin_entry.type.GetFactory<SdrDiscoveryPluginFactoryBase>())
        {
            const auto discovery_plug = discovery_plug_factory->New();
#if PXR_VERSION >= 2508
            auto nodes = discovery_plug->DiscoverShaderNodes(ctx);
#else
            auto nodes = discovery_plug->DiscoverNodes(ctx);
#endif
            if (!nodes.empty())
            {
                std::sort(nodes.begin(), nodes.end(),
                          [](const SdrShaderNodeDiscoveryResult& left, const SdrShaderNodeDiscoveryResult& right) { return left.name < right.name; });
                for (const auto& node : nodes)
                    m_node_to_plugin[node.identifier] = plugin_entry.name;
                m_plugin_nodes[plugin_entry.name] = std::move(nodes);
            }
        }
    }
}

ShaderNodeRegistry::PluginWatcher::PluginWatcher()
{
    TfNotice::Register(TfCreateWeakPtr(this), &PluginWatcher::on_did_register_plugins);
}

OPENDCC_NAMESPACE_CLOSE
