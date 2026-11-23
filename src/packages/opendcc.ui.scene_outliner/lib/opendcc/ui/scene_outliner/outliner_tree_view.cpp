/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/outliner_tree_view.h"

#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/outliner_proxy_model.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMetaType>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QRubberBand>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <algorithm>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTimer>

OPENDCC_NAMESPACE_OPEN

namespace
{
    class OutlinerDelegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
        {
            // Qt asks for row height with an unconstrained width. Layout must not query providers.
            if (option.rect.width() < 0)
            {
                auto size = QStyledItemDelegate::sizeHint(option, QModelIndex());
                size.setHeight(std::max(option.fontMetrics.height(), option.decorationSize.height()));
                return size;
            }
            return QStyledItemDelegate::sizeHint(option, index);
        }
    };

    // Allow values outside the column's suggested choices.
    class ComboBoxDelegate : public OutlinerDelegate
    {
    public:
        ComboBoxDelegate(QStringList choices, QObject* parent)
            : OutlinerDelegate(parent)
            , m_choices(std::move(choices))
        {
        }

        QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
        {
            auto* combo = new QComboBox(parent);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->addItems(m_choices);
            return combo;
        }

        void setEditorData(QWidget* editor, const QModelIndex& index) const override
        {
            if (auto* combo = qobject_cast<QComboBox*>(editor))
                combo->setCurrentText(index.data(Qt::EditRole).toString());
        }

        void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
        {
            if (auto* combo = qobject_cast<QComboBox*>(editor))
                model->setData(index, combo->currentText(), Qt::EditRole);
        }

    private:
        QStringList m_choices;
    };
}

struct SceneOutlinerTreeView::BranchOperation
{
    explicit BranchOperation(SceneOutlinerTreeView* view)
        : view(view)
        , model(view->m_source)
        , proxy(view->m_proxy)
        , source(model ? model->data_source() : nullptr)
        , cache(model ? model->m_cache : nullptr)
        , operation(cache ? cache->hierarchy_token() : scene_outliner::CancellationToken())
        , recursive_toggle(view->m_recursive_toggle)
        , restoring(view->m_restoring)
    {
    }

    ~BranchOperation()
    {
        if (view)
        {
            view->m_recursive_toggle = recursive_toggle;
            view->m_restoring = restoring;
        }
    }

    bool is_current() const
    {
        return view && model && proxy && view->m_source == model && view->m_proxy == proxy && view->model() == proxy &&
               model->data_source() == source && model->m_cache == cache && (!cache || (cache->is_valid() && !operation.is_cancelled()));
    }

    bool is_valid() const { return source && cache && is_current(); }

    SceneItemList drag_items(const QMimeData* mime) const
    {
        if (!is_valid())
            return {};
        const bool can_reparent = source->can_reparent();
        if (!is_valid() || !can_reparent)
            return {};
        return model->items_for_mime_data(mime);
    }

    QPointer<SceneOutlinerTreeView> view;
    QPointer<SceneOutlinerModel> model;
    QPointer<SceneOutlinerProxyModel> proxy;
    QPointer<ISceneOutlinerDataSource> source;
    std::shared_ptr<SceneOutlinerHierarchyCache> cache;
    scene_outliner::CancellationToken operation;
    bool recursive_toggle;
    bool restoring;
};

SceneOutlinerTreeView::SceneOutlinerTreeView(QWidget* parent)
    : QTreeView(parent)
{
    qRegisterMetaType<QList<QPersistentModelIndex>>("QList<QPersistentModelIndex>");
    setUniformRowHeights(true); // big-scene scrolling: fixed row height skips per-row sizing.
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setExpandsOnDoubleClick(true);
    setAllColumnsShowFocus(true);
    setIconSize(QSize(20, 20));
    setItemDelegate(new OutlinerDelegate(this));
    setIndentation(18);
    setAlternatingRowColors(true);

    // The source handles reparenting; CopyAction prevents Qt from removing rows itself.
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);

    drop_indicator();
    m_drag_scroll = new QTimer(this);
    m_drag_scroll->setInterval(50);
    connect(m_drag_scroll, &QTimer::timeout, this, [this]() {
        const QPointer<SceneOutlinerTreeView> self(this);
        const auto position = viewport()->mapFromGlobal(QCursor::pos());
        if (!hasAutoScroll() || !viewport()->rect().contains(position))
            return;
        const auto scroll = [this](QScrollBar* bar, int position, int extent) {
            const int direction = position < autoScrollMargin() ? -1 : position >= extent - autoScrollMargin() ? 1 : 0;
            if (direction)
            {
                if (m_drop_indicator)
                    m_drop_indicator->hide();
                bar->setValue(bar->value() + direction * bar->singleStep());
            }
        };
        scroll(verticalScrollBar(), position.y(), viewport()->height());
        if (self)
            scroll(horizontalScrollBar(), position.x(), viewport()->width());
    });

    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QHeaderView::customContextMenuRequested, this, &SceneOutlinerTreeView::show_header_menu);
    connect(this, &QTreeView::expanded, this, &SceneOutlinerTreeView::on_expanded);
    connect(this, &QTreeView::collapsed, this, &SceneOutlinerTreeView::on_collapsed);
}

