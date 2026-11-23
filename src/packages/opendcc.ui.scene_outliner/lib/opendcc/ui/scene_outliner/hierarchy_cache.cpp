/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/hierarchy_cache.h"

#include "opendcc/ui/scene_outliner/tree_data_source.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

OPENDCC_NAMESPACE_OPEN

SceneOutlinerHierarchyCache::SceneOutlinerHierarchyCache(const ISceneOutlinerDataSource* source, SceneItemId root)
    : m_source(source)
{
    m_root = register_item(root);
}

SceneOutlinerHierarchyCache::ProxyPtr SceneOutlinerHierarchyCache::register_item(const SceneItemId& item)
{
    const auto existing = m_item_to_proxy.find(item);
    if (existing != m_item_to_proxy.end())
        return existing->second;

    ProxyPtr proxy(new Proxy(item));
    m_item_to_proxy.emplace(item, proxy);
    return proxy;
}

std::optional<bool> SceneOutlinerHierarchyCache::Proxy::child_hint(bool allow_stale) const
{
    if (m_children_loaded)
        return !m_children.empty();
    return m_child_hint_known || allow_stale ? m_has_children : std::nullopt;
}

std::shared_ptr<SceneOutlinerHierarchyCache::Proxy> SceneOutlinerHierarchyCache::retain_proxy(const SceneItemId& item) const
{
    const auto found = m_item_to_proxy.find(item);
    return found == m_item_to_proxy.end() ? nullptr : found->second;
}

SceneItemSet SceneOutlinerHierarchyCache::cached_items() const
{
    SceneItemSet items;
    for (const auto& entry : m_item_to_proxy)
    {
        if (entry.first)
            items.insert(entry.first);
        items.insert(entry.second->m_children.begin(), entry.second->m_children.end());
    }
    return items;
}

void SceneOutlinerHierarchyCache::for_each_proxy(const std::function<void(const Proxy&)>& visit) const
{
    for (const auto& entry : m_item_to_proxy)
        visit(*entry.second);
}

std::optional<SceneItemList> SceneOutlinerHierarchyCache::read_children(const SceneItemId& item) const
{
    if (!m_valid || !m_source)
        return std::nullopt;
    const auto token = m_hierarchy_token;
    auto children = item ? m_source->children(item) : m_source->roots();
    if (!m_valid || !m_source || token.is_cancelled())
        return std::nullopt;
    return children;
}

bool SceneOutlinerHierarchyCache::ensure_children_loaded(Proxy* proxy)
{
    if (!m_valid || !proxy)
        return false;
    if (proxy->m_children_loaded)
        return true;
    const auto retained = retain_proxy(proxy->m_item);
    if (!retained)
        return false;
    auto children = read_children(proxy->m_item);
    if (!children)
        return false;
    set_children(proxy, std::move(*children));
    return true;
}

std::optional<bool> SceneOutlinerHierarchyCache::read_child_hint(Proxy* proxy)
{
    const auto retained = retain_proxy(proxy->m_item);
    if (!retained || !m_valid || !m_source)
        return std::nullopt;
    const auto token = m_hierarchy_token;
    const bool has_children = m_source->has_children(proxy->m_item);
    if (!m_valid || !m_source || token.is_cancelled())
        return std::nullopt;
    proxy->m_has_children = has_children;
    proxy->m_child_hint_known = true;
    return has_children;
}

void SceneOutlinerHierarchyCache::invalidate_child_hints(const SceneItemSet& items)
{
    for (const auto& item : items)
        if (auto* proxy = proxy_for(item))
            proxy->m_child_hint_known = false;
}

void SceneOutlinerHierarchyCache::set_children(Proxy* proxy, SceneItemList children)
{
    proxy->m_children.clear();
    proxy->m_child_proxies.clear();
    append_children(proxy, std::move(children));
}

void SceneOutlinerHierarchyCache::append_children(Proxy* proxy, SceneItemList children)
{
    const auto first = proxy->m_children.size();
    if (first == 0)
        proxy->m_children = std::move(children);
    else
        proxy->m_children.insert(proxy->m_children.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));

    proxy->m_child_hint_known = true;
    proxy->m_has_children = !proxy->m_children.empty();
    proxy->m_child_proxies.resize(proxy->m_children.size(), nullptr);
    for (size_t row = first; row < proxy->m_children.size(); ++row)
        if (auto* child = proxy_for(proxy->m_children[row]))
        {
            proxy->m_child_proxies[row] = child;
            child->m_parent = proxy->m_item;
            child->m_row = row;
        }
    proxy->m_children_loaded = true;
}

void SceneOutlinerHierarchyCache::apply_child_rows(Proxy* proxy, SceneItemList children, std::size_t first, std::size_t removed, std::size_t inserted)
{
    for (std::size_t row = first; row < first + removed; ++row)
        delete_subtree(proxy->m_children[row]);

    auto& proxies = proxy->m_child_proxies;
    if (inserted)
        proxies.insert(proxies.begin() + first, inserted, nullptr);
    else
        proxies.erase(proxies.begin() + first, proxies.begin() + first + removed);

    proxy->m_children = std::move(children);
    for (std::size_t row = first; row < proxy->m_children.size(); ++row)
        if (auto* child = proxies[row])
        {
            child->m_parent = proxy->m_item;
            child->m_row = row;
        }
}

