// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_layers/usd_layers_view.h"

#include "opendcc/usd_editor/usd_layers/usd_layers_model.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"

#include <QApplication>
#include <QRegularExpression>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

#include <functional>

OPENDCC_NAMESPACE_OPEN

namespace
{
    class LayerDelegate : public QStyledItemDelegate
    {
    public:
        explicit LayerDelegate(QObject* parent)
            : QStyledItemDelegate(parent)
            , m_muted_icon(QStringLiteral(":/layer_icons/muted"))
            , m_unmuted_icon(QStringLiteral(":/layer_icons/unmuted"))
            , m_checkmark_icon(QStringLiteral(":/icons/checkmark_white"))
            , m_sublayer_icon(QStringLiteral(":/icons/layers"))
            , m_reference_icon(QStringLiteral(":/icons/create_reference.png"))
        {
        }

        static QBrush background_for_index(const QModelIndex& index)
        {
            const QVariant background = index.data(Qt::BackgroundRole);
            if (background.canConvert<QBrush>())
                return qvariant_cast<QBrush>(background);
            if (index.data(UsdLayersModel::LayerIsEditTargetRole).toBool())
                return QColor(150, 200, 250, 70);
            if (index.data(UsdLayersModel::LayerIsEditTargetParentRole).toBool())
                return QColor(150, 200, 250, 40);
            return {};
        }

        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            QStyledItemDelegate::paint(painter, option, index);
            if (index.column() != UsdLayersModel::EditTargetColumn || index.data(UsdLayersModel::LayerIsReferenceRole).toBool())
                return;
            QStyleOptionButton radio;
            radio.state = QStyle::State_Enabled;
            radio.state |= index.data(UsdLayersModel::LayerIsEditTargetRole).toBool() ? QStyle::State_On : QStyle::State_Off;
            const int size = QApplication::style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth);
            radio.rect = QStyle::alignedRect(option.direction, Qt::AlignCenter, QSize(size, size), option.rect);
            QApplication::style()->drawPrimitive(QStyle::PE_IndicatorRadioButton, &radio, painter);
        }

    protected:
        void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
        {
            QStyledItemDelegate::initStyleOption(option, index);
            option->backgroundBrush = background_for_index(index);
            if (!index.data(Qt::FontRole).isValid())
                option->font.setBold(index.data(UsdLayersModel::LayerIsEditTargetRole).toBool());
            if (index.data(Qt::DecorationRole).isValid())
                return;
            QIcon icon;
            if (index.column() == UsdLayersModel::NameColumn)
                icon = index.data(UsdLayersModel::LayerIsReferenceRole).toBool() ? m_reference_icon : m_sublayer_icon;
            else if (index.column() == UsdLayersModel::MutedColumn && !index.data(UsdLayersModel::LayerIsRootRole).toBool())
                icon = index.data(UsdLayersModel::LayerIsMutedRole).toBool() ? m_muted_icon : m_unmuted_icon;
            else if (index.column() == UsdLayersModel::DirtyColumn && index.data(UsdLayersModel::LayerIsDirtyRole).toBool())
                icon = m_checkmark_icon;
            if (!icon.isNull())
            {
                option->icon = icon;
                option->features |= QStyleOptionViewItem::HasDecoration;
            }
        }

    private:
        QIcon m_muted_icon;
        QIcon m_unmuted_icon;
        QIcon m_checkmark_icon;
        QIcon m_sublayer_icon;
        QIcon m_reference_icon;
    };

    UsdLayersModel* source_layers_model(QAbstractItemModel* model)
    {
        if (auto* proxy = qobject_cast<QSortFilterProxyModel*>(model))
            return qobject_cast<UsdLayersModel*>(proxy->sourceModel());
        return qobject_cast<UsdLayersModel*>(model);
    }

    const UsdLayersModel* source_layers_model(const QAbstractItemModel* model)
    {
        if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(model))
            return qobject_cast<const UsdLayersModel*>(proxy->sourceModel());
        return qobject_cast<const UsdLayersModel*>(model);
    }
}

