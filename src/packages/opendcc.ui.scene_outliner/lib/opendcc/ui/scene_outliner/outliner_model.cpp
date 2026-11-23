/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/outliner_model.h"

#include "opendcc/base/logging/logger.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QMimeData>
#include <QPersistentModelIndex>
#include <QUuid>
#include <QTimer>
#include <QDataStream>
#include <QIODevice>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <algorithm>
#include <utility>

OPENDCC_NAMESPACE_OPEN

using Proxy = SceneOutlinerHierarchyCache::Proxy;

namespace
{
    constexpr std::size_t child_batch_size = 1024;
    constexpr qint64 filter_preparation_budget_ns = 4000000;
    const QString internal_drag_mime = QStringLiteral("application/x-opendcc-scene-outliner-drag");
}

void SceneOutlinerModel::begin_update()
{
    Q_ASSERT(!m_updating);
    m_updating = true;
    m_update_interrupted = false;
}

void SceneOutlinerModel::finish_update()
{
    m_updating = false;
    schedule_pending_tree_updates();
}

SceneOutlinerModel::SceneOutlinerModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    m_source_context = new QObject(this);
}

SceneOutlinerModel::~SceneOutlinerModel()
{
    delete m_source_context.data();
    if (m_cache)
        m_cache->invalidate();
}

void SceneOutlinerModel::set_data_source(ISceneOutlinerDataSource* source)
{
    if (source && source->thread() != thread())
    {
        OPENDCC_ERROR("The data source must belong to the model thread.");
        return;
    }
    if (m_updating)
    {
        m_pending_source = source;
        m_pending_source_clear = !source;
        Q_EMIT data_source_changed();
        return;
    }
    const bool canceled_pending = m_pending_source.has_value();
    m_pending_source.reset();
    if (source && source == m_source)
    {
        if (canceled_pending)
            Q_EMIT data_source_changed();
        return;
    }

    const QPointer<SceneOutlinerModel> self(this);
    const QPointer<ISceneOutlinerDataSource> replacement(source);
    const auto source_context = m_source_context;
    {
        begin_update();
        const auto update = qScopeGuard([self]() {
            if (self)
                self->finish_update();
        });
        Q_EMIT source_about_to_change(true);
        if (!self)
            return;
        if (source_context != m_source_context || (source && (!replacement || replacement->thread() != thread())))
        {
            Q_EMIT source_changed(true);
            return;
        }
        beginResetModel();
        if (!self)
            return;
        if (source_context != m_source_context || (source && (!replacement || replacement->thread() != thread())))
        {
            endResetModel();
            if (self)
                Q_EMIT source_changed(true);
            return;
        }
        m_pending_tree_updates.source_changes.clear();
        delete m_source_context.data();
        m_source_context = new QObject(this);
        m_source = replacement;
        const auto context = m_source_context;
        rebuild();
        if (!self)
            return;
        if (context && context == m_source_context && m_source)
        {
            connect(m_source, &ISceneOutlinerDataSource::changed, context, [this](const SceneOutlinerChange& change) { on_source_changed(change); });
            connect(m_source, &QObject::destroyed, context, [this]() {
                if (!pending_data_source())
                    set_data_source(nullptr);
            });
        }
        endResetModel();
    }
    if (!self)
        return;
    Q_EMIT data_source_changed();
    if (self)
        Q_EMIT source_changed(true);
}

void SceneOutlinerModel::apply_pending_data_source()
{
    if (m_updating || !m_pending_source)
        return;
    const auto source = *m_pending_source;
    m_pending_source.reset();
    if (!source && !m_pending_source_clear && m_source)
    {
        Q_EMIT data_source_changed();
        return;
    }
    const QPointer<SceneOutlinerModel> self(this);
    set_data_source(source);
    if (self && source != m_source && !m_pending_source)
    {
        if (!m_source)
            set_data_source(nullptr);
        else
            Q_EMIT data_source_changed();
    }
}

void SceneOutlinerModel::rebuild()
{
    m_pending_tree_updates.child_queries.clear();
    m_child_loads.clear();
    const QPointer<SceneOutlinerModel> self(this);
    const auto source = m_source;
    const auto context = m_source_context;
    auto columns = source ? source->columns() : SceneOutlinerColumnList();
    if (!self || context != m_source_context || source != m_source)
        return;
    auto roots = source ? source->roots() : SceneItemList();
    if (!self || context != m_source_context || source != m_source)
        return;
    m_columns = source ? std::move(columns) : SceneOutlinerColumnList();
    m_tree_column = 0;
    for (int i = 0; i < static_cast<int>(m_columns.size()); ++i)
    {
        if (m_columns[i].role == SceneOutlinerColumn::Role::TREE)
        {
            m_tree_column = i;
            break;
        }
    }
    m_drag_token.clear();
    m_drag_items.clear();
    if (m_cache)
        m_cache->invalidate();
    m_cache.reset();
    m_root_item = SceneItemId();
    if (!source || roots.empty())
        return;

    m_root_item = roots.size() == 1 ? roots.front() : SceneItemId();
    m_cache = std::make_shared<SceneOutlinerHierarchyCache>(m_source, m_root_item);
    if (!m_root_item)
        m_cache->append_children(m_cache->root(), std::move(roots));
}