SceneOutlinerTreeView::~SceneOutlinerTreeView()
{
    if (m_proxy)
        disconnect(m_proxy, nullptr, this, nullptr);
    if (m_busy_timer)
        m_busy_timer->stop();
}

void SceneOutlinerTreeView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        const QModelIndex clicked = indexAt(event->pos());
        // Interactive toggles leave selection unchanged.
        if (m_proxy && m_source && clicked.isValid() && m_source->is_toggle_column(clicked.column()) &&
            clicked.column() < static_cast<int>(m_source->columns().size()) && m_source->columns()[clicked.column()].interactive)
        {
            m_source->toggle_column(m_proxy->mapToSource(clicked));
            event->accept();
            return;
        }
        if (!clicked.isValid())
        {
            clearSelection();
            event->accept();
            return;
        }
    }
    QTreeView::mousePressEvent(event);
}

void SceneOutlinerTreeView::doItemsLayout()
{
    if (!m_source)
    {
        QTreeView::doItemsLayout();
        return;
    }
    // Paint queues hints for visible rows. Layout can treat unknown children as possible.
    QScopedValueRollback<bool> deferred(m_source->m_defer_child_hints, true);
    QTreeView::doItemsLayout();
}

void SceneOutlinerTreeView::dragEnterEvent(QDragEnterEvent* event)
{
    const BranchOperation operation(this);
    const auto items = operation.drag_items(event->mimeData());
    if (!operation.is_valid() || items.empty())
        event->ignore();
    else
        event->acceptProposedAction();
}

void SceneOutlinerTreeView::dragMoveEvent(QDragMoveEvent* event)
{
    const BranchOperation operation(this);
    clear_drop_indicator();
    const auto items = operation.drag_items(event->mimeData());
    if (!operation.is_valid() || items.empty())
    {
        event->ignore();
        return;
    }
    QRect indicator;
    if (!drop_target(event->pos(), operation, &indicator) || !operation.is_valid())
    {
        event->ignore();
        return;
    }
    if (showDropIndicator())
    {
        drop_indicator()->setGeometry(indicator);
        m_drop_indicator->show();
    }
    if (hasAutoScroll())
        m_drag_scroll->start();
    event->acceptProposedAction();
}

void SceneOutlinerTreeView::dragLeaveEvent(QDragLeaveEvent* event)
{
    clear_drop_indicator();
    QTreeView::dragLeaveEvent(event);
}