UsdLayersView::UsdLayersView(QWidget* parent)
    : QTreeView(parent)
{
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setExpandsOnDoubleClick(false);
    setIconSize(QSize(20, 20));
    setIndentation(20);
    header()->setMinimumHeight(20);
    header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setItemDelegate(new LayerDelegate(this));
    auto* layers_model = new UsdLayersModel(this);
    auto* proxy = new UsdLayersProxyModel(this);
    proxy->setSourceModel(layers_model);
    setModel(proxy);
    connect(proxy, &QAbstractItemModel::modelReset, this, [this]() {
        if (selected_layers().isEmpty())
            Q_EMIT layers_changed();
    });

    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QHeaderView::customContextMenuRequested, this, &UsdLayersView::show_header_menu);
    connect(this, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        auto* model = source_layers_model(this->model());
        if (!model)
            return;
        if (index.column() == UsdLayersModel::MutedColumn)
        {
            model->toggle_muted(source_index(index));
        }
        else if (index.column() == UsdLayersModel::EditTargetColumn)
        {
            const QModelIndex source = source_index(index);
            if (!source.data(UsdLayersModel::LayerIsReferenceRole).toBool())
                model->set_edit_target_layer(layer_for_index(index));
        }
    });
    connect(this, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (auto* model = source_layers_model(this->model()))
        {
            const QModelIndex source = source_index(index);
            if (!source.data(UsdLayersModel::LayerIsReferenceRole).toBool())
                model->set_edit_target_layer(layer_for_index(index));
        }
    });
}

void UsdLayersView::set_layer_model(UsdLayersModel* layers_model)
{
    if (!layers_model)
        return;
    if (auto* proxy = qobject_cast<UsdLayersProxyModel*>(model()))
        proxy->setSourceModel(layers_model);
}

UsdLayersModel* UsdLayersView::layer_model() const
{
    return source_layers_model(model());
}

void UsdLayersView::set_current_stage()
{
    auto* layers_model = source_layers_model(model());
    if (!layers_model)
        return;
    const auto stage = Application::instance().get_session()->get_current_stage();
    layers_model->set_stage(stage);
    expandAll();
    if (stage)
        select_layer(stage->GetRootLayer());
}

void UsdLayersView::refresh()
{
    if (auto* layers_model = source_layers_model(model()))
        layers_model->refresh();
}

void UsdLayersView::add_sublayer()
{
    auto* layers_model = source_layers_model(model());
    if (!layers_model || !layers_model->stage())
        return;
    const auto selected = selected_layers();
    const auto parent = selected.isEmpty() ? layers_model->stage()->GetRootLayer() : selected.first();
    add_sublayer_to(QString::fromStdString(parent->GetIdentifier()));
}

void UsdLayersView::add_sublayer_to(const QString& parent_identifier)
{
    auto* layers_model = source_layers_model(model());
    if (!layers_model || !layers_model->stage())
        return;

    const auto parent = PXR_NS::SdfLayer::Find(parent_identifier.toStdString());
    const auto current = currentIndex();
    const auto parent_index =
        current.data(UsdLayersModel::LayerIdentifierRole).toString() == parent_identifier ? current : proxy_index_for_layer(parent);
    auto child_path = index_path(parent_index);
    const auto child = layers_model->add_sublayer(parent);
    if (!child)
        return;

    child_path.push_back(QString::fromStdString(child->GetIdentifier()));
    layers_model->refresh();
    auto index = index_for_path(child_path);
    if (!index.isValid())
    {
        set_filter_text(QString());
        Q_EMIT filter_text_cleared();
        index = index_for_path(child_path);
    }
    if (!index.isValid())
        index = proxy_index_for_layer(child);
    if (index.isValid() && selectionModel())
    {
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        frame_selection();
    }
}

void UsdLayersView::select_layer_identifiers(const QStringList& identifiers)
{
    QList<PXR_NS::SdfLayerHandle> layers;
    for (const auto& identifier : identifiers)
        layers.push_back(PXR_NS::SdfLayer::Find(identifier.toStdString()));
    select_layers(layers);
}

void UsdLayersView::set_filter_text(const QString& text)
{
    auto* proxy = qobject_cast<UsdLayersProxyModel*>(model());
    if (!proxy)
        return;
    proxy->setFilterRegularExpression(
        QRegularExpression(QStringLiteral("^") + QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption));
    if (!text.isEmpty())
        expandAll();
}