std::vector<std::shared_ptr<SceneOutlinerHierarchyCache::Proxy>> SceneOutlinerHierarchyCache::take_retired_proxies()
{
    return std::exchange(m_retired, {});
}

bool SceneOutlinerHierarchyCache::is_root(const Proxy* proxy) const
{
    return proxy && m_root && proxy == m_root.get();
}

bool SceneOutlinerHierarchyCache::contains(const SceneItemId& item) const
{
    return m_item_to_proxy.find(item) != m_item_to_proxy.end();
}

SceneOutlinerHierarchyCache::Proxy* SceneOutlinerHierarchyCache::proxy_for(const SceneItemId& item) const
{
    const auto it = m_item_to_proxy.find(item);
    return it == m_item_to_proxy.end() ? nullptr : it->second.get();
}

std::size_t SceneOutlinerHierarchyCache::child_count(Proxy* proxy)
{
    if (!proxy)
        return 0;
    return ensure_children_loaded(proxy) ? proxy->m_children.size() : 0;
}

SceneOutlinerHierarchyCache::Proxy* SceneOutlinerHierarchyCache::child_at(Proxy* proxy, std::size_t index)
{
    if (!proxy)
        return nullptr;
    if (!ensure_children_loaded(proxy))
        return nullptr;
    if (index >= proxy->m_children.size())
        return nullptr;
    auto*& child = proxy->m_child_proxies[index];
    if (!child)
    {
        child = register_item(proxy->m_children[index]).get();
        child->m_parent = proxy->m_item;
        child->m_row = index;
    }
    return child;
}

SceneOutlinerHierarchyCache::Proxy* SceneOutlinerHierarchyCache::parent_of(const Proxy* child) const
{
    if (!child)
        return nullptr;
    const SceneItemId parent = child->m_parent;
    return parent ? proxy_for(parent) : nullptr;
}

std::size_t SceneOutlinerHierarchyCache::row_of(const Proxy* child) const
{
    if (!child || is_root(child))
        return 0;

    const SceneItemId parent = child->m_parent;
    const auto parent_it = m_item_to_proxy.find(parent);
    if (parent_it == m_item_to_proxy.end())
        return 0;

    // Reaching this child already populated its parent's children.
    const auto& siblings = parent_it->second->m_children;
    if (child->m_row < siblings.size() && siblings[child->m_row] == child->m_item)
        return child->m_row;
    // A resync may have reordered siblings.
    const auto row = std::find(siblings.begin(), siblings.end(), child->m_item);
    if (row == siblings.end())
        return 0;
    child->m_row = static_cast<std::size_t>(std::distance(siblings.begin(), row));
    return child->m_row;
}

void SceneOutlinerHierarchyCache::delete_subtree(const SceneItemId& item)
{
    SceneItemList pending { item };
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        const auto it = m_item_to_proxy.find(current);
        if (it == m_item_to_proxy.end())
            continue;
        const auto proxy = it->second;
        if (proxy->m_children_loaded)
            pending.insert(pending.end(), proxy->m_children.begin(), proxy->m_children.end());

        // Keep the proxy alive until Qt has invalidated or remapped persistent indices.
        m_retired.push_back(proxy);
        m_item_to_proxy.erase(it);
    }
}

void SceneOutlinerHierarchyCache::invalidate_subtrees(SceneItemList pending, ChildLists& child_lists, SceneItemSet& refreshed)
{
    const auto operation = m_hierarchy_token;
    const auto is_current = [&]() {
        return m_valid && !operation.is_cancelled() && m_source;
    };
    std::unordered_map<SceneItemId, bool, SceneItemId::Hash> reachable;
    if (!m_root->m_item)
        for (const auto& root : m_root->m_children)
            reachable.emplace(root, true);
    const auto within_roots = [&](SceneItemId item) {
        SceneItemList chain;
        SceneItemSet visited;
        bool found = false;
        while (item)
        {
            const auto known = reachable.find(item);
            if (known != reachable.end())
            {
                found = known->second;
                break;
            }
            if (!visited.insert(item).second)
                break;
            chain.push_back(item);
            item = m_source->parent(item);
            if (!is_current())
                return false;
        }
        for (const auto& ancestor : chain)
            reachable.emplace(ancestor, found);
        return found;
    };
    SceneItemSet visited;
    while (!pending.empty() && is_current())
    {
        const auto item = pending.back();
        pending.pop_back();
        const auto it = m_item_to_proxy.find(item);
        if (it == m_item_to_proxy.end() || !visited.insert(item).second)
            continue;
        const auto retained = it->second;
        auto* proxy = retained.get();
        proxy->m_child_hint_known = false;
        const bool exists = m_source->has_item(item);
        if (!is_current())
            return;
        SceneItemId parent;
        bool attached = exists;
        if (exists)
        {
            parent = m_source->parent(item);
            if (!is_current())
                return;
            if (!m_root->m_item)
            {
                const auto root = reachable.find(item);
                attached = (root != reachable.end() && root->second) || within_roots(parent);
                if (!is_current())
                    return;
                reachable[item] = attached;
            }
        }
        if (attached)
        {
            proxy->m_parent = parent;
            if (!resync_children(proxy, pending, child_lists, refreshed))
                return;
        }
        else
        {
            // Removed roots or ancestors can still have descendants that moved elsewhere.
            pending.insert(pending.end(), proxy->m_children.begin(), proxy->m_children.end());
            m_retired.push_back(retained);
            m_item_to_proxy.erase(item);
        }
    }
}