void SceneOutlinerTreeView::dropEvent(QDropEvent* event)
{
    const BranchOperation operation(this);
    clear_drop_indicator();
    const auto items = operation.drag_items(event->mimeData());
    if (!operation.is_valid() || items.empty())
    {
        event->ignore();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto target = drop_target(event->position().toPoint(), operation);
#else
    const auto target = drop_target(event->pos(), operation);
#endif
    if (!operation.is_valid() || !target)
    {
        event->ignore();
        return;
    }

    operation.source->reparent(items, target);
    event->acceptProposedAction();
}

QRubberBand* SceneOutlinerTreeView::drop_indicator()
{
    if (!m_drop_indicator || m_drop_indicator->parentWidget() != viewport())
    {
        if (m_drop_indicator)
            m_drop_indicator->deleteLater();
        m_drop_indicator = new QRubberBand(QRubberBand::Rectangle, viewport());
    }
    return m_drop_indicator;
}

void SceneOutlinerTreeView::clear_drop_indicator()
{
    if (m_drop_indicator)
        m_drop_indicator->hide();
    m_drag_scroll->stop();
}

SceneItemId SceneOutlinerTreeView::drop_target(const QPoint& position, const BranchOperation& operation, QRect* indicator) const
{
    if (!operation.is_valid() || !viewport()->rect().contains(position))
        return {};
    const auto index = indexAt(position);
    if (!operation.is_valid())
        return {};
    const auto rect = index.isValid() ? visualRect(index) : viewport()->rect();
    if (!operation.is_valid())
        return {};
    if (index.isValid() && !dragDropOverwriteMode())
    {
        const int margin = qBound(2, qRound(qreal(rect.height()) / 5.5), 12);
        if (position.y() - rect.top() < margin || rect.bottom() - position.y() < margin)
            return {};
    }
    if (indicator)
        *indicator = rect.adjusted(0, 0, -1, -1);
    return index.isValid() ? item_for_proxy_index(index) : m_source->root_item();
}

void SceneOutlinerTreeView::startDrag(Qt::DropActions supported_actions)
{
    if (m_drag_pending || !(supported_actions & Qt::CopyAction))
        return;
    if (!m_source || !m_proxy || model() != m_proxy || !selectionModel())
        return;
    QList<QPersistentModelIndex> indexes;
    const auto selection = selectionModel()->selection();
    for (const auto& range : selection)
        for (int row = range.top(); row <= range.bottom(); ++row)
            for (int column = range.left(); column <= range.right(); ++column)
            {
                const auto index = m_proxy->index(row, column, range.parent());
                auto visible = index;
                while (visible.isValid() && !isIndexHidden(visible))
                    visible = visible.parent();
                if (index.isValid() && !visible.isValid())
                    indexes.push_back(m_proxy->mapToSource(index));
            }
    if (indexes.empty())
        return;
    m_drag_pending = true;
    const auto model = m_source;
    const auto cache = model ? model->m_cache : nullptr;
    // Qt's mouse handler must unwind before a provider can destroy the view.
    QTimer::singleShot(0, this, [this, model, cache, supported_actions, indexes]() {
        const QPointer<SceneOutlinerTreeView> self(this);
        if (model && model == m_source && cache == model->m_cache)
            start_drag(supported_actions, indexes);
        if (self)
            m_drag_pending = false;
    });
}

void SceneOutlinerTreeView::start_drag(Qt::DropActions supported_actions, const QList<QPersistentModelIndex>& retained)
{
    const BranchOperation operation(this);
    if (!operation.is_valid())
        return;
    const bool can_reparent = operation.source->can_reparent();
    if (!operation.is_valid())
        return;
    if (!can_reparent)
    {
        const auto mime_type = operation.source->drag_mime_type();
        if (!operation.is_valid() || mime_type.isEmpty())
            return;
    }

    QModelIndexList indexes;
    for (const auto& index : retained)
    {
        if (!index.isValid() || index.model() != operation.model)
            return;
        indexes.push_back(index);
    }
    const QPersistentModelIndex preview(indexes.front().sibling(indexes.front().row(), operation.model->tree_column()));
    std::unique_ptr<QMimeData> mime(operation.model->mimeData(indexes));
    if (!operation.is_valid() || !mime || !preview.isValid())
        return;
    const auto text = operation.model->data(preview, Qt::DisplayRole).toString();
    if (!operation.is_valid() || !preview.isValid())
        return;
    const auto icon = qvariant_cast<QIcon>(operation.model->data(preview, Qt::DecorationRole));
    if (!operation.is_valid() || !preview.isValid())
        return;

    // Paint owned values, without querying the live model from Qt's drag renderer.
    const int height = std::max(fontMetrics().height(), iconSize().height()) + 8;
    const auto label = fontMetrics().elidedText(text, Qt::ElideRight, std::max(0, viewport()->width() - height - 8));
    QPixmap pixmap(fontMetrics().horizontalAdvance(label) + height + 8, height);
    pixmap.fill(palette().color(QPalette::Highlight));
    {
        QPainter painter(&pixmap);
        painter.setFont(font());
        painter.setPen(palette().color(QPalette::HighlightedText));
        icon.paint(&painter, QRect(4, 4, height - 8, height - 8));
        painter.drawText(pixmap.rect().adjusted(height, 0, -4, 0), Qt::AlignVCenter, label);
    }
    if (!operation.is_valid())
        return;
    auto* drag = new QDrag(this);
    drag->setMimeData(mime.release());
    drag->setPixmap(pixmap);
    drag->exec(supported_actions & Qt::CopyAction, Qt::CopyAction);
}

void SceneOutlinerTreeView::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) && event->modifiers() == Qt::NoModifier && m_source && selectionModel() &&
        currentIndex().isValid() && currentIndex().column() == m_source->tree_column() && m_source->tree_column() != 0)
    {
        // Qt navigates the hierarchy through column zero, even when the tree
        // decoration and name are displayed in another column.
        const auto current = currentIndex();
        selectionModel()->setCurrentIndex(current.sibling(current.row(), 0), QItemSelectionModel::NoUpdate);
        QTreeView::keyPressEvent(event);
        const auto destination = currentIndex();
        selectionModel()->setCurrentIndex(destination.sibling(destination.row(), m_source->tree_column()), QItemSelectionModel::NoUpdate);
        return;
    }
    QTreeView::keyPressEvent(event);
}

void SceneOutlinerTreeView::reveal_selection()
{
    if (!selectionModel())
        return;
    const QModelIndexList selected = selectionModel()->selectedRows();
    if (!selected.isEmpty())
    {
        SceneItemList ancestors;
        SceneItemSet seen;
        for (const QModelIndex& index : selected)
            for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
            {
                const auto item = item_for_proxy_index(parent);
                if (!seen.insert(item).second)
                    break;
                ancestors.push_back(item);
            }
        const BranchOperation operation(this);
        const QPersistentModelIndex first(selected.first());
        expand_items(ancestors);
        if (operation.is_valid() && first.isValid())
            scrollTo(first, QAbstractItemView::PositionAtCenter);
    }
}