void UsdLayersView::set_show_references(bool show)
{
    if (auto* proxy = qobject_cast<UsdLayersProxyModel*>(model()))
        proxy->set_show_references(show);
}
void UsdLayersView::set_show_only_dirty(bool show)
{
    if (auto* proxy = qobject_cast<UsdLayersProxyModel*>(model()))
        proxy->set_show_only_dirty(show);
}
void UsdLayersView::set_show_only_anonymous(bool show)
{
    if (auto* proxy = qobject_cast<UsdLayersProxyModel*>(model()))
        proxy->set_show_only_anonymous(show);
}
void UsdLayersView::set_show_only_empty(bool show)
{
    if (auto* proxy = qobject_cast<UsdLayersProxyModel*>(model()))
        proxy->set_show_only_empty(show);
}

void UsdLayersView::setModel(QAbstractItemModel* model)
{
    if (this->model())
        disconnect(this->model(), nullptr, this, nullptr);
    if (selectionModel())
        disconnect(selectionModel(), nullptr, this, nullptr);
    QTreeView::setModel(model);
    configure_columns();

    if (model)
    {
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, &UsdLayersView::save_expanded_state, Qt::UniqueConnection);
        connect(model, &QAbstractItemModel::modelReset, this, &UsdLayersView::restore_expanded_state, Qt::UniqueConnection);
    }
    if (selectionModel())
        connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &UsdLayersView::emit_selection);
}

QList<PXR_NS::SdfLayerHandle> UsdLayersView::selected_layers() const
{
    QList<PXR_NS::SdfLayerHandle> layers;
    if (!selectionModel())
        return layers;

    for (const QModelIndex& index : selectionModel()->selectedRows(UsdLayersModel::NameColumn))
    {
        const PXR_NS::SdfLayerHandle layer = layer_for_index(index);
        if (layer)
            layers.push_back(layer);
    }
    return layers;
}

void UsdLayersView::select_layer(const PXR_NS::SdfLayerHandle& layer)
{
    if (!selectionModel())
        return;

    const QModelIndex index = proxy_index_for_layer(layer);
    if (!index.isValid())
        return;

    selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(index);
}

