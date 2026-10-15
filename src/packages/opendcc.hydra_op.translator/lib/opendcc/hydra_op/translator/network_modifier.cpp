// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "network_modifier.h"
#include "network.h"
#include "registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/hydra_op/schema/group.h"
#include "opendcc/hydra_op/schema/translateAPI.h"
#include <opendcc/base/utils/scope_guard.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    bool is_group_path(const PXR_NS::SdfPath& path)
    {
        return path.IsPropertyPath();
    }

    UsdAttribute get_bypass_attr(const UsdPrim& prim)
    {
        if (auto node = UsdHydraOpBaseNode(prim))
        {
            return node.GetInputsBypassAttr();
        }

        if (auto api = UsdHydraOpTranslateAPI(prim))
        {
            return api.GetHydraOpBypassAttr();
        }
        return {};
    }
}

void HydraOpNetworkModifier::begin_changes()
{
    m_editing = true;
}

void HydraOpNetworkModifier::end_changes()
{
    base::utils::ScopeGuard editing_flag([this] { m_editing = false; });

    // Gather topology changes
    for (const auto& n : m_removed_nodes)
    {
        remove_node(n);
    }

    for (const auto& c : m_removed_connections)
    {
        remove_connection(c.from, c.to);
    }

    for (const auto& n : m_added_nodes)
    {
        add_node(n);
    }

    for (const auto& c : m_added_connections)
    {
        add_connection(c.from, c.to);
    }

    for (const auto& [entry_path, bypass_val] : m_bypass_changes)
    {
        change_bypass(entry_path, bypass_val);
    }

    if (m_dirty_topo_nodes.empty() && m_dirty_args_nodes.empty() && !m_time_changed)
    {
        return;
    }

    // Process changes
    auto& entries = get_entries();
    std::unordered_set<SdfPath, SdfPath::Hash> nodes_to_repopulate;
    std::function<void(const SdfPath&)> traverse = [&traverse, &entries, &nodes_to_repopulate, this](const SdfPath& path) {
        const auto entry_it = entries.find(path.GetPrimPath());
        if (entry_it == entries.end())
        {
            nodes_to_repopulate.insert(path.GetPrimPath());
            return;
        }

        if (auto node = entry_it->second->as_si_node())
        {
            if (m_time_changed && node->is_time_dependent())
            {
                node->set_time(m_time);
            }

            if (nodes_to_repopulate.insert(path.GetPrimPath()).second)
            {
                node->mark_unpopulated();
            }
            else
            {
                return;
            }

            for (const auto& output_port : entry_it->second->get_output_ports())
            {
                for (const auto& out : output_port.route.outputs)
                {
                    traverse(out);
                }
            }
        }
        else
        {
            if (!nodes_to_repopulate.insert(path.GetPrimPath()).second)
            {
                return;
            }

            for (const auto& ports : { &entry_it->second->get_input_ports(), &entry_it->second->get_output_ports() })
            {
                for (const auto& port : *ports)
                {
                    for (const auto& out : port.route.outputs)
                    {
                        traverse(out);
                    }
                }
            }
        }
    };

    for (const auto& n : m_dirty_topo_nodes)
    {
        traverse(n);
    }

    // delay dispatch, gather all dirties first, because some callbacks might want
    // to get data immediately
    std::vector<std::function<void()>> dispatches;
    for (const auto& [node_path, fn] : m_dispatchers)
    {
        if (nodes_to_repopulate.find(node_path) != nodes_to_repopulate.end())
        {
            dispatches.push_back([fn = fn] { fn(); });
            continue;
        }

        auto it = m_dirty_args_nodes.find(node_path);
        if (it != m_dirty_args_nodes.end())
        {
            auto node_it = entries.find(node_path);
            if (node_it != entries.end())
            {
                auto node = node_it->second->as_si_node();
                OPENDCC_ASSERT(node);
                if (node->is_time_dependent())
                {
                    node->set_time(m_time);
                }

                if (node->is_populated())
                {
                    auto prim = m_stage->GetPrimAtPath(node_path);
                    node->process_args_change(prim, it->second);
                }
                dispatches.push_back([fn = fn] { fn(); });
            }
            continue;
        }

        if (!m_time_changed)
        {
            continue;
        }

        auto node_it = entries.find(node_path);
        if (node_it != entries.end())
        {
            if (auto& node = node_it->second; node && node->is_time_dependent())
            {
                node->set_time(m_time);
                dispatches.push_back([fn = fn] { fn(); });
            }
        }
    }

    m_time_changed = false;
    for (const auto& dispatch : dispatches)
    {
        dispatch();
    }

    m_dirty_topo_nodes.clear();
    m_dirty_args_nodes.clear();
    m_added_connections.clear();
    m_added_nodes.clear();
    m_removed_connections.clear();
    m_removed_nodes.clear();
    m_bypass_changes.clear();
}