void SceneOutlinerTreeView::set_show_root(bool show)
{
    if (m_show_root == show)
        return;
    m_show_root = show;
    if (show && m_source && m_source->root_item())
        m_expanded.insert(m_source->root_item());
    apply_root_index();
}

void SceneOutlinerTreeView::apply_root_index()
{
    if (!model())
        return;
    const BranchOperation operation(this);
    on_data_source_changed();
    m_recursive_toggle = true;
    if (m_source && !m_source->root_item())
    {
        setRootIndex(QModelIndex());
        return;
    }
    if (m_show_root)
    {
        if (rootIndex().isValid())
            setRootIndex(QModelIndex());
        if (m_source && m_expanded.count(m_source->root_item()) && model()->rowCount() > 0)
            expand(model()->index(0, 0));
    }
    else if (model()->rowCount() > 0)
    {
        const auto root = model()->index(0, 0);
        if (rootIndex() != root)
            setRootIndex(root);
    }
}

void SceneOutlinerTreeView::show_header_menu(const QPoint& pos)
{
    if (!model() || !m_source)
        return;
    const QPointer<SceneOutlinerTreeView> self(this);
    const QPointer<QMenu> menu = new QMenu(this);
    Q_EMIT header_menu_requested(menu.data());
    if (self && menu && !menu->isEmpty())
        menu->exec(header()->mapToGlobal(pos));
    if (menu)
        menu->deleteLater();
}

void SceneOutlinerTreeView::drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const
{
    const QPen branch_pen(QBrush(QColor(130, 130, 130)), 1);
    const QPen branch_hilite_pen(QBrush(QColor(146, 166, 179)), 1);

    // Measure depth from the visible root, which may hide the source's root row.
    int level = -1;
    for (QModelIndex walk = index; walk.isValid() && walk != rootIndex(); walk = walk.parent())
        ++level;

    const QModelIndex name_index = index.sibling(index.row(), m_source ? m_source->tree_column() : 0);
    const bool selected = selectionModel() && selectionModel()->isSelected(name_index);
    const bool dim_highlight = !selected && name_index.data(Qt::BackgroundRole).canConvert<QBrush>();
    if (selected || dim_highlight)
        painter->fillRect(rect, selected ? QBrush(palette().highlight()) : qvariant_cast<QBrush>(name_index.data(Qt::BackgroundRole)));

    const QPen active_pen = selected ? branch_hilite_pen : branch_pen;
    const int line_width = 1;
    const int cell_width = level + 1 > 0 ? rect.width() / (level + 1) : rect.width();

    int cell_x = rect.x() + cell_width * level;
    const int cell_y = rect.y();
    const int cell_h = rect.height();
    int center_x = cell_x + cell_width / 2 - line_width / 2;
    const int center_y = cell_y + cell_h / 2 - line_width / 2;

    const QPen old_pen = painter->pen();

    const auto child_hint = m_source && m_proxy && index.model() == m_proxy ? m_source->child_hint_for_paint(m_proxy->mapToSource(index))
                                                                            : std::optional<bool>(model()->hasChildren(index));
    if (child_hint.value_or(false))
    {
        const int radius = 4;
        const int margin = 1;
        const bool expanded = isExpanded(index);
        // Skip item lookup when no branches are busy.
        const bool busy = !m_busy_items.empty() && m_busy_items.count(item_for_proxy_index(index)) > 0;

        if (busy)
        {
            draw_spinner(painter, center_x, center_y, radius);
        }
        else
        {
            painter->setPen(active_pen);
            painter->drawRect(center_x - radius, center_y - radius, radius * 2, radius * 2);
            painter->drawLine(center_x - radius + margin + line_width, center_y, center_x + radius - margin - line_width, center_y);
            if (!expanded)
                painter->drawLine(center_x, center_y - radius + margin + line_width, center_x, center_y + radius - margin - line_width);
        }

        painter->setPen(branch_pen);
        if (expanded)
            painter->drawLine(center_x, center_y + radius + margin + line_width, center_x, cell_y + cell_h);
        if (level > 0)
            painter->drawLine(cell_x, center_y, center_x - radius - margin - line_width, center_y);
    }
    else if (level > 0)
    {
        painter->setPen(branch_pen);
        painter->drawLine(cell_x, center_y, center_x, center_y);
        if (child_hint)
        {
            const QBrush old_brush = painter->brush();
            painter->setBrush(branch_pen.brush());
            const int dot = 2;
            painter->drawEllipse(center_x - dot, center_y - dot, dot * 2, dot * 2);
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
            painter->drawLine(center_x, center_y, cell_x + cell_width, center_y);
        }
        else
        {
            painter->drawLine(center_x, cell_y, center_x, center_y);
            painter->drawLine(center_x, center_y, cell_x + cell_width, center_y);
        }

        QModelIndex ancestor = index.parent();
        for (int i = 0; i < level - 1; ++i)
        {
            cell_x -= cell_width;
            center_x -= cell_width;
            if (ancestor.sibling(ancestor.row() + 1, ancestor.column()).isValid())
                painter->drawLine(center_x, cell_y, center_x, cell_y + cell_h);
            ancestor = ancestor.parent();
        }
    }

    painter->setPen(old_pen);
}

