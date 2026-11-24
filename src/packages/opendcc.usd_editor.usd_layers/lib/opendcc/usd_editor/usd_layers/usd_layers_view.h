/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/usd_layers/api.h"

#include <QList>
#include <QTreeView>

#include <pxr/usd/sdf/layer.h>

class QMenu;

OPENDCC_NAMESPACE_OPEN

class UsdLayersModel;

class OPENDCC_USD_LAYERS_API UsdLayersView : public QTreeView
{
    Q_OBJECT

public:
    explicit UsdLayersView(QWidget* parent = nullptr);

    void setModel(QAbstractItemModel* model) override;
    /** @brief Replaces the source model; the caller retains ownership. */
    void set_layer_model(UsdLayersModel* model);
    UsdLayersModel* layer_model() const;
    void set_current_stage();
    void refresh();
    void add_sublayer();
    void add_sublayer_to(const QString& parent_identifier);
    void select_layer_identifiers(const QStringList& identifiers);
    void set_filter_text(const QString& text);
    void set_show_references(bool show);
    void set_show_only_dirty(bool show);
    void set_show_only_anonymous(bool show);
    void set_show_only_empty(bool show);

Q_SIGNALS:
    void layers_changed();
    void filter_text_cleared();
    void layers_selected(const QStringList& identifiers);
    /**
     * @brief Lets packages populate the clicked layer's context menu.
     * @note Populate the menu synchronously; the view owns it.
     */
    void context_menu_requested(QMenu* menu, const QString& identifier, const QString& parent_identifier);

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QList<PXR_NS::SdfLayerHandle> selected_layers() const;
    void select_layer(const PXR_NS::SdfLayerHandle& layer);
    void select_layers(const QList<PXR_NS::SdfLayerHandle>& layers);
    void save_expanded_state();
    void restore_expanded_state();

    void configure_columns();
    void show_header_menu(const QPoint& position);
    void emit_selection();
    void frame_selection();
    QModelIndex source_index(const QModelIndex& index) const;
    QModelIndex proxy_index_for_layer(const PXR_NS::SdfLayerHandle& layer) const;
    PXR_NS::SdfLayerHandle layer_for_index(const QModelIndex& index) const;

    QStringList index_path(const QModelIndex& index) const;
    QModelIndex index_for_path(const QStringList& path) const;

    QList<QStringList> m_expanded_paths;
    QList<QStringList> m_selected_paths;
    QStringList m_current_path;
    int m_scroll_position = 0;
    bool m_resetting = false;
};

OPENDCC_NAMESPACE_CLOSE