void HydraOpNetworkModifier::add(const PXR_NS::SdfPath& path)
{
    OPENDCC_ASSERT(m_editing);
    OPENDCC_ASSERT(path.IsPrimPath());
    auto prim = m_stage->GetPrimAtPath(path);
    if (!prim)
    {
        return;
    }

    m_added_nodes.push_back(path);
}

void HydraOpNetworkModifier::remove(const PXR_NS::SdfPath& path)
{
    OPENDCC_ASSERT(m_editing);
    OPENDCC_ASSERT(path.IsPrimPath());
    m_removed_nodes.push_back(path);
}

void HydraOpNetworkModifier::add_group(const PXR_NS::UsdPrim& group_prim)
{
    OPENDCC_ASSERT(UsdHydraOpGroup(group_prim));

    std::function<void(const UsdPrim&)> recursive_add = [this, &recursive_add](const UsdPrim& group_prim) {
        auto entry = add_entry<GroupNode>(group_prim, weak_from_this());
        OPENDCC_ASSERT(entry);

        for (const auto& prim : group_prim.GetChildren())
        {
            if (auto group = UsdHydraOpGroup(prim))
            {
                recursive_add(prim);
            }
            else
            {
                add_node(prim.GetPath());
            }
        }

        connect_node_inputs(group_prim, [](const UsdAttribute& attr) {
            return attr.GetNamespace() == TfToken("inputs") || attr.GetName() == UsdHydraOpTokens->outputsOut;
        });
    };

    recursive_add(group_prim);
}

void HydraOpNetworkModifier::add_connection(const SdfPath& from, const SdfPath& to)
{
    auto& entries = get_entries();

    auto from_entry = entries.find(from.GetPrimPath());
    auto to_entry = entries.find(to.GetPrimPath());
    if (from_entry == entries.end() || to_entry == entries.end())
    {
        // error log
        return;
    }

    auto res = from_entry->second->add_connection(from, to);
    if (!res)
    {
        // error
        return;
    }

    res = to_entry->second->add_connection(from, to);
    if (!res)
    {
        // rollback, error
        from_entry->second->remove_connection(from, to);
        return;
    }

    m_dirty_topo_nodes.push_back(to);
}

void HydraOpNetworkModifier::change_bypass(const PXR_NS::SdfPath& node_path, bool bypass_value)
{
    OPENDCC_ASSERT(node_path.IsPrimPath());

    auto& entries = get_entries();
    auto entry_it = entries.find(node_path);
    if (entry_it == entries.end())
    {
        return;
    }

    const auto cur_bypass = entry_it->second->is_bypassed();
    if (cur_bypass != bypass_value)
    {
        entry_it->second->set_bypass(bypass_value);
        m_dirty_topo_nodes.push_back(node_path);
    }
}

void HydraOpNetworkModifier::finalize(const PXR_NS::SdfPath& node_path)
{
    OPENDCC_ASSERT(node_path.IsPrimPath());

    auto& entries = get_entries();
    auto entry_it = entries.find(node_path);
    if (entry_it == entries.end())
    {
        return;
    }

    entry_it->second->finalize(*this);
}