void SceneOutlinerTreeView::set_models(SceneOutlinerProxyModel* proxy, SceneOutlinerModel* source)
{
    if (m_proxy && m_proxy != proxy)
        disconnect(m_proxy, nullptr, this, nullptr);
    m_proxy = proxy;
    if (m_source != source)
    {
        if (m_source)
            disconnect(m_source, nullptr, this, nullptr);
        m_expanded.clear();
        set_busy_items({});
        m_source_context = source ? source->m_source_context : nullptr;
    }
    m_source = source;
    if (source)
    {
        source->m_defer_child_queries = true;
        connect(source, &SceneOutlinerModel::data_source_changed, this, &SceneOutlinerTreeView::on_data_source_changed, Qt::UniqueConnection);
        connect(source, &SceneOutlinerModel::source_about_to_change, this, &SceneOutlinerTreeView::clear_drop_indicator, Qt::UniqueConnection);
    }
    auto* previous_selection = selectionModel();
    setModel(proxy);
    if (previous_selection && previous_selection != selectionModel() && previous_selection->parent() == this)
        previous_selection->deleteLater();

    if (proxy)
    {
        disconnect(m_header_reset_connection);
        m_header_reset_connection =
            connect(proxy, &QAbstractItemModel::modelAboutToBeReset, this, [this]() { m_header_state = header()->saveState(); });
        const auto queued = static_cast<Qt::ConnectionType>(Qt::UniqueConnection | Qt::QueuedConnection);
        connect(proxy, &QAbstractItemModel::layoutChanged, this, &SceneOutlinerTreeView::on_layout_changed, queued);
        disconnect(m_rows_inserted_connection);
        m_rows_inserted_connection = connect(proxy, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex& parent, int first, int last) {
            const QPersistentModelIndex retained(parent);
            const bool valid = parent.isValid();
            QTimer::singleShot(0, this, [this, retained, valid, first, last]() {
                if (!valid || (retained.isValid() && retained.model() == m_proxy))
                    on_rows_inserted(retained, first, last);
            });
        });
        connect(proxy, &QAbstractItemModel::modelReset, this, &SceneOutlinerTreeView::on_model_reset, queued);
    }
    configure_columns();
    Q_EMIT models_changed();
}

void SceneOutlinerTreeView::configure_columns()
{
    if (!m_source || !model())
        return;

    for (int column = 0; column < static_cast<int>(m_column_delegates.size()); ++column)
    {
        if (m_column_delegates[column])
        {
            setItemDelegateForColumn(column, nullptr);
            m_column_delegates[column]->deleteLater();
        }
    }
    m_column_delegates.clear();
    setTreePosition(m_source->tree_column());

    QHeaderView* hdr = header();
    hdr->setStretchLastSection(false);
    const SceneOutlinerColumnList& columns = m_source->columns();
    std::vector<std::string> column_ids;
    for (const auto& column : columns)
        column_ids.push_back(column.id);
    const bool restore_header = m_column_source_context == m_source->m_source_context && column_ids == m_column_ids;
    m_column_source_context = m_source->m_source_context;
    m_column_ids = std::move(column_ids);
    m_column_delegates.resize(columns.size());
    for (int column = 0; column < static_cast<int>(columns.size()); ++column)
    {
        const SceneOutlinerColumn& col = columns[column];
        if (col.stretch)
            hdr->setSectionResizeMode(column, QHeaderView::Stretch);
        else if (col.width > 0)
        {
            // Only toggle widths are fixed; text columns remain resizable.
            hdr->setSectionResizeMode(column, col.role == SceneOutlinerColumn::Role::TOGGLE ? QHeaderView::Fixed : QHeaderView::Interactive);
            hdr->resizeSection(column, col.width);
        }
        else
            hdr->setSectionResizeMode(column, QHeaderView::Interactive);

        setColumnHidden(column, !col.default_visible);

        if (col.editable && !col.choices.isEmpty())
        {
            auto* delegate = new ComboBoxDelegate(col.choices, this);
            m_column_delegates[column] = delegate;
            setItemDelegateForColumn(column, delegate);
        }
    }
    if (restore_header && !m_header_state.isEmpty())
        hdr->restoreState(m_header_state);
}