bool SceneOutlinerModel::has_source() const
{
    return m_source && m_cache;
}

void SceneOutlinerModel::queue_child_query(const SceneItemId& item, ChildQuery query) const
{
    const auto pending = m_pending_tree_updates.child_queries.try_emplace(item, query).first;
    if (query == ChildQuery::LOAD_CHILDREN)
        pending->second = query;
    schedule_pending_tree_updates();
}

std::optional<bool> SceneOutlinerModel::child_hint_for_paint(const QModelIndex& index) const
{
    const auto* proxy = proxy_from_index(index);
    if (!proxy || !m_source)
        return false;
    const auto hint = proxy->child_hint();
    if (hint)
        return hint;
    queue_child_query(proxy->item(), ChildQuery::HINT);
    return proxy->child_hint(true);
}

void SceneOutlinerModel::schedule_pending_tree_updates() const
{
    auto& pending = m_pending_tree_updates;
    if (m_updating || pending.scheduled || (pending.source_changes.empty() && pending.child_queries.empty() && !m_pending_source))
        return;
    pending.scheduled = true;
    auto* model = const_cast<SceneOutlinerModel*>(this);
    QTimer::singleShot(0, model, &SceneOutlinerModel::apply_pending_tree_updates);
}

void SceneOutlinerModel::apply_pending_tree_updates()
{
    const QPointer<SceneOutlinerModel> self(this);
    const auto finish = qScopeGuard([self]() {
        if (self)
        {
            self->m_pending_tree_updates.scheduled = false;
            self->schedule_pending_tree_updates();
        }
    });
    if (m_updating)
        return;

    const auto context = m_source_context;
    const bool replace_source = m_pending_source.has_value();
    const auto notices = std::exchange(m_pending_tree_updates.source_changes, {});
    for (const auto& change : notices)
    {
        on_source_changed(change);
        if (!self || context != m_source_context)
            return;
    }
    if (replace_source)
    {
        apply_pending_data_source();
        if (!self || context != m_source_context)
            return;
    }
    if (!m_pending_source && m_pending_tree_updates.source_changes.empty())
        apply_pending_child_queries();
}

void SceneOutlinerModel::apply_pending_child_queries()
{
    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    if (!cache || !cache->is_valid())
    {
        m_pending_tree_updates.child_queries.clear();
        return;
    }

    const auto queries = std::exchange(m_pending_tree_updates.child_queries, {});
    bool loaded_children = false;
    for (const auto& query : queries)
    {
        if (!self || cache != m_cache || !cache->is_valid())
            return;
        const bool load = query.second == ChildQuery::LOAD_CHILDREN;
        auto* proxy = cache->proxy_for(query.first);
        if (!proxy || (proxy->children_loaded() && (!load || !m_child_loads.count(query.first))))
            continue;
        if (m_pending_source || !m_pending_tree_updates.source_changes.empty() || (load && loaded_children))
        {
            queue_child_query(query.first, query.second);
            continue;
        }
        if (load)
        {
            loaded_children = true;
            load_children(proxy, false);
        }
        else if (!proxy->child_hint())
        {
            const auto displayed_hint = proxy->child_hint(true);
            const auto hint = cache->read_child_hint(proxy);
            if (!self || cache != m_cache || !hint)
                return;
            if (hint != displayed_hint)
                Q_EMIT dataChanged(index_for_item(query.first), index_for_item(query.first, columnCount() - 1));
        }
    }
}

