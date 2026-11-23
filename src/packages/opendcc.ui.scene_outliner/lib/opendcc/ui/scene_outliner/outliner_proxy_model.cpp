/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/outliner_proxy_model.h"

#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/tree_data_source.h"
#include <QPersistentModelIndex>
#include <QBrush>
#include <QColor>
#include <QScopedValueRollback>
#include <QTimer>
#include <QScopeGuard>
#include <utility>
#include <algorithm>

OPENDCC_NAMESPACE_OPEN

SceneOutlinerProxyModel::SceneOutlinerProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    // Search supplies ancestor membership without walking unopened branches.
    setRecursiveFilteringEnabled(false);
    setFilterRole(SceneOutlinerModel::SEARCH_FILTER);
}

QVariant SceneOutlinerProxyModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::BackgroundRole)
        if (auto* model = qobject_cast<SceneOutlinerModel*>(sourceModel()))
            if (m_highlighted.count(model->item_for_index(mapToSource(index))))
                return QBrush(QColor(150, 200, 250, 40));
    return QSortFilterProxyModel::data(index, role);
}

void SceneOutlinerProxyModel::set_highlighted(const SceneItemList& items)
{
    SceneItemSet next(items.begin(), items.end());
    if (next == m_highlighted)
        return;
    auto changed = m_highlighted;
    changed.insert(next.begin(), next.end());
    m_highlighted = std::move(next);
    const auto operation = m_highlight_token.restart();
    const QPointer<SceneOutlinerProxyModel> self(this);
    const QPointer<SceneOutlinerModel> model(qobject_cast<SceneOutlinerModel*>(sourceModel()));
    if (model)
        for (const auto& item : changed)
        {
            auto index = mapFromSource(model->index_for_item(item));
            if (index.isValid())
                Q_EMIT dataChanged(index, index.sibling(index.row(), columnCount() - 1), { Qt::BackgroundRole });
            if (!self || !model || sourceModel() != model || operation.is_cancelled())
                return;
        }
}

void SceneOutlinerProxyModel::set_search_filter(bool active, SceneItemSet accept)
{
    apply_search_filter(active, std::move(accept), {});
}

void SceneOutlinerProxyModel::apply_search_filter(bool active, SceneItemSet accept, SearchRows rows)
{
    if (m_search_active == active && m_accept == accept)
        return;
    const QPointer<SceneOutlinerProxyModel> self(this);
    const QPointer<SceneOutlinerModel> source(qobject_cast<SceneOutlinerModel*>(sourceModel()));
    const auto operation = m_search_token.restart();
    const auto is_current = [&]() {
        return self && source && sourceModel() == source && !operation.is_cancelled();
    };
    SceneItemList changed;
    constexpr size_t incremental_limit = 256;
    if (m_search_active && active && source && dynamicSortFilter())
    {
        for (const auto& item : accept)
        {
            if (!m_accept.count(item))
                changed.push_back(item);
            if (changed.size() > incremental_limit)
                break;
        }
        if (changed.size() <= incremental_limit)
            for (const auto& item : m_accept)
            {
                if (!accept.count(item))
                    changed.push_back(item);
                if (changed.size() > incremental_limit)
                    break;
            }
    }
    const bool incremental = m_search_active && active && source && dynamicSortFilter() && changed.size() <= incremental_limit;
    m_search_active = active;
    m_accept = std::move(accept);
    m_search_rows = std::move(rows);
    if (!incremental)
    {
        if (source)
        {
            // Qt may restore expanded rows synchronously while filtering.
            const bool previous = std::exchange(source->m_defer_child_hints, true);
            invalidateFilter();
            if (source)
                source->m_defer_child_hints = previous;
        }
        else
            invalidateFilter();
        return;
    }
    struct Update
    {
        QPersistentModelIndex index;
        int depth;
        bool added;
    };
    std::vector<Update> updates;
    for (const auto& item : changed)
    {
        const auto index = source->ensure_index_for_item(item);
        if (!is_current())
            return;
        if (!index.isValid())
            continue;
        int depth = 0;
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
            ++depth;
        updates.push_back({ index, depth, m_accept.count(item) != 0 });
    }
    // Parents must appear before their children, and disappear after them.
    std::stable_sort(updates.begin(), updates.end(), [](const Update& left, const Update& right) {
        if (left.added != right.added)
            return left.added;
        return left.added ? left.depth < right.depth : left.depth > right.depth;
    });
    for (const auto& update : updates)
    {
        if (update.index.isValid())
            Q_EMIT source->dataChanged(update.index, update.index, { SceneOutlinerModel::SEARCH_FILTER });
        if (!is_current())
            return;
    }
}