void HydraOpNetworkModifier::remove_connection(const SdfPath& from, const SdfPath& to)
{
    auto& entries = get_entries();

    auto from_it = entries.find(from.GetPrimPath());
    auto to_it = entries.find(to.GetPrimPath());
    if (from_it == entries.end() || to_it == entries.end())
    {
        // error
        return;
    }

    if (!from_it->second->remove_connection(from, to))
    {
        // error
        return;
    }

    if (!to_it->second->remove_connection(from, to))
    {
        // rollback, error
        from_it->second->add_connection(from, to);
        return;
    }

    m_dirty_topo_nodes.push_back(to);
}

void HydraOpNetworkModifier::add_node(const PXR_NS::SdfPath& path)
{
    OPENDCC_ASSERT(path.IsPrimPath());

    auto& entries = get_entries();
    auto node_at_path = entries.find(path);
    if (node_at_path != entries.end())
    {
        return;
    }

    auto node_prim = m_stage->GetPrimAtPath(path);
    if (!node_prim)
    {
        // log
        return;
    }

    if (is_group(node_prim))
    {
        add_group(node_prim);
        return;
    }

    auto translator = HydraOpTranslatorRegistry::instance().make_translator(node_prim);
    if (!translator)
    {
        return;
    }

    auto entry = add_entry<SceneIndexNode>(node_prim, weak_from_this(), std::move(translator));

    OPENDCC_ASSERT(entry);

    connect_node_inputs(node_prim, [entry](const UsdAttribute& attr) {
        return entry->get_dirty_flags(attr.GetPrim(), attr.GetName()) & HydraOpNodeTranslator::DirtyType::DirtyInput;
    });
}

void HydraOpNetworkModifier::remove_node(const PXR_NS::SdfPath& path)
{
    finalize(path);
}

void HydraOpNetworkModifier::connect(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to)
{
    OPENDCC_ASSERT(m_editing);
    m_added_connections.emplace_back(Connection { from, to });
}

void HydraOpNetworkModifier::disconnect(const PXR_NS::SdfPath& from, const PXR_NS::SdfPath& to)
{
    OPENDCC_ASSERT(m_editing);
    m_removed_connections.emplace_back(Connection { from, to });
}

void HydraOpNetworkModifier::bypass_node(const PXR_NS::SdfPath& node_path, bool bypass)
{
    OPENDCC_ASSERT(m_editing);
    OPENDCC_ASSERT(node_path.IsPrimPath());
    m_bypass_changes[node_path] = bypass;
}

HydraOpNetworkModifier::Handle HydraOpNetworkModifier::subscribe_for_node(const PXR_NS::SdfPath& node_path, const std::function<void()>& callback)
{
    if (auto node = get_node(node_path))
    {
        if (node->is_time_dependent())
        {
            node->set_time(m_time);
        }

        return m_dispatchers[node_path].append(callback);
    }
    return Handle();
}

void HydraOpNetworkModifier::unsubscribe_for_node(const PXR_NS::SdfPath& node_path, const Handle& handle)
{
    if (auto node = get_node(node_path))
    {
        m_dispatchers[node_path].remove(handle);
    }
    else
    {
        m_dispatchers.erase(node_path);
    }
}

void HydraOpNetworkModifier::mark_dirty_args(const PXR_NS::SdfPath& path, const PXR_NS::TfToken& attr_name)
{
    OPENDCC_ASSERT(m_editing);
    m_dirty_args_nodes[path].push_back(attr_name);
}

void HydraOpNetworkModifier::mark_time_changed()
{
    OPENDCC_ASSERT(m_editing);
    m_time_changed = true;
}