bool SceneOutlinerModel::load_children(Proxy* proxy, bool complete)
{
    const SceneItemId item = proxy->item();
    const auto existing = m_child_loads.find(item);
    const bool needs_query = existing == m_child_loads.end();
    if (needs_query && proxy->children_loaded())
        return true;
    if (m_updating || m_update_interrupted)
    {
        queue_child_query(item, ChildQuery::LOAD_CHILDREN);
        return false;
    }
    if (m_pending_source || !m_source)
        return false;

    const auto load = needs_query ? m_child_loads.emplace(item, std::make_shared<ChildLoad>()).first->second : existing->second;
    if (load->active)
        return false;
    load->active = true;
    const auto release = qScopeGuard([load]() { load->active = false; });

    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto retained = cache->retain_proxy(item);
    const auto operation = cache->hierarchy_token();
    const auto is_current = [&]() {
        return self && cache == m_cache && cache->is_valid() && m_source && !m_pending_source && !m_update_interrupted && !operation.is_cancelled();
    };
    if (needs_query)
    {
        auto children = cache->read_children(item);
        if (!children)
            return false;
        load->children = std::move(*children);
    }
    if (!is_current())
        return false;

    const auto remaining = load->children.size() - load->next;
    const auto batch_size = std::max(child_batch_size, load->batch_size);
    const auto count = complete ? remaining : std::min(remaining, batch_size);
    const bool batched = load->next != 0 || count < remaining;
    const auto first = load->children.begin() + load->next;
    SceneItemList rows(first, first + count);
    Q_EMIT source_about_to_change(false);
    if (is_current() && batched)
    {
        QElapsedTimer preparation;
        preparation.start();
        Q_EMIT child_rows_preparing(rows);
        if (count < remaining)
        {
            // Larger batches limit repeated Qt layouts. Expensive filters need shorter turns.
            const auto elapsed_ns = std::max<qint64>(1, preparation.nsecsElapsed());
            const auto budgeted_rows = static_cast<qint64>(count) * filter_preparation_budget_ns / elapsed_ns;
            load->batch_size = static_cast<std::size_t>(std::min<qint64>(load->next + count, budgeted_rows));
        }
    }
    if (!is_current())
    {
        if (self)
            Q_EMIT source_changed(false);
        return false;
    }

    const auto index = index_for_item(item);
    {
        begin_update();
        const auto update = qScopeGuard([self]() {
            if (self)
            {
                self->m_appending_child_rows = false;
                self->finish_update();
            }
        });
        const int first_row = static_cast<int>(proxy->children().size());
        m_appending_child_rows = batched;
        if (count)
            beginInsertRows(index, first_row, first_row + static_cast<int>(count) - 1);
        if (!self)
            return false;
        cache->append_children(proxy, std::move(rows));
        load->next += count;
        if (count)
            endInsertRows();
        else
            Q_EMIT dataChanged(index, index);
    }
    if (!self)
        return false;
    load->active = false;
    if (load->next == load->children.size())
        m_child_loads.erase(item);
    else if (is_current())
        queue_child_query(item, ChildQuery::LOAD_CHILDREN);
    Q_EMIT source_changed(false);
    return is_current();
}

void SceneOutlinerModel::on_source_changed(const SceneOutlinerChange& change)
{
    if (m_updating)
    {
        if (change.type != SceneOutlinerChange::Type::ITEMS_CHANGED)
            m_update_interrupted = true;
        m_pending_tree_updates.source_changes.push_back(change);
        return;
    }
    if ((change.type == SceneOutlinerChange::Type::ITEMS_ADDED || change.type == SceneOutlinerChange::Type::ITEMS_REMOVED) &&
        (!m_cache || m_root_item) && std::find(change.parents.begin(), change.parents.end(), SceneItemId()) != change.parents.end())
    {
        on_source_changed(SceneOutlinerChange::reset());
        return;
    }
    const QPointer<SceneOutlinerModel> self(this);
    const auto context = m_source_context;
    const auto retained_cache = m_cache;
    const bool affects_search = change.type != SceneOutlinerChange::Type::ITEMS_CHANGED || change.affects_search;
    if (m_cache && change.type != SceneOutlinerChange::Type::ITEMS_CHANGED)
        m_cache->invalidate_queries();
    {
        begin_update();
        const auto update = qScopeGuard([self]() {
            if (self)
                self->finish_update();
        });
        Q_EMIT source_about_to_change(affects_search);
        if (!self)
            return;
        if (context != m_source_context)
        {
            Q_EMIT source_changed(affects_search);
            return;
        }
        switch (change.type)
        {
        case SceneOutlinerChange::Type::RESET:
            beginResetModel();
            if (!self)
                return;
            rebuild();
            if (!self)
                return;
            endResetModel();
            break;
        case SceneOutlinerChange::Type::ITEMS_ADDED:
        case SceneOutlinerChange::Type::ITEMS_REMOVED:
        {
            SceneItemList parents = change.parents;
            for (const auto& entry : m_child_loads)
            {
                const auto* proxy = m_cache ? m_cache->proxy_for(entry.first) : nullptr;
                if (!proxy)
                    continue;
                if (proxy->children_loaded())
                {
                    if (std::find(parents.begin(), parents.end(), entry.first) == parents.end())
                        parents.push_back(entry.first);
                }
                else if (m_defer_child_queries)
                    queue_child_query(entry.first, ChildQuery::LOAD_CHILDREN);
            }
            m_child_loads.clear();
            resync_subtrees(parents);
            break;
        }
        case SceneOutlinerChange::Type::ITEMS_CHANGED:
            emit_items_changed(change.items);
            break;
        }
    }
    if (self)
        Q_EMIT source_changed(affects_search);
}

Proxy* SceneOutlinerModel::proxy_from_index(const QModelIndex& index) const
{
    return index.isValid() && index.model() == this ? static_cast<Proxy*>(index.internalPointer()) : nullptr;
}

SceneItemId SceneOutlinerModel::item_from_index(const QModelIndex& index) const
{
    const Proxy* proxy = proxy_from_index(index);
    return proxy ? proxy->item() : SceneItemId();
}