SceneItemId SceneOutlinerTreeView::item_for_proxy_index(const QModelIndex& proxy_index) const
{
    if (!m_proxy || !m_source || !proxy_index.isValid())
        return {};
    return m_source->item_for_index(m_proxy->mapToSource(proxy_index));
}

void SceneOutlinerTreeView::set_busy_items(const SceneItemList& items)
{
    SceneItemSet next(items.begin(), items.end());
    if (next == m_busy_items)
        return;
    m_busy_items = std::move(next);

    if (!m_busy_timer)
    {
        m_busy_timer = new QTimer(this);
        m_busy_timer->setInterval(90);
        connect(m_busy_timer, &QTimer::timeout, this, [this] {
            m_busy_phase = (m_busy_phase + 1) % 12;
            viewport()->update();
        });
    }
    if (m_busy_items.empty())
        m_busy_timer->stop();
    else if (!m_busy_timer->isActive())
        m_busy_timer->start();
    viewport()->update();
}

void SceneOutlinerTreeView::draw_spinner(QPainter* painter, int cx, int cy, int radius) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(146, 166, 179), 1.5); // same accent as the branch highlight
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    const QRectF box(cx - radius, cy - radius, radius * 2.0, radius * 2.0);
    const int start_deg = (m_busy_phase * 30) % 360;
    painter->drawArc(box, -start_deg * 16, 270 * 16); // Qt angles: 1/16 degree, CCW from 3 o'clock
    painter->restore();
}

void SceneOutlinerTreeView::on_expanded(const QModelIndex& index)
{
    if (!m_restoring)
    {
        const SceneItemId item = item_for_proxy_index(index);
        if (item)
            m_expanded.insert(item);
    }
    // Emit once per action, including recursive expansion.
    const bool user_action = !m_restoring && !m_recursive_toggle;
    if (user_action && (QApplication::keyboardModifiers() & Qt::ShiftModifier))
    {
        const QPersistentModelIndex retained(index);
        QTimer::singleShot(0, this, [this, retained]() {
            if (retained.isValid() && retained.model() == m_proxy)
                expand_branch(item_for_proxy_index(retained));
        });
        return;
    }
    if (user_action)
        Q_EMIT expansion_changed();
}

void SceneOutlinerTreeView::on_collapsed(const QModelIndex& index)
{
    if (!m_restoring)
    {
        const SceneItemId item = item_for_proxy_index(index);
        if (item)
            m_expanded.erase(item);
    }
    const bool user_action = !m_restoring && !m_recursive_toggle;
    if (!m_recursive_toggle && (QApplication::keyboardModifiers() & Qt::ShiftModifier))
    {
        const BranchOperation operation(this);
        m_recursive_toggle = true;
        collapse_recursively(index, operation);
        if (!operation.is_valid())
            return;
    }
    if (user_action)
        Q_EMIT expansion_changed();
}

void SceneOutlinerTreeView::expand_items(const SceneItemList& items)
{
    if (items.empty())
        return;
    const QPointer<SceneOutlinerTreeView> self(this);
    // Batch signals while still letting on_expanded record each expanded item.
    m_recursive_toggle = true;
    for (const SceneItemId& item : items)
    {
        const QModelIndex index = proxy_index_for_item(item);
        if (!self)
            return;
        if (index.isValid())
            setExpanded(index, true);
        if (!self)
            return;
    }
    m_recursive_toggle = false;
    Q_EMIT expansion_changed();
}

SceneItemList SceneOutlinerTreeView::expanded_items() const
{
    return SceneItemList(m_expanded.begin(), m_expanded.end());
}

void SceneOutlinerTreeView::set_expanded_items(const SceneItemList& items)
{
    const BranchOperation operation(this);
    // Reset the view and the tracked set without mutating either via the expand/collapse slots.
    m_restoring = true;
    collapseAll();
    if (!operation.is_current())
        return;
    m_expanded.clear();
    for (const SceneItemId& item : items)
    {
        if (item)
            m_expanded.insert(item);
    }
    if (operation.is_valid())
    {
        const auto expanded = m_expanded;
        for (const SceneItemId& item : expanded)
        {
            m_source->ensure_index_for_item(item);
            if (!operation.is_valid())
                break;
        }
        if (operation.is_valid())
            restore_expansion(QModelIndex());
    }
    if (operation.view)
        m_restoring = operation.restoring;
    if (operation.is_current())
        Q_EMIT expansion_changed();
}

QModelIndex SceneOutlinerTreeView::proxy_index_for_item(const SceneItemId& item)
{
    if (!m_source || !m_proxy || !item)
        return {};
    const QPointer<SceneOutlinerTreeView> self(this);
    const auto model = m_source;
    const auto proxy = m_proxy;
    const QModelIndex source = model->ensure_index_for_item(item, 0);
    if (!self || model != m_source || proxy != m_proxy || !source.isValid())
        return {};
    return proxy->mapFromSource(source);
}

