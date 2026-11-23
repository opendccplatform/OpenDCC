/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/details/cancellation_token.h"
#include "opendcc/ui/scene_outliner/item_id.h"

#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <functional>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Filters rows by backend categories and a precomputed search accept set.
 * @note Include each match's ancestors in the accept set. This keeps filtering
 * lazy without recursively populating the source model.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit SceneOutlinerProxyModel(QObject* parent = nullptr);
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void set_highlighted(const SceneItemList& items);
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    void setSourceModel(QAbstractItemModel* source) override;

    /// When active, show only accept (matches and ancestors), subject to category filters.
    void set_search_filter(bool active, SceneItemSet accept);
    bool search_active() const { return m_search_active; }

    /// Hide items and their subtrees matching any of these category IDs.
    void set_hidden_filters(const QSet<QString>& hidden);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    friend class SceneOutliner;
    using SearchCheck = std::function<bool()>;
    using SearchRows = std::unordered_map<const void*, std::vector<unsigned char>>;
    bool hidden_item(const SceneItemId& item, const SearchCheck& is_current = {}) const;
    bool cached_hidden_item(const SceneItemId& item) const;
    bool refresh_filter_items(const SceneItemSet& items) const;
    void schedule_filter_refresh(const SceneItemId& item = {}) const;
    void prepare_search_row(int row, const QModelIndex& parent, bool active, const SceneItemSet& accept, const SearchCheck& is_current,
                            SearchRows& rows);
    void apply_search_filter(bool active, SceneItemSet accept, SearchRows rows);
    void clear_filter_cache(bool keep_item_filters = false);
    void apply_category_filter();
    Q_SIGNAL void category_filter_about_to_change();
    Q_SIGNAL void category_filter_changed();
    SearchRows m_search_rows;
    scene_outliner::CancellationToken m_filter_token;
    SceneItemSet m_highlighted;
    scene_outliner::CancellationToken m_highlight_token;
    scene_outliner::CancellationToken m_search_token;
    bool m_search_active = false;
    bool m_filtering = false;
    SceneItemSet m_accept;
    QSet<QString> m_hidden_filters;
    using FilterCache = std::unordered_map<SceneItemId, bool, SceneItemId::Hash>;
    mutable FilterCache m_item_filters;
    mutable SceneItemSet m_pending_filters;
    mutable bool m_filter_refresh_scheduled = false;
    mutable bool m_refresh_known_items = false;
    mutable std::unordered_map<const void*, bool> m_parent_filters;
    std::vector<QMetaObject::Connection> m_filter_connections;
};

OPENDCC_NAMESPACE_CLOSE
