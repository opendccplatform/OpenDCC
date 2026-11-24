/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/usd_layers/api.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QSortFilterProxyModel>

#include <pxr/base/tf/hash.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

OPENDCC_NAMESPACE_OPEN

struct UsdLayersStageListener;
struct UsdLayersChanges;

class OPENDCC_USD_LAYERS_API UsdLayersModel : public QAbstractItemModel
{
    Q_OBJECT
    friend struct UsdLayersStageListener;

public:
    enum Column
    {
        NameColumn = 0,
        EditTargetColumn,
        PathColumn,
        ResolvedPathColumn,
        MutedColumn,
        DirtyColumn,
        VersionColumn,
        ColumnCount
    };

    enum CustomRole
    {
        LayerIdentifierRole = Qt::UserRole + 1,
        LayerIsReferenceRole,
        LayerIsMutedRole,
        LayerIsEditTargetRole,
        LayerIsEditTargetParentRole,
        LayerIsRootRole,
        LayerIsDirtyRole,
        LayerIsEmptyRole
    };

    struct LayerItem
    {
        explicit LayerItem(const PXR_NS::SdfLayerRefPtr& layer_, LayerItem* parent_ = nullptr, bool is_ref_ = false);

        PXR_NS::SdfLayerRefPtr layer;
        LayerItem* parent = nullptr;
        std::vector<std::unique_ptr<LayerItem>> children;
        int row = 0;
        bool is_ref = false;
    };

    explicit UsdLayersModel(QObject* parent = nullptr);
    ~UsdLayersModel() override;

    void set_stage(const PXR_NS::UsdStageRefPtr& stage);
    PXR_NS::UsdStageRefPtr stage() const;
    void refresh();
    void set_current_stage();
    bool is_model_valid() const;
    PXR_NS::SdfLayerHandle layer_for_index(const QModelIndex& index) const;
    LayerItem* item_for_index(const QModelIndex& index) const;
    QModelIndex index_for_layer(const PXR_NS::SdfLayerHandle& layer, int column = NameColumn) const;
    void set_edit_target_layer(const PXR_NS::SdfLayerHandle& layer);
    void toggle_muted(const QModelIndex& index);
    PXR_NS::SdfLayerHandle add_sublayer(const PXR_NS::SdfLayerHandle& parent_layer);

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

private:
    struct LayerState
    {
        std::vector<LayerItem*> items;
        std::set<std::string> references;
        bool dirty = false;
        bool empty = false;
    };

    using ItemChanges = QHash<LayerItem*, QVector<int>>;

    void apply_changes(const UsdLayersChanges& changes);
    void rebuild();
    void update_edit_target_parents();
    void notify_item_changes(const ItemChanges& changes);
    QModelIndex index_for_item(const LayerItem* item) const;
    QModelIndex find_index_for_layer(const QModelIndex& parent, const PXR_NS::SdfLayerHandle& layer, int column) const;
    void emit_all_data_changed(const QVector<int>& roles);

    QMap<int, QVariant> m_headers;
    PXR_NS::UsdStageRefPtr m_stage;
    std::unique_ptr<UsdLayersStageListener> m_stage_listener;
    std::vector<std::unique_ptr<LayerItem>> m_root_items;
    std::unordered_map<PXR_NS::SdfLayerHandle, LayerState, PXR_NS::TfHash> m_layers;
    PXR_NS::SdfLayerHandle m_edit_target;
    QSet<LayerItem*> m_edit_target_parents;
};

class OPENDCC_USD_LAYERS_API UsdLayersProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit UsdLayersProxyModel(QObject* parent = nullptr);

    void set_show_references(bool show);
    void set_show_only_dirty(bool show);
    void set_show_only_anonymous(bool show);
    void set_show_only_empty(bool show);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    bool m_show_references = true;
    bool m_show_only_dirty = false;
    bool m_show_only_anonymous = false;
    bool m_show_only_empty = false;
};

OPENDCC_NAMESPACE_CLOSE