bool SceneOutlinerTreeView::collect_reveal_ancestors(const SceneItemId& root, const ItemPredicate& predicate, SceneItemSet& out,
                                                     SceneItemSet& visited, const BranchOperation& operation) const
{
    if (!operation.is_valid() || !root || !predicate)
        return false;
    struct Frame
    {
        SceneItemId item;
        SceneItemList children;
        size_t next_child = 0;
        bool entered = false;
        bool has_match = false;
    };
    std::vector<Frame> stack { { root } };
    while (!stack.empty())
    {
        auto& frame = stack.back();
        if (!frame.entered)
        {
            frame.entered = true;
            if (!frame.item || !visited.insert(frame.item).second)
            {
                stack.pop_back();
                continue;
            }
            const bool matches = predicate(frame.item);
            if (!operation.is_valid())
                return false;
            if (matches)
            {
                stack.pop_back();
                if (stack.empty())
                    return true;
                stack.back().has_match = true;
                continue;
            }
            // Collapsed branches may not exist in the Qt model.
            frame.children = operation.source->children(frame.item);
            if (!operation.is_valid())
                return false;
        }
        if (frame.next_child < frame.children.size())
        {
            const auto child = frame.children[frame.next_child++];
            stack.push_back({ child });
            continue;
        }
        const bool matched = frame.has_match;
        if (matched)
            out.insert(frame.item);
        stack.pop_back();
        if (!operation.is_valid())
            return false;
        if (stack.empty())
            return matched;
        stack.back().has_match |= matched;
    }
    return false;
}

void SceneOutlinerTreeView::expandAll()
{
    const BranchOperation operation(this);
    if (!operation.is_valid())
        return;
    if (m_source->root_item())
        expand_branch(m_source->root_item());
    else
    {
        const auto roots = operation.source->roots();
        if (!operation.is_valid())
            return;
        for (const auto& root : roots)
        {
            expand_branch(root);
            if (!operation.is_valid())
                return;
        }
    }
}

void SceneOutlinerTreeView::expand_branch(const SceneItemId& item)
{
    const BranchOperation operation(this);
    SceneItemList pending { item };
    SceneItemSet visited;
    while (!pending.empty() && operation.is_valid())
    {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second)
            continue;
        const auto source = m_source->ensure_index_for_item(current);
        if (!operation.is_valid())
            return;
        auto* proxy = m_source->proxy_from_index(source);
        if (!proxy)
            continue;
        if (!m_source->load_children(proxy) || !operation.is_valid())
            return;
        const int count = m_source->rowCount(source);
        for (int row = 0; row < count; ++row)
            pending.push_back(m_source->item_at_row(row, source));
    }
    const QModelIndex index = proxy_index_for_item(item);
    if (!operation.is_valid() || !index.isValid())
        return;
    m_recursive_toggle = true;
    expandRecursively(index);
    if (!operation.is_valid())
        return;
    m_recursive_toggle = false;
    Q_EMIT expansion_changed();
}

void SceneOutlinerTreeView::collapse_branch(const SceneItemId& item)
{
    const BranchOperation operation(this);
    const QModelIndex index = proxy_index_for_item(item);
    if (!operation.is_valid() || !index.isValid())
        return;
    m_recursive_toggle = true;
    collapse_recursively(index, operation);
    if (operation.is_valid())
    {
        m_recursive_toggle = operation.recursive_toggle;
        Q_EMIT expansion_changed();
    }
}

void SceneOutlinerTreeView::expand_location(const SceneItemId& item)
{
    const QModelIndex index = proxy_index_for_item(item);
    if (index.isValid())
        expand(index);
}

void SceneOutlinerTreeView::collapse_location(const SceneItemId& item)
{
    const QModelIndex index = proxy_index_for_item(item);
    if (index.isValid())
        collapse(index);
}

void SceneOutlinerTreeView::expand_branch_to(const SceneItemId& item, const ItemPredicate& predicate)
{
    const BranchOperation operation(this);
    SceneItemSet reveal;
    SceneItemSet visited;
    collect_reveal_ancestors(item, predicate, reveal, visited, operation);
    if (!operation.is_valid() || reveal.empty())
        return;
    m_recursive_toggle = true;
    for (const SceneItemId& ancestor : reveal)
    {
        const QModelIndex index = proxy_index_for_item(ancestor);
        if (!operation.is_valid())
            return;
        if (index.isValid())
            setExpanded(index, true);
        if (!operation.is_valid())
            return;
    }
    m_recursive_toggle = operation.recursive_toggle;
    Q_EMIT expansion_changed();
}

