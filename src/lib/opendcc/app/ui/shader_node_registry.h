/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include <pxr/base/tf/token.h>
#if PXR_VERSION >= 2508
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>
#else
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
PXR_NAMESPACE_OPEN_SCOPE
using SdrShaderNodeDiscoveryResult = NdrNodeDiscoveryResult;
using SdrShaderNodeDiscoveryResultVec = NdrNodeDiscoveryResultVec;
PXR_NAMESPACE_CLOSE_SCOPE
#endif
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/notice.h>

OPENDCC_NAMESPACE_OPEN

class OPENDCC_API ShaderNodeRegistry
{
public:
    static std::string get_node_plugin_name(const PXR_NS::TfToken& node_name);
    static std::vector<std::string> get_loaded_node_plugin_names();
    static PXR_NS::SdrShaderNodeDiscoveryResultVec get_ndr_plugin_nodes(const std::string& plugin_name);

private:
    ShaderNodeRegistry();
    ShaderNodeRegistry(const ShaderNodeRegistry&) = delete;
    ShaderNodeRegistry(ShaderNodeRegistry&&) = delete;

    ShaderNodeRegistry& operator=(const ShaderNodeRegistry&) = delete;
    ShaderNodeRegistry& operator=(ShaderNodeRegistry&&) = delete;

    static ShaderNodeRegistry& instance();
    void init();

    class PluginWatcher : public PXR_NS::TfWeakBase
    {
    public:
        PluginWatcher();

    private:
        void on_did_register_plugins(const PXR_NS::PlugNotice::DidRegisterPlugins& notice);
    };

    struct PluginEntry
    {
        PXR_NS::TfType type;
        std::string name;
        bool operator==(const PluginEntry& other) const { return type == other.type; }
        bool operator<(const PluginEntry& other) const { return name < other.name; }
    };

    std::unordered_map<PXR_NS::TfToken, std::string, PXR_NS::TfToken::HashFunctor> m_node_to_plugin;
    std::map<std::string, PXR_NS::SdrShaderNodeDiscoveryResultVec> m_plugin_nodes;
    std::set<PluginEntry> m_loaded_plugins;
    std::set<PXR_NS::TfType> m_ndr_plugins;
    std::unique_ptr<PluginWatcher> m_watcher;
};

OPENDCC_NAMESPACE_CLOSE