void HydraOpNetworkModifier::process_property_change(const PXR_NS::SdfPath& path)
{
    auto prim = m_stage->GetPrimAtPath(path.GetPrimPath());

    auto& entries = get_entries();
    auto node_it = entries.find(prim.GetPath());
    if (node_it == entries.end())
    {
        return;
    }

    auto attr = m_stage->GetAttributeAtPath(path);

    auto bypass_attr = get_bypass_attr(prim);
    bool new_bypass_value = false;
    if (node_it->second->is_bypassed() != (bypass_attr && bypass_attr.Get(&new_bypass_value) && new_bypass_value))
    {
        bypass_node(path.GetPrimPath(), !node_it->second->is_bypassed());
    }

    if (is_group(prim))
    {
        // Ugly dynamic_cast just to validate if typename wasn't accidentally changed
        if (!dynamic_cast<GroupNode*>(node_it->second.get()))
        {
            remove(path.GetPrimPath());
            // something weird happened, typename changed?
            return;
        }

        if (!TfStringStartsWith(path.GetName(), "inputs:") && path.GetNameToken() != UsdHydraOpTokens->outputsOut)
        {
            return;
        }

        SdfPath old_input;

        if (auto port = node_it->second->get_port_by_name(path.GetNameToken()); port && !port->route.inputs.empty())
        {
            old_input = port->route.inputs.front();
        }

        if (!attr)
        {
            disconnect(old_input, attr.GetPath());
        }
        else
        {
            SdfPathVector new_inputs;
            attr.GetConnections(&new_inputs);

            const auto new_input = new_inputs.empty() ? SdfPath::EmptyPath() : new_inputs[0];

            if (!old_input.IsEmpty() && new_input != old_input)
            {
                disconnect(old_input, attr.GetPath());
                connect(new_input, attr.GetPath());
            }
            else if (old_input.IsEmpty() && new_input != old_input)
            {
                connect(new_input, attr.GetPath());
            }
        }
        return;
    }

    // Ugly dynamic_cast just to validate if typename wasn't accidentally changed
    auto node = node_it->second->as_si_node();
    if (!node)
    {
        remove(path.GetPrimPath());
        // something weird happened, typename changed?
        return;
    }

    const auto dirty_flags = node->get_dirty_flags(prim, path.GetNameToken());
    // fast path if we need full node re-populate
    if (dirty_flags == HydraOpNodeTranslator::DirtyType::DirtyNode)
    {
        auto current_outputs = node_it->second->get_output_ports();
        remove(path.GetPrimPath());
        add(path.GetPrimPath());
        for (const auto& out_port : current_outputs)
        {
            for (const auto& out : out_port.route.outputs)
            {
                connect(path.GetPrimPath().AppendProperty(out_port.name), out);
            }
        }
    }
    else if (dirty_flags & HydraOpNodeTranslator::DirtyType::DirtyInput)
    {
        SdfPathVector old_inputs;
        SdfPathVector removed, added;
        for (const auto& in_port : node_it->second->get_input_ports())
        {
            old_inputs.insert(old_inputs.end(), in_port.route.inputs.begin(), in_port.route.inputs.end());
        }

        if (!attr)
        {
            removed = old_inputs;
        }
        else
        {
            SdfPathVector new_inputs;
            attr.GetConnections(&new_inputs);

            std::sort(old_inputs.begin(), old_inputs.end());
            std::sort(new_inputs.begin(), new_inputs.end());
            std::set_difference(old_inputs.begin(), old_inputs.end(), new_inputs.begin(), new_inputs.end(), std::back_inserter(removed));
            std::set_difference(new_inputs.begin(), new_inputs.end(), old_inputs.begin(), old_inputs.end(), std::back_inserter(added));
        }

        for (const auto& rem : removed)
        {
            disconnect(rem, path);
        }
        for (const auto& add : added)
        {
            connect(add, path);
        }
    }
    else if (dirty_flags & HydraOpNodeTranslator::DirtyType::DirtyArgs)
    {
        mark_dirty_args(path.GetPrimPath(), path.GetNameToken());
    }
}

void HydraOpNetworkModifier::mark_finalized(const PXR_NS::SdfPath& node_path)
{
    OPENDCC_ASSERT(node_path.IsPrimPath());
    get_entries().erase(node_path);
    m_dirty_topo_nodes.push_back(node_path);
}

