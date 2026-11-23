/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QPointer>
#include <QByteArray>

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/item_id.h"
#include "opendcc/ui/scene_outliner/item_predicate.h"

#include <QTreeView>

#include <functional>
#include <unordered_set>
#include <vector>

class QTimer;
class QRubberBand;
class QMenu;

OPENDCC_NAMESPACE_OPEN

class SceneOutlinerModel;
class SceneOutlinerProxyModel;

/**
 * @brief Outliner view with item-based expansion state and column editors.
 * @note Expansion survives layout changes and model resets by storing item IDs.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerTreeView : public QTreeView
{
    Q_OBJECT
public:
    explicit SceneOutlinerTreeView(QWidget* parent = nullptr);
    ~SceneOutlinerTreeView() override;
    void doItemsLayout() override;

    /// Bind borrowed models; the caller retains ownership.
    void set_models(SceneOutlinerProxyModel* proxy, SceneOutlinerModel* source);

    /// Show or hide the source root row. When hidden, the root's
    /// children become the top-level rows.
    void set_show_root(bool show);
    bool shows_root() const { return m_show_root; }
    /// Re-apply the current root mode (call after a model reset / re-filter).
    void apply_root_index();

    /// Apply the data source's column layout: tree position, fixed/stretch
    /// widths, default-hidden columns and combo editors for columns with choices.
    void configure_columns();

    /// Draw an animated branch spinner for these items; an empty list stops it.
    void set_busy_items(const SceneItemList& items);

    /// Materialize and expand items, emitting expansion_changed() once.
    void expand_items(const SceneItemList& items);

    /// The currently-expanded items, for publishing or saving the expansion state.
    SceneItemList expanded_items() const;
    /// Replace expansion state, materializing items as needed. Emits expansion_changed().
    void set_expanded_items(const SceneItemList& items);

    /// Predicate over items, used by the "…To" operations to decide the stop level.
    using ItemPredicate = SceneOutlinerItemPredicate;

    // Branch operations materialize rows as needed and emit one expansion_changed().

    /// Expand the entire subtree rooted at `item`.
    void expand_branch(const SceneItemId& item);
    /// Collapse the entire subtree rooted at `item`.
    void collapse_branch(const SceneItemId& item);
    /// Reveal item's immediate children.
    void expand_location(const SceneItemId& item);
    /// Collapse `item` itself.
    void collapse_location(const SceneItemId& item);
    /// Expand ancestor chains up to predicate matches. Leave other branches unchanged.
    void expand_branch_to(const SceneItemId& item, const ItemPredicate& predicate);
    /// Collapse the branch, then reveal the shallowest predicate matches.
    void collapse_branch_to(const SceneItemId& item, const ItemPredicate& predicate);

public Q_SLOTS:
    void expandAll();
    /// Expand selected items' ancestors and scroll the first item into view.
    void reveal_selection();

Q_SIGNALS:
    /// Emitted after the bound proxy and source models change.
    void models_changed();
    /// Populate the borrowed menu during this synchronous signal.
    void header_menu_requested(QMenu* menu);

    /// Emitted once per expansion action; suppressed during internal restoration.
    void expansion_changed();

private Q_SLOTS:
    void on_expanded(const QModelIndex& index);
    void on_collapsed(const QModelIndex& index);
    void on_layout_changed(const QList<QPersistentModelIndex>& parents);
    void on_rows_inserted(const QModelIndex& parent, int first, int last);
    void on_model_reset();
    void on_data_source_changed();
    void show_header_menu(const QPoint& pos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supported_actions) override;

private:
    struct BranchOperation;
    void start_drag(Qt::DropActions supported_actions, const QList<QPersistentModelIndex>& indexes);
    SceneItemId drop_target(const QPoint& position, const BranchOperation& operation, QRect* indicator = nullptr) const;
    QRubberBand* drop_indicator();
    void clear_drop_indicator();
    QPointer<QRubberBand> m_drop_indicator;
    QTimer* m_drag_scroll = nullptr;
    bool m_drag_pending = false;
    std::vector<QPointer<QAbstractItemDelegate>> m_column_delegates;
    QByteArray m_header_state;
    std::vector<std::string> m_column_ids;
    QPointer<QObject> m_column_source_context;
    SceneItemId item_for_proxy_index(const QModelIndex& proxy_index) const;
    // Materialize item and map it to column zero; filtered items have no proxy index.
    QModelIndex proxy_index_for_item(const SceneItemId& item);
    // Collect ancestors up to the first predicate match on each path. Return whether a match exists.
    bool collect_reveal_ancestors(const SceneItemId& root, const ItemPredicate& predicate, SceneItemSet& out, SceneItemSet& visited,
                                  const BranchOperation& operation) const;
    void restore_expansion(const QModelIndex& proxy_parent);
    void collapse_recursively(const QModelIndex& index, const BranchOperation& operation);

    QPointer<SceneOutlinerProxyModel> m_proxy;
    QPointer<SceneOutlinerModel> m_source;
    QMetaObject::Connection m_rows_inserted_connection;
    QMetaObject::Connection m_header_reset_connection;
    SceneItemSet m_expanded;
    QPointer<QObject> m_source_context;
    bool m_restoring = false;
    bool m_show_root = true;
    bool m_recursive_toggle = false;

    void draw_spinner(QPainter* painter, int cx, int cy, int radius) const;
    SceneItemSet m_busy_items;
    QTimer* m_busy_timer = nullptr;
    int m_busy_phase = 0;
};

OPENDCC_NAMESPACE_CLOSE