void SceneOutlinerTreeView::collapse_branch_to(const SceneItemId& item, const ItemPredicate& predicate)
{
    const BranchOperation operation(this);
    const QModelIndex root = proxy_index_for_item(item);
    if (!operation.is_valid() || !root.isValid())
        return;
    const QPersistentModelIndex persistent_root(root);
    SceneItemSet reveal;
    SceneItemSet visited;
    collect_reveal_ancestors(item, predicate, reveal, visited, operation);
    if (!operation.is_valid() || !persistent_root.isValid())
        return;
    m_recursive_toggle = true;
    // Collapse unrelated branches and stop expansion at each matching item.
    collapse_recursively(persistent_root, operation);
    if (!operation.is_valid())
        return;
    for (const SceneItemId& ancestor : reveal)
    {
        const QModelIndex index = proxy_index_for_item(ancestor);
        if (!operation.is_valid())
            return;
        if (index.isValid())
            setExpanded(index, true);
        if (!operation.is_valid())
            return;
    }
    m_recursive_toggle = operation.recursive_toggle;
    Q_EMIT expansion_changed();
}

void SceneOutlinerTreeView::collapse_recursively(const QModelIndex& index, const BranchOperation& operation)
{
    if (!operation.is_valid() || !index.isValid())
        return;
    const QPersistentModelIndex current(index);
    const int rows = operation.proxy->rowCount(current);
    if (!operation.is_valid() || !current.isValid())
        return;
    for (int row = 0; row < rows; ++row)
    {
        const QModelIndex child = operation.proxy->index(row, 0, current);
        if (!operation.is_valid() || !current.isValid())
            return;
        if (isExpanded(child))
            collapse_recursively(child, operation);
        if (!operation.is_valid() || !current.isValid())
            return;
    }
    collapse(current);
}

void SceneOutlinerTreeView::on_data_source_changed()
{
    if (m_source && m_source_context != m_source->m_source_context)
    {
        m_source_context = m_source->m_source_context;
        m_expanded.clear();
        if (m_show_root && m_source->root_item())
            m_expanded.insert(m_source->root_item());
        set_busy_items({});
    }
}

void SceneOutlinerTreeView::on_model_reset()
{
    on_data_source_changed();
    on_layout_changed({});
}

void SceneOutlinerTreeView::on_layout_changed(const QList<QPersistentModelIndex>& parents)
{
    if (!m_proxy)
        return;
    const BranchOperation operation(this);
    m_restoring = true;
    if (parents.empty())
        restore_expansion(QModelIndex());
    else
        for (const auto& parent : parents)
        {
            if (parent.isValid() && parent.model() == m_proxy &&
                (parent == rootIndex() || m_expanded.find(item_for_proxy_index(parent)) != m_expanded.end()))
            {
                setExpanded(parent, true);
                if (!operation.is_valid())
                    return;
                restore_expansion(parent);
            }
            if (!operation.is_valid())
                return;
        }
}

void SceneOutlinerTreeView::restore_expansion(const QModelIndex& proxy_parent)
{
    if (!m_proxy || !m_source)
        return;
    const BranchOperation operation(this);
    const QPersistentModelIndex parent_index(proxy_parent);
    const bool has_parent = proxy_parent.isValid();
    std::vector<std::pair<int, QPersistentModelIndex>> targets;
    const auto expanded = m_expanded;
    for (const auto& item : expanded)
    {
        const auto source = m_source->ensure_index_for_item(item);
        if (!operation.is_valid() || (has_parent && !parent_index.isValid()))
            return;
        const auto index = m_proxy->mapFromSource(source);
        if (!operation.is_valid())
            return;
        if (!index.isValid() || index == parent_index)
            continue;
        int depth = 0;
        bool under_parent = !has_parent;
        for (auto parent = index.parent(); parent.isValid(); parent = parent.parent())
        {
            ++depth;
            under_parent |= parent == parent_index;
        }
        if (under_parent)
            targets.emplace_back(depth, index);
    }
    std::stable_sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    for (const auto& target : targets)
    {
        if (target.second.isValid())
            setExpanded(target.second, true);
        if (!operation.is_valid())
            return;
    }
}

void SceneOutlinerTreeView::on_rows_inserted(const QModelIndex& parent, int first, int last)
{
    if (m_restoring || !m_source || !m_proxy)
        return;
    const BranchOperation operation(this);
    m_restoring = true;
    const auto expanded = m_expanded;
    for (const auto& item : expanded)
    {
        const auto index = m_proxy->mapFromSource(m_source->index_for_item(item));
        if (index.isValid() && index.parent() == parent && index.row() >= first && index.row() <= last)
        {
            setExpanded(index, true);
            if (!operation.is_valid())
                return;
            restore_expansion(index);
            if (!operation.is_valid())
                return;
        }
    }
}

OPENDCC_NAMESPACE_CLOSE