SceneItemId SceneOutlinerModel::item_for_index(const QModelIndex& index) const
{
    return item_from_index(index);
}

SceneItemId SceneOutlinerModel::item_at_row(int row, const QModelIndex& parent) const
{
    if (!m_cache || row < 0)
        return {};
    if (!parent.isValid())
    {
        if (m_root_item)
            return row == 0 ? m_root_item : SceneItemId();
        const auto& roots = m_cache->root()->children();
        return static_cast<size_t>(row) < roots.size() ? roots[row] : SceneItemId();
    }
    auto* proxy = proxy_from_index(parent);
    if (!proxy || parent.column() != 0)
        return {};
    if (m_defer_child_queries && !proxy->children_loaded())
    {
        queue_child_query(proxy->item(), ChildQuery::LOAD_CHILDREN);
        return {};
    }
    const QPointer<const SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    if (!cache->ensure_children_loaded(proxy) || !self || cache != m_cache)
        return {};
    return static_cast<size_t>(row) < proxy->children().size() ? proxy->children()[row] : SceneItemId();
}

QModelIndex SceneOutlinerModel::index_for_item(const SceneItemId& item, int column) const
{
    if (!m_cache || !item || column < 0 || column >= columnCount())
        return {};
    Proxy* proxy = m_cache->proxy_for(item);
    if (!proxy)
        return {};
    if (m_cache->is_root(proxy))
        return createIndex(0, column, proxy);
    return createIndex(static_cast<int>(m_cache->row_of(proxy)), column, proxy);
}

QModelIndex SceneOutlinerModel::ensure_index_for_item(const SceneItemId& item, int column)
{
    return materialize_index(item, column, nullptr);
}

QModelIndex SceneOutlinerModel::materialize_index(const SceneItemId& item, int column, bool* awaiting_rows)
{
    if (!has_source() || !item)
        return {};
    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto load = [this](Proxy* proxy) {
        return load_children(proxy);
    };
    if (!cache->materialize(item, m_defer_child_queries ? load : std::function<bool(Proxy*)>(), awaiting_rows) || !self || cache != m_cache)
        return {};
    return index_for_item(item, column);
}

QModelIndex SceneOutlinerModel::index(int row, int column, const QModelIndex& parent_index) const
{
    if (!m_cache || row < 0 || column < 0 || column >= columnCount())
        return {};

    if (!parent_index.isValid())
    {
        if (m_root_item)
            return row == 0 ? createIndex(0, column, m_cache->root()) : QModelIndex();
        auto* root = m_cache->child_at(m_cache->root(), static_cast<size_t>(row));
        return root ? createIndex(row, column, root) : QModelIndex();
    }

    Proxy* parent_proxy = proxy_from_index(parent_index);
    if (!parent_proxy || parent_index.column() != 0)
        return {};
    if (m_defer_child_queries && !parent_proxy->children_loaded())
    {
        queue_child_query(parent_proxy->item(), ChildQuery::LOAD_CHILDREN);
        return {};
    }
    const QPointer<const SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    Proxy* child = cache->child_at(parent_proxy, static_cast<std::size_t>(row));
    return self && child && cache == m_cache ? createIndex(row, column, child) : QModelIndex();
}

QModelIndex SceneOutlinerModel::parent(const QModelIndex& child) const
{
    if (!m_cache || !child.isValid())
        return {};

    Proxy* child_proxy = proxy_from_index(child);
    if (!child_proxy || m_cache->is_root(child_proxy))
        return {};

    Proxy* parent_proxy = m_cache->parent_of(child_proxy);
    if (!parent_proxy)
        return {};
    return createIndex(static_cast<int>(m_cache->row_of(parent_proxy)), 0, parent_proxy);
}

int SceneOutlinerModel::rowCount(const QModelIndex& parent_index) const
{
    // Source destruction defers the clear; cached rows must remain readable through endInsertRows/endRemoveRows.
    if (!m_cache)
        return 0;
    if (!parent_index.isValid())
        return m_root_item ? 1 : static_cast<int>(m_cache->root()->children().size());
    if (parent_index.column() != 0)
        return 0;
    Proxy* parent_proxy = proxy_from_index(parent_index);
    if (m_defer_child_queries && parent_proxy && !parent_proxy->children_loaded())
    {
        queue_child_query(parent_proxy->item(), ChildQuery::LOAD_CHILDREN);
        return 0;
    }
    const QPointer<const SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto count = parent_proxy ? cache->child_count(parent_proxy) : 0;
    return self && cache == m_cache ? static_cast<int>(count) : 0;
}

int SceneOutlinerModel::columnCount(const QModelIndex&) const
{
    return static_cast<int>(m_columns.size());
}