void UsdLayersView::select_layers(const QList<PXR_NS::SdfLayerHandle>& layers)
{
    if (!selectionModel())
        return;
    const QSignalBlocker blocker(selectionModel());
    selectionModel()->clearSelection();
    for (const auto& layer : layers)
    {
        const QModelIndex index = proxy_index_for_layer(layer);
        if (index.isValid())
            selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

QStringList UsdLayersView::index_path(const QModelIndex& index) const
{
    QStringList path;
    for (QModelIndex current = index; current.isValid(); current = current.parent())
        path.prepend(current.data(UsdLayersModel::LayerIdentifierRole).toString());
    return path;
}

QModelIndex UsdLayersView::index_for_path(const QStringList& path) const
{
    QModelIndex parent;
    for (const auto& identifier : path)
    {
        QModelIndex found;
        for (int row = 0; row < model()->rowCount(parent); ++row)
        {
            const QModelIndex candidate = model()->index(row, 0, parent);
            if (candidate.data(UsdLayersModel::LayerIdentifierRole).toString() == identifier)
            {
                found = candidate;
                break;
            }
        }
        if (!found.isValid())
            return {};
        parent = found;
    }
    return parent;
}

void UsdLayersView::save_expanded_state()
{
    m_resetting = true;
    m_expanded_paths.clear();
    m_selected_paths.clear();
    m_current_path = index_path(currentIndex());
    m_scroll_position = verticalScrollBar()->value();
    if (!model())
        return;

    std::function<void(const QModelIndex&)> collect = [&](const QModelIndex& parent) {
        for (int row = 0; row < model()->rowCount(parent); ++row)
        {
            const QModelIndex index = model()->index(row, 0, parent);
            if (isExpanded(index))
                m_expanded_paths.push_back(index_path(index));
            collect(index);
        }
    };
    collect(QModelIndex());
    if (selectionModel())
    {
        for (const auto& index : selectionModel()->selectedRows())
            m_selected_paths.push_back(index_path(index));
    }
}

void UsdLayersView::restore_expanded_state()
{
    for (const auto& path : m_expanded_paths)
    {
        const QModelIndex index = index_for_path(path);
        if (index.isValid())
            setExpanded(index, true);
    }
    if (selectionModel())
    {
        const QSignalBlocker blocker(selectionModel());
        for (const auto& path : m_selected_paths)
        {
            const QModelIndex index = index_for_path(path);
            if (index.isValid())
                selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        selectionModel()->setCurrentIndex(index_for_path(m_current_path), QItemSelectionModel::NoUpdate);
    }
    doItemsLayout();
    verticalScrollBar()->setValue(m_scroll_position);
    m_resetting = false;
    if (!m_selected_paths.isEmpty())
        emit_selection();
}

void UsdLayersView::drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QTreeView::drawRow(painter, option, index);

    const QModelIndex name_index = index.sibling(index.row(), UsdLayersModel::NameColumn);
    const QBrush background = LayerDelegate::background_for_index(name_index);
    const bool selected = selectionModel() && selectionModel()->isSelected(name_index);
    if (!selected && background.style() == Qt::NoBrush)
        return;

    const QRect tree_cell = visualRect(name_index);
    const QRect gutter(option.rect.left(), option.rect.top(), qMax(0, tree_cell.left() - option.rect.left()), option.rect.height());
    if (gutter.isEmpty())
        return;

    painter->save();
    painter->fillRect(gutter, selected ? QBrush(palette().highlight()) : background);
    painter->restore();
    drawBranches(painter, gutter, name_index);
}

void UsdLayersView::drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const
{
    QPen pen_branch(QBrush(QColor(130, 130, 130)), 1);
    const QPen pen_branch_hilite(QBrush(QColor(146, 166, 179)), 1);

    int level = 0;
    QModelIndex tmp_index = index.parent();
    while (tmp_index.isValid())
    {
        ++level;
        tmp_index = tmp_index.parent();
    }

    const bool is_highlight = selectionModel() && selectionModel()->isSelected(index);
    const int line_width = 1;
    const int cell_width = qMax(1, rect.width() / (level + 1));
    int cell_x = rect.x() + cell_width * level;
    const int cell_y = rect.y();
    const int cell_w = cell_width;
    const int cell_h = rect.height();
    int center_x = cell_x + cell_w / 2 - line_width / 2;
    const int center_y = cell_y + cell_h / 2 - line_width / 2;

    const QPen old_pen = painter->pen();

    if (model()->hasChildren(index))
    {
        const int rect_radius = 4;
        const int cross_margin = 1;
        const bool expanded = isExpanded(index);

        painter->setPen(is_highlight ? pen_branch_hilite : pen_branch);
        painter->drawRect(center_x - rect_radius, center_y - rect_radius, rect_radius * 2, rect_radius * 2);
        painter->drawLine(center_x - rect_radius + cross_margin + line_width, center_y, center_x + rect_radius - cross_margin - line_width, center_y);
        if (!expanded)
        {
            painter->drawLine(center_x, center_y - rect_radius + cross_margin + line_width, center_x,
                              center_y + rect_radius - cross_margin - line_width);
        }

        painter->setPen(pen_branch);
        if (expanded)
            painter->drawLine(center_x, center_y + rect_radius + cross_margin + line_width, center_x, cell_y + cell_h);
        if (level > 0)
            painter->drawLine(cell_x, center_y, center_x - rect_radius - cross_margin - line_width, center_y);
    }
    else
    {
        painter->setPen(pen_branch);
        if (level > 0)
        {
            painter->drawLine(cell_x, center_y, center_x, center_y);

            const QBrush old_brush = painter->brush();
            painter->setBrush(pen_branch.brush());
            const int circle_radius = 2;
            painter->drawEllipse(center_x - circle_radius, center_y - circle_radius, circle_radius * 2, circle_radius * 2);
            painter->setBrush(old_brush);
        }
    }

    if (level > 0)
    {
        cell_x -= cell_width;
        center_x -= cell_width;

        if (index.sibling(index.row() + 1, index.column()).isValid())
        {
            painter->drawLine(center_x, cell_y, center_x, cell_y + cell_h);
            painter->drawLine(center_x, center_y, cell_x + cell_w, center_y);
        }
        else
        {
            painter->drawLine(center_x, cell_y, center_x, center_y);
            painter->drawLine(center_x, center_y, cell_x + cell_w, center_y);
        }

        tmp_index = index.parent();
        for (int i = 0; i < level - 1; ++i)
        {
            cell_x -= cell_width;
            center_x -= cell_width;
            if (tmp_index.sibling(tmp_index.row() + 1, tmp_index.column()).isValid())
                painter->drawLine(center_x, cell_y, center_x, cell_y + cell_h);
            tmp_index = tmp_index.parent();
        }
    }

    painter->setPen(old_pen);
}

void UsdLayersView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier)
    {
        frame_selection();
        return;
    }
    QTreeView::keyPressEvent(event);
}

