/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/hydra_op/translator/api.h"
#include "opendcc/hydra_op/translator/network.h"
#include "opendcc/base/logging/logger.h"
#include <pxr/imaging/hd/sceneIndex.h>
#include "opendcc/hydra_op/schema/nodegraph.h"
#include "opendcc/app/core/stage_watcher.h"
#include <memory>

OPENDCC_NAMESPACE_OPEN

class HydraOpNetwork;
class HydraOpNetworkData;
class HydraOpNodeTranslator;

class IHydraOpUSDTranslator
{
public:
    virtual ~IHydraOpUSDTranslator() = default;

    virtual PXR_NS::SdfPath get_root() const = 0;
    virtual PXR_NS::UsdStageRefPtr get_stage() const = 0;
    virtual GraphNode* get_node(const PXR_NS::SdfPath& node_path) = 0;
};

class IHydraOpUsdStageListener
{
public:
    using Dispatcher = eventpp::CallbackList<void()>;
    using Handle = Dispatcher::Handle;

    virtual ~IHydraOpUsdStageListener() = default;
    virtual void process_changes(const PXR_NS::UsdNotice::ObjectsChanged& notice) = 0;
    virtual Handle subscribe_for_node(const PXR_NS::SdfPath& node_path, const std::function<void()>& callback) = 0;
    virtual void unsubscribe_for_node(const PXR_NS::SdfPath& node_path, const Handle& handle) = 0;
};

class IHydraOpNetworkDataModifier
{
public:
    virtual ~IHydraOpNetworkDataModifier() = default;

    virtual void remove_connection(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to) = 0;
    virtual void mark_finalized(const PXR_NS::SdfPath& node_path) = 0;
};

class HydraOpNetworkModifier final
    : public IHydraOpUSDTranslator
    , public IHydraOpUsdStageListener
    , public IHydraOpNetworkDataModifier
    , public std::enable_shared_from_this<HydraOpNetworkModifier>
{
private:
    struct Private
    {
        explicit Private() = default;
    };

public:
    HydraOpNetworkModifier(const PXR_NS::UsdHydraOpNodegraph& nodegraph, HydraOpNetworkModifier::Private);
    static std::shared_ptr<HydraOpNetworkModifier> create(const PXR_NS::UsdHydraOpNodegraph& nodegraph);

    // Inherited via IHydraOpUsdStageListener
    void process_changes(const PXR_NS::UsdNotice::ObjectsChanged& notice);
    Handle subscribe_for_node(const PXR_NS::SdfPath& node_path, const std::function<void()>& callback) override;
    void unsubscribe_for_node(const PXR_NS::SdfPath& node_path, const Handle& handle) override;

    // Inherited via IHydraOpUSDTranslator
    virtual PXR_NS::SdfPath get_root() const override;
    virtual PXR_NS::UsdStageRefPtr get_stage() const override;
    virtual GraphNode* get_node(const PXR_NS::SdfPath& node_path) override;

    // Inherited via IHydraOpNetworkDataModifier
    void remove_connection(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to);
    void mark_finalized(const PXR_NS::SdfPath& node_path);

    void set_time(PXR_NS::UsdTimeCode time);

private:
    std::unordered_map<PXR_NS::SdfPath, std::unique_ptr<GraphNode>, PXR_NS::SdfPath::Hash>& get_entries();
    const std::unordered_map<PXR_NS::SdfPath, std::unique_ptr<GraphNode>, PXR_NS::SdfPath::Hash>& get_entries() const;
    void initialize();

    void begin_changes();
    void end_changes();

    void add(const PXR_NS::SdfPath& path);
    void remove(const PXR_NS::SdfPath& path);
    void connect(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to);
    void disconnect(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to);
    void bypass_node(const PXR_NS::SdfPath& node_path, bool bypass);

    void process_property_change(const PXR_NS::SdfPath& path);
    void mark_dirty_args(const PXR_NS::SdfPath& path, const PXR_NS::TfToken& attr_name);
    void mark_time_changed();
    void connect_node_inputs(const PXR_NS::UsdPrim& prim, std::function<bool(const PXR_NS::UsdAttribute&)> predicate = nullptr);

    bool is_group(const PXR_NS::UsdPrim& prim) const;

    void remove_node(const PXR_NS::SdfPath& path);
    void add_node(const PXR_NS::SdfPath& path);
    void add_group(const PXR_NS::UsdPrim& group_prim);
    void add_connection(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to);
    void change_bypass(const PXR_NS::SdfPath& node_path, bool bypass_value);
    void finalize(const PXR_NS::SdfPath& node_path);

    void clear();

    template <class T, class... TArgs>
    T* add_entry(const PXR_NS::UsdPrim& prim, TArgs&&... args);

    // USD change tracking data
    struct Connection
    {
        PXR_NS::SdfPath from;
        PXR_NS::SdfPath to;
    };
    std::vector<PXR_NS::SdfPath> m_added_nodes;
    std::vector<PXR_NS::SdfPath> m_removed_nodes;
    std::vector<Connection> m_added_connections;
    std::vector<Connection> m_removed_connections;
    std::unordered_map<PXR_NS::SdfPath, bool, PXR_NS::SdfPath::Hash> m_bypass_changes;
    PXR_NS::SdfPathVector m_dirty_topo_nodes;
    std::unordered_map<PXR_NS::SdfPath, PXR_NS::TfTokenVector, PXR_NS::SdfPath::Hash> m_dirty_args_nodes;

    // Dispatchers
    std::unordered_map<PXR_NS::SdfPath, Dispatcher, PXR_NS::SdfPath::Hash> m_dispatchers;

    // Network Data
    std::unordered_map<PXR_NS::SdfPath, std::unique_ptr<GraphNode>, PXR_NS::SdfPath::Hash> m_entries;

    PXR_NS::SdfPath m_graph_root;
    PXR_NS::UsdStageRefPtr m_stage;
    PXR_NS::UsdTimeCode m_time;
    bool m_editing = false;
    bool m_time_changed = false;
};

template <class T, class... TArgs>
inline T* HydraOpNetworkModifier::add_entry(const PXR_NS::UsdPrim& prim, TArgs&&... args)
{
    static_assert(std::is_base_of_v<GraphNode, T>, "T must be derived from GraphNode.");
    OPENDCC_ASSERT(prim);

    const auto [it, result] = get_entries().emplace(prim.GetPath(), std::make_unique<T>(prim.GetPath(), std::forward<TArgs>(args)...));
    if (!result)
    {
        return nullptr;
    }

    bool bypass = false;
    // qualified rather than relying on a PXR_NAMESPACE_USING_DIRECTIVE arriving transitively;
    // this is a header, so pulling the pxr namespace in here would leak into every consumer
    auto bypass_attr = prim.GetAttribute(PXR_NS::UsdHydraOpTokens->inputsBypass);
    it->second->set_bypass(bypass_attr && bypass_attr.Get(&bypass) && bypass);

    m_dirty_topo_nodes.push_back(prim.GetPath());

    return static_cast<T*>(it->second.get());
}

OPENDCC_NAMESPACE_CLOSE