bool SceneOutlinerModel::hasChildren(const QModelIndex& parent) const
{
    if (!m_cache || columnCount() == 0)
        return false;
    if (!parent.isValid())
        return m_root_item || !m_cache->root()->children().empty();
    if (parent.column() != 0)
        return false;
    auto* proxy = proxy_from_index(parent);
    if (!proxy)
        return false;
    if (const auto hint = proxy->child_hint())
        return *hint;
    if (!m_source)
        return false;
    if (m_defer_child_hints)
        return true;
    if (m_defer_child_queries)
    {
        queue_child_query(proxy->item(), ChildQuery::HINT);
        return true;
    }

    const QPointer<const SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto hint = cache->read_child_hint(proxy);
    return self && cache == m_cache && hint.value_or(false);
}

bool SceneOutlinerModel::canFetchMore(const QModelIndex& parent) const
{
    if (!has_source() || m_updating || m_update_interrupted || m_pending_source || parent.column() != 0)
        return false;
    const auto* proxy = proxy_from_index(parent);
    if (!proxy || proxy->children_loaded() || proxy->child_hint() == false || m_child_loads.count(proxy->item()))
        return false;
    const auto& queries = m_pending_tree_updates.child_queries;
    const auto pending = queries.find(proxy->item());
    return pending == queries.end() || pending->second == ChildQuery::HINT;
}

void SceneOutlinerModel::fetchMore(const QModelIndex& parent)
{
    if (!canFetchMore(parent))
        return;
    auto* proxy = proxy_from_index(parent);
    if (m_defer_child_queries)
        queue_child_query(proxy->item(), ChildQuery::LOAD_CHILDREN);
    else
        load_children(proxy);
}

QVariant SceneOutlinerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !has_source())
        return {};
    const SceneItemId item = item_from_index(index);
    if (!item)
        return {};

    if (role == ITEM_TYPE_ID)
        return QString::fromStdString(item.type_id());
    if (index.column() < 0 || index.column() >= static_cast<int>(m_columns.size()))
        return {};
    const SceneOutlinerColumn column = m_columns[index.column()];

    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        if (column.role == SceneOutlinerColumn::Role::TOGGLE)
            return {};
        return QString::fromStdString(m_source->column_text(item, column.id));
    }

    if (role == Qt::DecorationRole)
    {
        if (column.role == SceneOutlinerColumn::Role::TREE)
            return m_source->icon(item);
        if (column.role == SceneOutlinerColumn::Role::TOGGLE)
        {
            const QPointer<const SceneOutlinerModel> self(this);
            const auto source = m_source;
            const auto cache = m_cache;
            const auto operation = cache->hierarchy_token();
            const auto is_current = [&]() {
                return self && source && source == m_source && cache == m_cache && !operation.is_cancelled();
            };
            const bool on = source->toggle_state(item, column.id);
            if (!is_current())
                return {};
            QString icon = on ? column.icon_on : column.icon_off;
            if (icon.isEmpty())
                return {};
            const bool enabled = source->toggle_enabled(item, column.id);
            if (!is_current())
                return {};
            if (!enabled)
                icon += QStringLiteral("_faded");
            return QIcon(icon);
        }
    }

    if (role == Qt::FontRole)
    {
        const int hints = m_source->display_hints(item);
        if (hints & (ISceneOutlinerDataSource::ITALIC | ISceneOutlinerDataSource::STRIKEOUT))
        {
            QFont font;
            font.setItalic(hints & ISceneOutlinerDataSource::ITALIC);
            font.setStrikeOut(hints & ISceneOutlinerDataSource::STRIKEOUT);
            return font;
        }
    }

    if (role == Qt::ForegroundRole && (m_source->display_hints(item) & ISceneOutlinerDataSource::DIMMED))
        return QColor(120, 120, 120);

    if (role == Qt::ToolTipRole)
        return QString::fromStdString(m_source->tooltip(item));

    return {};
}

SceneItemSet SceneOutlinerModel::filter_items() const
{
    return m_cache ? m_cache->cached_items() : SceneItemSet {};
}

QVariant SceneOutlinerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= static_cast<int>(m_columns.size()))
        return QAbstractItemModel::headerData(section, orientation, role);

    const SceneOutlinerColumn& column = m_columns[section];
    if (role == Qt::DisplayRole)
        return column.header_icon.isEmpty() ? column.title : QString();
    if (role == Qt::DecorationRole && !column.header_icon.isEmpty())
        return QIcon(column.header_icon);
    return QAbstractItemModel::headerData(section, orientation, role);
}