void HydraOpNetworkModifier::set_time(PXR_NS::UsdTimeCode time)
{
    if (time == m_time)
    {
        return;
    }

    if (m_editing)
    {
        mark_time_changed();
    }
    else
    {
        begin_changes();
        m_time = time;
        mark_time_changed();
        end_changes();
    }
}

void HydraOpNetworkModifier::clear()
{
    begin_changes();
    for (const auto& [node_path, node] : m_entries)
    {
        remove_node(node_path);
    }
    end_changes();
}

PXR_NS::SdfPath HydraOpNetworkModifier::get_root() const
{
    return m_graph_root;
}

PXR_NS::UsdStageRefPtr HydraOpNetworkModifier::get_stage() const
{
    return m_stage;
}

GraphNode* HydraOpNetworkModifier::get_node(const PXR_NS::SdfPath& node_path)
{
    OPENDCC_ASSERT(node_path.IsPrimPath());
    auto it = get_entries().find(node_path);
    return it != get_entries().end() ? it->second.get() : nullptr;
}

std::unordered_map<PXR_NS::SdfPath, std::unique_ptr<GraphNode>, PXR_NS::SdfPath::Hash>& HydraOpNetworkModifier::get_entries()
{
    return m_entries;
}

const std::unordered_map<PXR_NS::SdfPath, std::unique_ptr<GraphNode>, PXR_NS::SdfPath::Hash>& HydraOpNetworkModifier::get_entries() const
{
    return m_entries;
}

void HydraOpNetworkModifier::initialize()
{
    auto graph_prim = m_stage->GetPrimAtPath(m_graph_root);
    begin_changes();
    for (auto prim : graph_prim.GetPrim().GetChildren())
    {
        add(prim.GetPath());
    }
    end_changes();
}

void HydraOpNetworkModifier::connect_node_inputs(const PXR_NS::UsdPrim& prim, std::function<bool(const PXR_NS::UsdAttribute&)> predicate)
{
    for (const auto& attr : prim.GetAuthoredAttributes())
    {
        if (predicate && !predicate(attr))
        {
            continue;
        }

        SdfPathVector connections;
        attr.GetConnections(&connections);
        for (const auto& con : connections)
        {
            connect(con, attr.GetPath());
        }
    }
}

bool HydraOpNetworkModifier::is_group(const PXR_NS::UsdPrim& prim) const
{
    return !!UsdHydraOpGroup(prim);
}

HydraOpNetworkModifier::HydraOpNetworkModifier(const PXR_NS::UsdHydraOpNodegraph& graph, Private)
{
    OPENDCC_ASSERT(graph);
    m_stage = graph.GetPrim().GetStage();
    m_graph_root = graph.GetPath();
}

std::shared_ptr<HydraOpNetworkModifier> HydraOpNetworkModifier::create(const PXR_NS::UsdHydraOpNodegraph& nodegraph)
{
    if (!nodegraph)
    {
        return nullptr;
    }

    auto result = std::make_shared<HydraOpNetworkModifier>(nodegraph, Private {});
    result->initialize();
    return result;
}

void HydraOpNetworkModifier::process_changes(const UsdNotice::ObjectsChanged& notice)
{
    if (auto graph_root = m_stage->GetPrimAtPath(m_graph_root); !graph_root || !UsdHydraOpNodegraph(graph_root))
    {
        clear();
        return;
    }

    begin_changes();
    for (const auto& path : notice.GetResyncedPaths())
    {
        if (path.IsPrimPath())
        {
            auto new_prim = m_stage->GetPrimAtPath(path);
            if (new_prim)
            {
                add(path);
            }
            else
            {
                remove(path);
            }
        }
        else
        {
            process_property_change(path);
        }
    }

    for (const auto& path : notice.GetChangedInfoOnlyPaths())
    {
        if (path.IsPropertyPath())
        {
            process_property_change(path);
        }
    }

    end_changes();
}

OPENDCC_NAMESPACE_CLOSE