bool SceneOutlinerProxyModel::hasChildren(const QModelIndex& parent) const
{
    // Let expansion resolve filtered children; probing them here populates collapsed branches.
    return sourceModel() && sourceModel()->hasChildren(mapToSource(parent));
}

void SceneOutlinerProxyModel::setSourceModel(QAbstractItemModel* source)
{
    if (source == sourceModel())
        return;
    const QPointer<SceneOutlinerProxyModel> self(this);
    m_search_token.restart();
    m_highlight_token.restart();
    for (const auto& connection : m_filter_connections)
        disconnect(connection);
    m_filter_connections.clear();
    clear_filter_cache();
    if (source)
    {
        const auto tree_changed = [this]() {
            clear_filter_cache(true);
        };
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this]() { clear_filter_cache(); }));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::layoutAboutToBeChanged, this, tree_changed));
        auto* model = qobject_cast<SceneOutlinerModel*>(source);
        const auto appending = [model]() {
            return model && model->m_appending_child_rows;
        };
        if (model)
            m_filter_connections.push_back(connect(model, &SceneOutlinerModel::child_rows_preparing, this, [this](const SceneItemList& items) {
                if (!m_hidden_filters.isEmpty())
                    refresh_filter_items(SceneItemSet(items.begin(), items.end()));
            }));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this, [tree_changed, appending]() {
            if (!appending())
                tree_changed();
        }));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this, tree_changed));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::rowsAboutToBeMoved, this, tree_changed));
        const auto refresh = [this]() {
            schedule_filter_refresh();
        };
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::modelReset, this, refresh));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::layoutChanged, this, refresh));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::rowsInserted, this, [refresh, appending]() {
            if (!appending())
                refresh();
        }));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::rowsRemoved, this, refresh));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::rowsMoved, this, refresh));
        m_filter_connections.push_back(connect(source, &QAbstractItemModel::dataChanged, this,
                                               [this](const QModelIndex& first, const QModelIndex& last, const QVector<int>& roles) {
                                                   if (roles.size() == 1 && roles.front() == SceneOutlinerModel::SEARCH_FILTER)
                                                       return;
                                                   m_filter_token.restart();
                                                   m_search_rows.erase(first.parent().internalPointer());
                                                   m_parent_filters.clear();
                                                   auto* model = qobject_cast<SceneOutlinerModel*>(sourceModel());
                                                   if (!model || m_hidden_filters.isEmpty())
                                                       return;
                                                   SceneItemSet items;
                                                   for (int row = first.row(); row <= last.row(); ++row)
                                                       items.insert(model->item_for_index(first.sibling(row, 0)));
                                                   if (refresh_filter_items(items))
                                                       apply_category_filter();
                                               }));
    }
    QSortFilterProxyModel::setSourceModel(source);
    if (self)
        schedule_filter_refresh();
}

void SceneOutlinerProxyModel::set_hidden_filters(const QSet<QString>& hidden)
{
    if (m_hidden_filters == hidden)
        return;
    m_hidden_filters = hidden;
    clear_filter_cache();
    auto* model = qobject_cast<SceneOutlinerModel*>(sourceModel());
    if (!model || refresh_filter_items(model->filter_items()))
        apply_category_filter();
}

void SceneOutlinerProxyModel::apply_category_filter()
{
    if (m_filtering)
    {
        schedule_filter_refresh();
        return;
    }
    const QPointer<SceneOutlinerProxyModel> self(this);
    {
        m_filtering = true;
        const auto filtering = qScopeGuard([self]() {
            if (self)
                self->m_filtering = false;
        });
        Q_EMIT category_filter_about_to_change();
        if (!self)
            return;
        invalidateFilter();
    }
    if (self)
        Q_EMIT category_filter_changed();
}

void SceneOutlinerProxyModel::clear_filter_cache(bool keep_item_filters)
{
    m_filter_token.restart();
    m_search_rows.clear();
    m_parent_filters.clear();
    if (!keep_item_filters)
        m_item_filters.clear();
    m_pending_filters.clear();
}

bool SceneOutlinerProxyModel::refresh_filter_items(const SceneItemSet& items) const
{
    const QPointer<const SceneOutlinerProxyModel> self(this);
    const QPointer<SceneOutlinerModel> model(qobject_cast<SceneOutlinerModel*>(sourceModel()));
    const QPointer<ISceneOutlinerDataSource> source(model ? model->data_source() : nullptr);
    const auto operation = m_filter_token;
    const auto filters = m_hidden_filters;
    for (const auto& item : items)
    {
        bool hidden = false;
        for (const auto& filter : filters)
        {
            if (!source)
                break;
            const bool matched = source->matches_filter(item, filter.toStdString());
            if (!self || !model || !source || sourceModel() != model || model->data_source() != source || operation.is_cancelled())
                return false;
            if (matched)
            {
                hidden = true;
                break;
            }
        }
        m_item_filters[item] = hidden;
    }
    return true;
}

