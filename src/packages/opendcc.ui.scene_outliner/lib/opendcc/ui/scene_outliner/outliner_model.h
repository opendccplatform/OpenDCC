/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/hierarchy_cache.h"
#include "opendcc/ui/scene_outliner/item_id.h"
#include "opendcc/ui/scene_outliner/tree_data_source.h"

#include <QAbstractItemModel>
#include <QByteArray>
#include <QPointer>

#include <memory>
#include <unordered_set>
#include <vector>
#include <optional>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Qt tree model backed by a data source and a lazy hierarchy cache.
 * @note Model methods run on the owning thread. Worker notifications are
 * queued there and discarded after source replacement.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum CustomRole
    {
        ITEM_TYPE_ID = Qt::UserRole + 1, ///< Item data type identifier.
        SEARCH_FILTER = Qt::UserRole + 2 ///< Reevaluate search acceptance without changing display data.
    };

    explicit SceneOutlinerModel(QObject* parent = nullptr);
    ~SceneOutlinerModel() override;

    /// Bind a borrowed source. The model clears itself when the source is destroyed.
    void set_data_source(ISceneOutlinerDataSource* source);
    ISceneOutlinerDataSource* data_source() const { return m_source; }
    /// Source awaiting the end of a Qt model update; retained by the Python binding.
    ISceneOutlinerDataSource* pending_data_source() const { return m_pending_source ? m_pending_source->data() : nullptr; }

    SceneItemId item_for_index(const QModelIndex& index) const;
    QModelIndex index_for_item(const SceneItemId& item, int column = 0) const;
    /// Materialize item's proxy chain before returning its index.
    QModelIndex ensure_index_for_item(const SceneItemId& item, int column = 0);

    /// Column definitions supplied by the data source.
    const SceneOutlinerColumnList& columns() const { return m_columns; }
    /// Index of the hierarchy/name column (carries the tree branch + expand).
    int tree_column() const { return m_tree_column; }
    /// Source root, used as the drop target on empty space; invalid for multiple roots.
    SceneItemId root_item() const { return m_root_item; }

    /// Whether column supports click toggling.
    bool is_toggle_column(int column) const;
    /// Flip the toggle of the item at `index` (must be a toggle-column index).
    void toggle_column(const QModelIndex& index);

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    /// Include the source's MIME data alongside Qt's internal drag encoding.
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    /// Items captured by a drag from this model; empty for foreign or reset-stale payloads.
    SceneItemList items_for_mime_data(const QMimeData* mime) const;

Q_SIGNALS:
    /// Emitted when the borrowed data source changes, including its destruction.
    void data_source_changed();
    void source_about_to_change(bool affects_search);
    void source_changed(bool affects_search);

private:
    friend class SceneOutliner;
    friend class SceneOutlinerTreeView;
    friend class SceneOutlinerProxyModel;
    void begin_update();
    void finish_update();
    void apply_pending_data_source();
    enum class ChildQuery
    {
        HINT,
        LOAD_CHILDREN
    };
    void queue_child_query(const SceneItemId& item, ChildQuery query) const;
    std::optional<bool> child_hint_for_paint(const QModelIndex& index) const;
    void schedule_pending_tree_updates() const;
    void apply_pending_tree_updates();
    void apply_pending_child_queries();
    bool load_children(SceneOutlinerHierarchyCache::Proxy* proxy, bool complete = true);
    Q_SIGNAL void child_rows_preparing(const SceneItemList& items);
    void rebuild();
    void on_source_changed(const SceneOutlinerChange& change);
    void resync_subtrees(const SceneItemList& parents);
    bool resync_rows(const SceneItemList& parents, SceneOutlinerHierarchyCache::ChildLists& child_lists);
    void emit_items_changed(const SceneItemList& items);

    bool has_source() const;
    QModelIndex materialize_index(const SceneItemId& item, int column, bool* awaiting_rows);
    SceneOutlinerHierarchyCache::Proxy* proxy_from_index(const QModelIndex& index) const;
    SceneItemId item_from_index(const QModelIndex& index) const;
    SceneItemId item_at_row(int row, const QModelIndex& parent) const;
    SceneItemSet filter_items() const;

    QPointer<ISceneOutlinerDataSource> m_source;
    QPointer<QObject> m_source_context;
    std::shared_ptr<SceneOutlinerHierarchyCache> m_cache;
    SceneItemId m_root_item;
    SceneOutlinerColumnList m_columns;
    int m_tree_column = 0;
    mutable QByteArray m_drag_token;
    mutable SceneItemList m_drag_items;
    bool m_defer_child_hints = false;
    bool m_defer_child_queries = false;
    bool m_updating = false;
    bool m_update_interrupted = false;
    bool m_appending_child_rows = false;
    struct ChildLoad
    {
        SceneItemList children;
        std::size_t next = 0;
        std::size_t batch_size = 0;
        bool active = false;
    };
    std::unordered_map<SceneItemId, std::shared_ptr<ChildLoad>, SceneItemId::Hash> m_child_loads;
    std::optional<QPointer<ISceneOutlinerDataSource>> m_pending_source;
    bool m_pending_source_clear = false;
    struct PendingTreeUpdates
    {
        std::vector<SceneOutlinerChange> source_changes;
        std::unordered_map<SceneItemId, ChildQuery, SceneItemId::Hash> child_queries;
        bool scheduled = false;
    };
    mutable PendingTreeUpdates m_pending_tree_updates;
};

OPENDCC_NAMESPACE_CLOSE
