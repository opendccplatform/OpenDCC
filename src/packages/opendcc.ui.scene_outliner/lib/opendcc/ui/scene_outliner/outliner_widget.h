/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/details/cancellation_token.h"
#include "opendcc/ui/scene_outliner/selection_context.h"

#include <QPoint>
#include <QPointer>
#include <QModelIndex>
#include <QSet>
#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

class QLineEdit;
class QElapsedTimer;
class QMenu;
class QMenuBar;
class QTimer;
class QToolButton;

OPENDCC_NAMESPACE_OPEN

class ISceneOutlinerDataSource;
class SceneOutlinerModel;
class SceneOutlinerProxyModel;
class SceneOutlinerTreeView;
class SceneOutlinerSearchRequest;

/**
 * @brief Searchable scene tree with backend selection synchronization.
 * @note The widget owns its default model, proxy and view. Adapters retain their QObject ownership.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutliner : public QWidget
{
    Q_OBJECT
public:
    SceneOutliner(ISceneOutlinerDataSource* source = nullptr, ISceneOutlinerSelection* selection = nullptr, QWidget* parent = nullptr);
    ~SceneOutliner() override;

    /// Bind a borrowed source; nullptr clears the tree.
    void set_data_source(ISceneOutlinerDataSource* source);
    /// Swap the selection context the widget syncs with. Not owned.
    void set_selection_context(ISceneOutlinerSelection* selection);

    ISceneOutlinerDataSource* data_source() const;
    QModelIndex index_for_item(const SceneItemId& item) const;
    SceneItemId item_for_index(const QModelIndex& index) const;

    ISceneOutlinerSelection* selection_context() const { return m_selection; }

    SceneOutlinerTreeView* tree_view() const { return m_view; }
    SceneOutlinerModel* model() const;
    /// Menu bar owned by the widget; Python wrappers populate its menus.
    QMenuBar* menu_bar() const { return m_menu_bar; }
    bool filter_shown(const QString& id) const { return !m_hidden_filters.contains(id); }
    /// Change a backend category without changing backend selection.
    void set_filter_shown(const QString& id, bool shown);
    /// Set the search field, using the same debounce as keyboard input.
    void set_search_text(const QString& text);
    QString search_text() const;
    bool search_pending() const { return m_search_pending; }

    /// Currently selected items.
    SceneItemList selected_items() const;
    /// Start an inline rename of `item`'s name cell (if the source allows it).
    void edit_item(const SceneItemId& item);
    /// Start an inline rename of the current item.
    void edit_current();

Q_SIGNALS:
    /// Emitted after recreating the model following its destruction.
    void model_changed();
    /// Available categories or their defaults changed after a source update.
    void filters_changed();

    /// Emitted after the backend selection adapter changes or is destroyed.
    void selection_context_changed();

    /// Emitted on right-click after the item under the cursor is selected.
    /// Append actions to menu to display it. Handlers can query selected_items().
    void context_menu_requested(QMenu* menu);

private Q_SLOTS:
    void on_filter_text_changed(const QString& text);
    void on_filter_timeout();
    void on_view_selection_changed();
    void on_context_menu(const QPoint& pos);

private:
    void connect_selection_context();
    void disconnect_selection_context();
    void apply_selection_from_context(bool wait_for_rows = false);
    void update_ancestor_highlight();
    void refresh_filters();
    void update_hidden_filters();
    void update_search_filter();
    struct SearchResults;
    void apply_search_results(SceneItemList matches);
    void continue_search_results(const std::shared_ptr<SearchResults>& results);
    bool collect_search_matches(const std::shared_ptr<SearchResults>& results, const QElapsedTimer& timer);
    bool prepare_search_rows(const std::shared_ptr<SearchResults>& results, const QElapsedTimer& timer);
    bool defer_search_results(const std::shared_ptr<SearchResults>& results, const QElapsedTimer& timer);
    void finish_search_results(const std::shared_ptr<SearchResults>& results);
    void reset_model();
    void connect_model();
    scene_outliner::CancellationToken invalidate_search();
    scene_outliner::CancellationToken cancel_search();
    void toggle_search(bool on);

    QPointer<SceneOutlinerModel> m_model;
    SceneOutlinerProxyModel* m_proxy = nullptr;
    SceneOutlinerTreeView* m_view = nullptr;
    QLineEdit* m_filter = nullptr;
    QToolButton* m_search_button = nullptr;
    QTimer* m_filter_timer = nullptr;
    QMenuBar* m_menu_bar = nullptr;
    QString m_pending_filter;
    QPointer<SceneOutlinerSearchRequest> m_search_request;
    scene_outliner::CancellationToken m_search_token;
    bool m_search_pending = false;
    QSet<QString> m_hidden_filters;
    QSet<QString> m_known_filters;
    QPointer<ISceneOutlinerDataSource> m_filter_source;
    bool m_filters_dirty = false;

    QPointer<ISceneOutlinerSelection> m_selection;
    QPointer<QObject> m_selection_context;
    bool m_syncing_selection = false;
    bool m_selection_sync_pending = false;
};

OPENDCC_NAMESPACE_CLOSE