Qt::ItemFlags SceneOutlinerModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || index.model() != this || !has_source())
        return Qt::NoItemFlags;
    // Toggle cells are flipped by a click in the view, not edited inline.
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const SceneItemId item = item_from_index(index);
    if (item && m_source)
    {
        const QPointer<const SceneOutlinerModel> self(this);
        const auto source = m_source;
        const auto cache = m_cache;
        const auto operation = cache->hierarchy_token();
        const auto is_current = [&]() {
            return self && source && source == m_source && cache == m_cache && !operation.is_cancelled();
        };
        if (index.column() >= 0 && index.column() < static_cast<int>(m_columns.size()))
        {
            const SceneOutlinerColumn column = m_columns[index.column()];
            if (column.editable && (column.role != SceneOutlinerColumn::Role::TREE || source->can_rename(item)))
                result |= Qt::ItemIsEditable;
            if (!is_current())
                return Qt::NoItemFlags;
        }
        const bool can_reparent = source->can_reparent();
        if (!is_current())
            return Qt::NoItemFlags;
        if (can_reparent)
            result |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        else
        {
            const auto mime_type = source->drag_mime_type();
            if (!is_current())
                return Qt::NoItemFlags;
            if (!mime_type.isEmpty())
                result |= Qt::ItemIsDragEnabled;
        }
    }
    return result;
}

bool SceneOutlinerModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || !has_source() || role != Qt::EditRole || index.column() < 0 || index.column() >= static_cast<int>(m_columns.size()))
        return false;
    const SceneItemId item = item_from_index(index);
    if (!item)
        return false;
    const SceneOutlinerColumn column = m_columns[index.column()];
    if (!column.editable)
        return false;

    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto operation = cache->hierarchy_token();
    const QPersistentModelIndex edited(index);
    if (!m_source->set_column_text(item, column.id, value.toString().toStdString()))
        return false;
    if (self && cache == m_cache && !operation.is_cancelled() && edited.isValid())
        Q_EMIT dataChanged(edited.sibling(edited.row(), 0), edited.sibling(edited.row(), columnCount() - 1));
    return true;
}

QStringList SceneOutlinerModel::mimeTypes() const
{
    QStringList types = QAbstractItemModel::mimeTypes();
    if (m_source)
    {
        const QString custom = m_source->drag_mime_type();
        if (!custom.isEmpty() && !types.contains(custom))
            types.push_front(custom);
    }
    return types;
}

QMimeData* SceneOutlinerModel::mimeData(const QModelIndexList& indexes) const
{
    m_drag_token.clear();
    m_drag_items.clear();
    if (indexes.empty() || !has_source())
        return nullptr;
    const QPointer<const SceneOutlinerModel> self(this);
    const auto source = m_source;
    const auto cache = m_cache;
    const auto operation = cache->hierarchy_token();
    const auto token = QUuid::createUuid().toByteArray();
    m_drag_token = token;
    const auto is_current = [&]() {
        return self && source && source == m_source && cache == m_cache && !operation.is_cancelled() && token == m_drag_token;
    };
    QList<QPersistentModelIndex> cells;
    SceneItemList items;
    for (const QModelIndex& idx : indexes)
    {
        if (!idx.isValid() || idx.model() != this)
            continue;
        cells.push_back(idx);
        if (idx.column() == 0)
            items.push_back(item_from_index(idx));
    }
    const QString custom = source->drag_mime_type();
    if (!is_current())
        return nullptr;
    const auto custom_data = !custom.isEmpty() && !items.empty() ? source->drag_mime_data(items) : QByteArray();
    if (!is_current())
        return nullptr;

    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    for (const auto& cell : cells)
    {
        QMap<int, QVariant> roles;
        for (int role = 0; role < Qt::UserRole; ++role)
        {
            const auto value = data(cell, role);
            if (!is_current() || !cell.isValid())
                return nullptr;
            if (value.isValid())
                roles.insert(role, value);
        }
        // Match Qt's item-model payload without running callbacks inside its serializer.
        stream << cell.row() << cell.column() << roles;
    }
    if (!is_current())
        return nullptr;
    auto* mime = new QMimeData;
    mime->setData(QAbstractItemModel::mimeTypes().front(), encoded);
    m_drag_items = std::move(items);
    if (!m_drag_items.empty())
        mime->setData(internal_drag_mime, token);
    if (!custom.isEmpty() && !m_drag_items.empty())
        mime->setData(custom, custom_data);
    return mime;
}

SceneItemList SceneOutlinerModel::items_for_mime_data(const QMimeData* mime) const
{
    if (!mime || m_drag_token.isEmpty() || mime->data(internal_drag_mime) != m_drag_token || !m_source)
        return {};
    const QPointer<const SceneOutlinerModel> self(this);
    const auto source = m_source;
    const auto token = m_drag_token;
    const auto items = m_drag_items;
    for (const auto& item : items)
    {
        const bool exists = source->has_item(item);
        if (!self || !source || source != m_source || token != m_drag_token || !exists || item == m_root_item)
            return {};
    }
    return items;
}

bool SceneOutlinerModel::is_toggle_column(int column) const
{
    return column >= 0 && column < static_cast<int>(m_columns.size()) && m_columns[column].role == SceneOutlinerColumn::Role::TOGGLE;
}