void SceneOutlinerHierarchyCache::resync(const SceneItemList& parents)
{
    resync(parents, {});
}

void SceneOutlinerHierarchyCache::resync(const SceneItemList& parents, ChildLists child_lists)
{
    SceneItemList pending;
    SceneItemSet refreshed;
    for (const auto& parent : parents)
    {
        if (!m_valid)
            return;
        const auto it = m_item_to_proxy.find(parent);
        if (it == m_item_to_proxy.end())
            continue;

        const auto retained = it->second;
        Proxy* proxy = retained.get();
        proxy->m_child_hint_known = false;
        if (!resync_children(proxy, pending, child_lists, refreshed))
            return;
    }
    invalidate_subtrees(std::move(pending), child_lists, refreshed);
}

bool SceneOutlinerHierarchyCache::resync_children(Proxy* proxy, SceneItemList& pending, ChildLists& child_lists, SceneItemSet& refreshed)
{
    if (!proxy->m_children_loaded || !refreshed.insert(proxy->m_item).second)
        return true;

    const auto saved = child_lists.find(proxy->m_item);
    auto updated = saved == child_lists.end() ? read_children(proxy->m_item) : std::optional<SceneItemList>(std::move(saved->second));
    if (!updated)
        return false;
    auto children = std::move(proxy->m_children);
    set_children(proxy, std::move(*updated));
    SceneItemSet seen(children.begin(), children.end());
    for (const auto& child : proxy->m_children)
        if (seen.insert(child).second)
            children.push_back(child);
    pending.insert(pending.end(), children.begin(), children.end());
    return m_valid;
}

SceneOutlinerHierarchyCache::Proxy* SceneOutlinerHierarchyCache::materialize(const SceneItemId& item,
                                                                             const std::function<bool(Proxy*)>& load_children)
{
    return materialize(item, load_children, nullptr);
}

SceneOutlinerHierarchyCache::Proxy* SceneOutlinerHierarchyCache::materialize(const SceneItemId& item,
                                                                             const std::function<bool(Proxy*)>& load_children, bool* awaiting_rows)
{
    if (!m_valid)
        return nullptr;
    const auto operation = m_hierarchy_token;
    if (Proxy* existing = proxy_for(item))
        return existing;
    if (!m_source || !item || !m_root)
        return nullptr;

    SceneItemList chain;
    SceneItemSet visited;
    for (SceneItemId cur = item; cur;)
    {
        if (!visited.insert(cur).second)
            return nullptr;
        const bool exists = m_source->has_item(cur);
        if (!m_valid || operation.is_cancelled() || !m_source || !exists)
            return nullptr;
        chain.push_back(cur);
        if (cur == m_root->m_item)
            break;
        cur = m_source->parent(cur);
        if (!m_valid || operation.is_cancelled() || !m_source)
            return nullptr;
    }
    if (chain.empty())
        return nullptr;
    if (m_root->m_item)
    {
        if (chain.back() != m_root->m_item)
            return nullptr;
        chain.pop_back();
    }

    Proxy* proxy = m_root.get();
    for (auto ancestor = chain.rbegin(); ancestor != chain.rend(); ++ancestor)
    {
        if (!proxy->m_children_loaded && !(load_children ? load_children(proxy) : ensure_children_loaded(proxy)))
            return nullptr;

        const SceneItemList& children = proxy->m_children;
        auto it = std::find(children.begin(), children.end(), *ancestor);
        if (it == children.end() && load_children)
        {
            if (!load_children(proxy))
                return nullptr;
            it = std::find(children.begin(), children.end(), *ancestor);
        }
        if (it == children.end())
        {
            if (awaiting_rows)
            {
                const auto parent = proxy->m_item;
                const auto current = read_children(parent);
                *awaiting_rows = current && std::find(current->begin(), current->end(), *ancestor) != current->end();
            }
            return nullptr;
        }
        proxy = child_at(proxy, static_cast<std::size_t>(std::distance(children.begin(), it)));
        if (!proxy)
            return nullptr;
    }
    return proxy;
}

OPENDCC_NAMESPACE_CLOSE
