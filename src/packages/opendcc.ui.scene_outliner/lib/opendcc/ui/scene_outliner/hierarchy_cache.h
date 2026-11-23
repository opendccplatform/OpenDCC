/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/details/cancellation_token.h"
#include "opendcc/ui/scene_outliner/item_id.h"

#include <QPointer>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>

OPENDCC_NAMESPACE_OPEN

class ISceneOutlinerDataSource;

/**
 * @brief Caches immediate children lazily, keyed by item identity.
 * @note QModelIndex holds a raw proxy pointer. Removed proxies are retained
 * until persistent indices have been remapped and layout listeners have returned.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerHierarchyCache
{
public:
    class Proxy
    {
    public:
        const SceneItemId& item() const { return m_item; }
        const SceneItemId& parent_item() const { return m_parent; }
        const SceneItemList& children() const { return m_children; }
        bool children_loaded() const { return m_children_loaded; }
        std::optional<bool> child_hint(bool allow_stale = false) const;

    private:
        explicit Proxy(SceneItemId item)
            : m_item(std::move(item))
        {
        }

        SceneItemId m_item;
        // Preserve the old parent until Qt has received the backend's structural change.
        SceneItemId m_parent;
        SceneItemList m_children;
        std::vector<Proxy*> m_child_proxies;
        bool m_children_loaded = false;
        bool m_child_hint_known = false;
        std::optional<bool> m_has_children;
        // Avoid sibling scans in parent(); row_of() repairs this after reordering.
        mutable std::size_t m_row = 0;

        friend class SceneOutlinerHierarchyCache;
    };

    SceneOutlinerHierarchyCache(const ISceneOutlinerDataSource* source, SceneItemId root);

    Proxy* root() const { return m_root.get(); }
    bool is_root(const Proxy* proxy) const;
    bool contains(const SceneItemId& item) const;

    Proxy* parent_of(const Proxy* child) const;
    std::size_t row_of(const Proxy* child) const;
    std::size_t child_count(Proxy* proxy);
    Proxy* child_at(Proxy* proxy, std::size_t index);
    Proxy* proxy_for(const SceneItemId& item) const;

    /// Create item's proxy chain through source parent links. Return nullptr outside the root.
    Proxy* materialize(const SceneItemId& item, const std::function<bool(Proxy*)>& load_children = {});
    Proxy* materialize(const SceneItemId& item, const std::function<bool(Proxy*)>& load_children, bool* awaiting_rows);

    /// Reconcile cached children and subtrees; ignore uncached parents.
    void resync(const SceneItemList& parents);

    using ChildLists = std::unordered_map<SceneItemId, SceneItemList, SceneItemId::Hash>;
    void resync(const SceneItemList& parents, ChildLists child_lists);

    bool is_valid() const { return m_valid; }
    void invalidate() { m_valid = false; }
    scene_outliner::CancellationToken hierarchy_token() const { return m_hierarchy_token; }
    void invalidate_queries() { m_hierarchy_token.restart(); }

    std::shared_ptr<Proxy> retain_proxy(const SceneItemId& item) const;
    SceneItemSet cached_items() const;
    void for_each_proxy(const std::function<void(const Proxy&)>& visit) const;
    std::optional<SceneItemList> read_children(const SceneItemId& item) const;
    std::optional<bool> read_child_hint(Proxy* proxy);
    void invalidate_child_hints(const SceneItemSet& items);

    bool ensure_children_loaded(Proxy* proxy);
    void append_children(Proxy* proxy, SceneItemList children);
    void apply_child_rows(Proxy* proxy, SceneItemList children, std::size_t first, std::size_t removed, std::size_t inserted);
    std::vector<std::shared_ptr<Proxy>> take_retired_proxies();

private:
    using ProxyPtr = std::shared_ptr<Proxy>;
    using ProxyMap = std::unordered_map<SceneItemId, ProxyPtr, SceneItemId::Hash>;
    using ProxyList = std::vector<ProxyPtr>;

    ProxyPtr register_item(const SceneItemId& item);
    void set_children(Proxy* proxy, SceneItemList children);
    void invalidate_subtrees(SceneItemList items, ChildLists& child_lists, SceneItemSet& refreshed);
    bool resync_children(Proxy* proxy, SceneItemList& pending, ChildLists& child_lists, SceneItemSet& refreshed);
    void delete_subtree(const SceneItemId& item);

    QPointer<const ISceneOutlinerDataSource> m_source;
    bool m_valid = true;
    scene_outliner::CancellationToken m_hierarchy_token;
    ProxyPtr m_root;
    ProxyMap m_item_to_proxy;
    ProxyList m_retired;
};

OPENDCC_NAMESPACE_CLOSE