void SceneOutlinerModel::toggle_column(const QModelIndex& index)
{
    if (!has_source() || !index.isValid() || !is_toggle_column(index.column()))
        return;
    const SceneItemId item = item_from_index(index);
    if (!item)
        return;
    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto operation = cache->hierarchy_token();
    const QPersistentModelIndex toggled(index);
    const auto column = m_columns[index.column()];
    const auto source = m_source;
    if (!column.interactive)
        return;
    const bool enabled = source->toggle_enabled(item, column.id);
    if (!self || !source || source != m_source || cache != m_cache || operation.is_cancelled() || !toggled.isValid() || !enabled)
        return;
    source->toggle_column(item, column.id);
    // Synchronous adapters may apply edits without emitting a change notice.
    if (self && cache == m_cache && !operation.is_cancelled() && toggled.isValid())
        Q_EMIT dataChanged(toggled, toggled, { Qt::DecorationRole });
}

void SceneOutlinerModel::resync_subtrees(const SceneItemList& parents)
{
    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    if (!has_source() || parents.empty())
        return;
    const auto operation = cache->hierarchy_token();
    const auto is_current = [&]() {
        return self && cache == m_cache && cache->is_valid() && !operation.is_cancelled();
    };
    SceneOutlinerHierarchyCache::ChildLists child_lists;
    if (resync_rows(parents, child_lists))
    {
        if (is_current())
            emit_items_changed(parents);
        return;
    }

    if (!is_current() || !m_source)
        return;
    QList<QPersistentModelIndex> affected;
    SceneItemSet seen;
    for (const auto& parent : parents)
    {
        const auto index = index_for_item(parent);
        if (index.isValid() && seen.insert(parent).second)
            affected.push_back(index);
    }
    const bool root_change = !m_root_item && std::find(parents.begin(), parents.end(), SceneItemId()) != parents.end();
    if (affected.empty() && !root_change)
        return;
    Q_EMIT layoutAboutToBeChanged(affected, QAbstractItemModel::VerticalSortHint);
    if (!is_current())
        return;
    if (m_update_interrupted)
    {
        // Retry with the new notice so a move can update both parents in one layout.
        auto& pending = m_pending_tree_updates.source_changes;
        const auto next = std::find_if(pending.begin(), pending.end(),
                                       [](const SceneOutlinerChange& change) { return change.type != SceneOutlinerChange::Type::ITEMS_CHANGED; });
        if (next != pending.end() && next->type != SceneOutlinerChange::Type::RESET)
            next->parents.insert(next->parents.end(), parents.begin(), parents.end());
        Q_EMIT layoutChanged(affected, QAbstractItemModel::VerticalSortHint);
        return;
    }
    // Listeners may create persistent indices during layoutAboutToBeChanged.
    const QModelIndexList persistent_indices = persistentIndexList();
    std::vector<std::pair<QModelIndex, SceneItemId>> remap;
    remap.reserve(static_cast<std::size_t>(persistent_indices.size()));
    for (const QModelIndex& idx : persistent_indices)
    {
        const SceneItemId item = item_from_index(idx);
        if (item)
            remap.emplace_back(idx, item);
    }

    cache->resync(parents, std::move(child_lists));
    if (!is_current())
        return;
    auto retired = cache->take_retired_proxies();

    QModelIndexList from_indices;
    QModelIndexList to_indices;
    for (const auto& candidate : remap)
    {
        if (auto* proxy = cache->proxy_for(candidate.second))
        {
            // A surviving item may have moved into a branch that was never opened.
            if (proxy->parent_item())
            {
                auto* parent = cache->materialize(proxy->parent_item());
                if (!is_current())
                    return;
                if (parent)
                    cache->ensure_children_loaded(parent);
                if (!is_current())
                    return;
            }
        }
        from_indices.push_back(candidate.first);
        to_indices.push_back(m_cache->contains(candidate.second) ? index_for_item(candidate.second, candidate.first.column()) : QModelIndex());
    }
    if (!from_indices.empty())
        changePersistentIndexList(from_indices, to_indices);

    Q_EMIT layoutChanged(affected, QAbstractItemModel::VerticalSortHint);
}