void SceneOutlinerProxyModel::schedule_filter_refresh(const SceneItemId& item) const
{
    if (m_hidden_filters.isEmpty())
        return;
    if (item)
        m_pending_filters.insert(item);
    else
        m_refresh_known_items = true;
    if (m_filter_refresh_scheduled)
        return;
    m_filter_refresh_scheduled = true;
    auto* self = const_cast<SceneOutlinerProxyModel*>(this);
    QTimer::singleShot(0, self, [self]() {
        self->m_filter_refresh_scheduled = false;
        auto items = std::exchange(self->m_pending_filters, {});
        if (std::exchange(self->m_refresh_known_items, false))
            if (auto* model = qobject_cast<SceneOutlinerModel*>(self->sourceModel()))
            {
                auto known = model->filter_items();
                for (auto item = self->m_item_filters.begin(); item != self->m_item_filters.end();)
                    if (!known.count(item->first))
                        item = self->m_item_filters.erase(item);
                    else
                        ++item;
                items.insert(known.begin(), known.end());
            }
        if (self->refresh_filter_items(items))
        {
            self->m_parent_filters.clear();
            self->apply_category_filter();
        }
    });
}

bool SceneOutlinerProxyModel::cached_hidden_item(const SceneItemId& item) const
{
    const auto cached = m_item_filters.find(item);
    if (cached != m_item_filters.end())
        return cached->second;
    // Qt retains proxy state while filtering. Query Python outside that call stack.
    schedule_filter_refresh(item);
    return true;
}

bool SceneOutlinerProxyModel::hidden_item(const SceneItemId& item, const SearchCheck& is_current) const
{
    const auto cached = m_item_filters.find(item);
    if (cached != m_item_filters.end())
        return cached->second;
    if (!refresh_filter_items({ item }) || (is_current && !is_current()))
        return true;
    return m_item_filters.at(item);
}

void SceneOutlinerProxyModel::prepare_search_row(int row, const QModelIndex& parent, bool active, const SceneItemSet& accept,
                                                 const SearchCheck& is_current, SearchRows& rows)
{
    const QPointer<SceneOutlinerProxyModel> self(this);
    const QPointer<SceneOutlinerModel> model(qobject_cast<SceneOutlinerModel*>(sourceModel()));
    if (!model)
        return;
    const auto item = model->item_at_row(row, parent);
    if (!self || !model || !is_current())
        return;
    bool accepted = item && (!active || accept.count(item));
    if (accepted && !m_hidden_filters.isEmpty())
    {
        accepted = !hidden_item(item, is_current);
        if (!self || !model || !is_current())
            return;
    }
    auto& siblings = rows[parent.internalPointer()];
    siblings.push_back(accepted);
    if (!accepted)
        return;
    model->index(row, 0, parent);
}

bool SceneOutlinerProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    // Avoid item lookup when no filters are active.
    if (!m_search_active && m_hidden_filters.isEmpty())
        return true;

    auto* model = qobject_cast<SceneOutlinerModel*>(sourceModel());
    // Reuse the decisions prepared in GUI batches during Qt's synchronous filter pass.
    const auto prepared = m_search_rows.find(source_parent.internalPointer());
    const bool cached = prepared != m_search_rows.end() && source_row >= 0 && static_cast<size_t>(source_row) < prepared->second.size();
    if (cached)
    {
        if (!prepared->second[source_row])
            return false;
    }
    else
    {
        // Rejected search rows need identities, not allocated QModelIndex proxies.
        const SceneItemId item = model ? model->item_at_row(source_row, source_parent) : SceneItemId();
        if (m_search_active && (!item || m_accept.find(item) == m_accept.end()))
            return false;
        if (!m_hidden_filters.isEmpty() && model && cached_hidden_item(item))
            return false;
    }

    if (!m_hidden_filters.isEmpty() && model)
    {
        if (auto* source = model->data_source())
        {
            if (!source_parent.isValid())
                return true;
            const auto key = source_parent.internalPointer();
            const auto cached = m_parent_filters.find(key);
            if (cached != m_parent_filters.end())
                return !cached->second;
            bool hidden = false;
            for (auto parent = source_parent; parent.isValid(); parent = parent.parent())
            {
                const auto ancestor = model->item_for_index(parent);
                const bool blocked = cached_hidden_item(ancestor);
                if (blocked)
                {
                    hidden = true;
                    break;
                }
            }
            m_parent_filters[key] = hidden;
            return !hidden;
        }
    }
    return true;
}

OPENDCC_NAMESPACE_CLOSE