void UsdLayersView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !indexAt(event->pos()).isValid())
    {
        if (selectionModel())
            selectionModel()->clearSelection();
        event->accept();
        return;
    }
    QTreeView::mousePressEvent(event);
}

void UsdLayersView::contextMenuEvent(QContextMenuEvent* event)
{
    auto* model = source_layers_model(this->model());
    if (!model || !model->stage())
        return;

    const QModelIndex clicked = indexAt(event->pos());
    if (!clicked.isValid())
        return;
    if (auto* selection = selectionModel())
    {
        auto flags = QItemSelectionModel::SelectionFlags(QItemSelectionModel::NoUpdate);
        if (!selection->isSelected(clicked))
            flags = QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows;
        selection->setCurrentIndex(clicked, flags);
    }

    const QString identifier = clicked.data(UsdLayersModel::LayerIdentifierRole).toString();
    if (identifier.isEmpty())
        return;
    const QString parent_identifier = clicked.data(UsdLayersModel::LayerIsReferenceRole).toBool()
                                          ? QString()
                                          : clicked.parent().data(UsdLayersModel::LayerIdentifierRole).toString();
    QMenu menu(this);
    Q_EMIT context_menu_requested(&menu, identifier, parent_identifier);
    if (!menu.isEmpty() && menu.exec(event->globalPos()))
        model->refresh();
}

void UsdLayersView::configure_columns()
{
    if (!model())
        return;

    setTreePosition(UsdLayersModel::NameColumn);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(QHeaderView::Interactive);
}

void UsdLayersView::show_header_menu(const QPoint& position)
{
    if (!model())
        return;

    QMenu menu(this);
    for (int column = 0; column < model()->columnCount(); ++column)
    {
        QAction* action = menu.addAction(model()->headerData(column, Qt::Horizontal).toString());
        action->setCheckable(true);
        action->setChecked(!isColumnHidden(column));
        action->setEnabled(column != UsdLayersModel::NameColumn);
        connect(action, &QAction::toggled, this, [this, column](bool checked) { setColumnHidden(column, !checked); });
    }
    menu.exec(header()->mapToGlobal(position));
}

void UsdLayersView::emit_selection()
{
    if (m_resetting)
        return;
    QStringList identifiers;
    for (const PXR_NS::SdfLayerHandle& layer : selected_layers())
        identifiers.push_back(QString::fromStdString(layer->GetIdentifier()));
    Q_EMIT layers_selected(identifiers);
}

void UsdLayersView::frame_selection()
{
    if (!selectionModel())
        return;

    const QModelIndexList rows = selectionModel()->selectedRows();
    for (const QModelIndex& selected : rows)
    {
        QModelIndex index = selected.parent();
        while (index.isValid())
        {
            expand(index);
            index = index.parent();
        }
    }
    if (!rows.isEmpty())
        scrollTo(rows.first(), QAbstractItemView::PositionAtCenter);
}

QModelIndex UsdLayersView::source_index(const QModelIndex& index) const
{
    if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(model()))
        return proxy->mapToSource(index);
    return index;
}

QModelIndex UsdLayersView::proxy_index_for_layer(const PXR_NS::SdfLayerHandle& layer) const
{
    auto* model = source_layers_model(this->model());
    if (!model)
        return {};

    const QModelIndex source = model->index_for_layer(layer);
    if (!source.isValid())
        return {};

    if (auto* proxy = qobject_cast<QSortFilterProxyModel*>(this->model()))
        return proxy->mapFromSource(source);
    return source;
}

PXR_NS::SdfLayerHandle UsdLayersView::layer_for_index(const QModelIndex& index) const
{
    const auto* layers_model = source_layers_model(model());
    return layers_model ? layers_model->layer_for_index(source_index(index)) : PXR_NS::SdfLayerHandle();
}

OPENDCC_NAMESPACE_CLOSE