bool SceneOutlinerModel::resync_rows(const SceneItemList& parents, SceneOutlinerHierarchyCache::ChildLists& child_lists)
{
    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto source = m_source;
    if (!source)
        return false;
    const auto operation = cache->hierarchy_token();
    const auto is_current = [&]() {
        return self && cache == m_cache && cache->is_valid() && !operation.is_cancelled();
    };
    struct Change
    {
        SceneItemId parent;
        int first;
        int removed;
        int inserted;
    };
    std::vector<Change> changes;
    SceneItemList pending = parents;
    SceneItemSet visited;
    while (!pending.empty())
    {
        const auto item = pending.back();
        pending.pop_back();
        auto* proxy = m_cache->proxy_for(item);
        if (!visited.insert(item).second || !proxy || !proxy->children_loaded())
            continue;
        const bool exists = !item || source->has_item(item);
        if (!is_current() || !source || !exists)
            return false;
        auto snapshot = cache->read_children(item);
        if (!is_current() || !snapshot)
            return false;
        const auto& children = child_lists.emplace(item, std::move(*snapshot)).first->second;
        const auto& previous = proxy->children();
        size_t first = 0;
        while (first < previous.size() && first < children.size() && previous[first] == children[first])
            ++first;
        size_t old_end = previous.size();
        size_t new_end = children.size();
        while (old_end > first && new_end > first && previous[old_end - 1] == children[new_end - 1])
        {
            --old_end;
            --new_end;
        }
        const auto removed = old_end - first;
        const auto inserted = new_end - first;
        if (removed && inserted)
            return false;
        // Reparenting needs the layout path to preserve indices across branches.
        for (size_t row = first; row < old_end; ++row)
        {
            SceneItemList descendants { previous[row] };
            SceneItemSet checked;
            while (!descendants.empty())
            {
                const auto removed_item = descendants.back();
                descendants.pop_back();
                if (!checked.insert(removed_item).second)
                    continue;
                const bool exists = source->has_item(removed_item);
                if (!is_current() || !source || exists)
                    return false;
                if (const auto* child = cache->proxy_for(removed_item))
                    descendants.insert(descendants.end(), child->children().begin(), child->children().end());
            }
        }
        for (size_t row = first; row < new_end; ++row)
            if (m_cache->contains(children[row]))
                return false;
        for (size_t row = 0; row < previous.size(); ++row)
        {
            if (row >= first && row < old_end)
                continue;
            const auto* child = m_cache->proxy_for(previous[row]);
            if (child)
                pending.push_back(previous[row]);
        }
        if (removed || inserted)
            changes.push_back({ item, static_cast<int>(first), static_cast<int>(removed), static_cast<int>(inserted) });
    }
    if (changes.empty())
        return false;

    SceneItemList inserted_items;
    for (const auto& change : changes)
    {
        const auto& children = child_lists.at(change.parent);
        const auto first = children.begin() + change.first;
        inserted_items.insert(inserted_items.end(), first, first + change.inserted);
    }
    if (!inserted_items.empty())
    {
        Q_EMIT child_rows_preparing(inserted_items);
        if (!is_current() || m_update_interrupted || !source)
            return false;
    }
    cache->invalidate_child_hints(visited);
    for (auto& change : changes)
    {
        auto* proxy = m_cache->proxy_for(change.parent);
        const auto parent = index_for_item(change.parent);
        if (change.inserted)
            beginInsertRows(parent, change.first, change.first + change.inserted - 1);
        else
            beginRemoveRows(parent, change.first, change.first + change.removed - 1);
        if (!is_current())
            return false;
        auto children = child_lists.extract(change.parent);
        cache->apply_child_rows(proxy, std::move(children.mapped()), change.first, change.removed, change.inserted);
        auto retired = cache->take_retired_proxies();
        if (change.inserted)
            endInsertRows();
        else
            endRemoveRows();
        if (!is_current() || !source)
            return false;
    }
    return true;
}

void SceneOutlinerModel::emit_items_changed(const SceneItemList& items)
{
    if (!has_source() || columnCount() <= 0)
        return;
    const QPointer<SceneOutlinerModel> self(this);
    const auto cache = m_cache;
    const auto operation = cache->hierarchy_token();
    if (items.empty())
    {
        std::unordered_map<SceneItemId, std::pair<QModelIndex, QModelIndex>, SceneItemId::Hash> ranges;
        cache->for_each_proxy([&](const Proxy& proxy) {
            const auto* parent = cache->parent_of(&proxy);
            auto& range = ranges[parent ? parent->item() : SceneItemId()];
            const auto first = index_for_item(proxy.item(), 0);
            const auto last = index_for_item(proxy.item(), columnCount() - 1);
            if (!range.first.isValid() || first.row() < range.first.row())
                range.first = first;
            if (!range.second.isValid() || last.row() > range.second.row())
                range.second = last;
        });
        std::vector<std::pair<QPersistentModelIndex, QPersistentModelIndex>> rows;
        for (const auto& entry : ranges)
            rows.emplace_back(entry.second.first, entry.second.second);
        for (const auto& row : rows)
            if (row.first.isValid() && row.second.isValid())
            {
                Q_EMIT dataChanged(row.first, row.second);
                if (!self || cache != m_cache || operation.is_cancelled() || m_update_interrupted || m_pending_source)
                    return;
            }
        return;
    }
    for (const SceneItemId& item : items)
    {
        if (!m_cache->contains(item))
            continue;
        const QModelIndex first = index_for_item(item, 0);
        const QModelIndex last = index_for_item(item, columnCount() - 1);
        if (first.isValid() && last.isValid())
        {
            Q_EMIT dataChanged(first, last);
            if (!self || cache != m_cache || operation.is_cancelled() || m_update_interrupted || m_pending_source)
                return;
        }
    }
}

OPENDCC_NAMESPACE_CLOSE
