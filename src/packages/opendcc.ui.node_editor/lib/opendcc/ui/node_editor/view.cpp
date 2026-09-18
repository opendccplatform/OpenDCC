// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/node_editor/view.h"
#include "opendcc/ui/node_editor/scene.h"
#include "opendcc/ui/node_editor/connection.h"
#include "opendcc/ui/node_editor/node.h"
#include "opendcc/ui/node_editor/tab_search.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsRectItem>
#include <QPixmap>

#include <QDebug>
#include <QStyleOption>
#include <QApplication>
#include <QVBoxLayout>

OPENDCC_NAMESPACE_OPEN

NodeEditorView::NodeEditorView(NodeEditorScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    assert(scene);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);

    setMouseTracking(true);
    setInteractive(true);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::DragMode::NoDrag);
    setCacheMode(CacheBackground);

    m_hint = new BottomHintWidget(this);
    m_hint->setVisible(true);

    m_scene_range = QRectF(0, 0, size().width(), size().height());
    update_scene();
}

NodeEditorView::~NodeEditorView() {}

bool NodeEditorView::focusNextPrevChild(bool next)
{
    // Override Tab key
    return false;
}

NodeItem* NodeEditorView::node_at(const QPoint& pos) const
{
    for (const auto& item : items(pos))
    {
        if (auto node = qgraphicsitem_cast<NodeItem*>(item))
            return node;
    }
    return nullptr;
}

void NodeEditorView::mousePressEvent(QMouseEvent* event)
{
    m_mouse_press_view_pos = event->pos();
    m_mouse_press_scene_pos = mapToScene(event->pos());
    QGraphicsView::mousePressEvent(event);
    if (event->isAccepted())
        return;

    if (event->buttons() == Qt::MiddleButton)
    {
        m_last_mouse_pos = mapToScene(event->pos());
        setInteractive(false);
        viewport()->setCursor(Qt::ClosedHandCursor);
        m_pan_mode = true;
        event->accept();
        return;
    }

    if (event->buttons() == Qt::LeftButton && !m_rubber_banding)
    {
        m_rubber_banding = true;
        m_rubber_band_rect = QRect();
        const auto extend = (event->modifiers() & Qt::ShiftModifier) != 0;
        if (extend)
            m_selection_operation = Qt::ItemSelectionOperation::AddToSelection;
        else
            m_selection_operation = Qt::ItemSelectionOperation::ReplaceSelection;
        event->accept();
    }
}

void NodeEditorView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_rubber_banding)
    {
        update_rubber_band(event);
    }
    else if (event->buttons() == Qt::MiddleButton && m_pan_mode)
    {
        setInteractive(false);
        auto pos = m_last_mouse_pos - mapToScene(event->pos());
        m_scene_range.translate(pos.x(), pos.y());
        update_scene();
    }
    else
    {
        QGraphicsView::mouseMoveEvent(event);
    }
}

void NodeEditorView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_rubber_banding && ((event->buttons() & Qt::LeftButton) == 0))
    {
        clear_rubber_band();
        return;
    }

    if (m_pan_mode)
    {
        m_pan_mode = false;
        setInteractive(true);
        viewport()->setCursor(Qt::ArrowCursor);
    }
    else
    {
        QGraphicsView::mouseReleaseEvent(event);
    }

    clear_align_lines();
}

void NodeEditorView::wheelEvent(QWheelEvent* event)
{
    QGraphicsView::wheelEvent(event);
    if (event->isAccepted())
        return;

    const double delta = 1.0 - (event->angleDelta().y() * 0.00125);

    const float new_w = m_scene_range.width() * delta;
    const float new_h = m_scene_range.height() * delta;

    // QWheelEvent::pos() was removed in Qt6; position() is QPointF, present since Qt 5.14
    const float pos_x = event->position().x();
    const float pos_y = event->position().y();
    const float w = width();
    const float h = height();

    if (new_w / w < 0.55 || new_h / h < 0.55)
        return;

    const float new_x = (float)m_scene_range.x() + ((m_scene_range.width() - new_w) * pos_x / w);
    const float new_y = (float)m_scene_range.y() + ((m_scene_range.height() - new_h) * pos_y / h);

    m_scene_range = QRectF(new_x, new_y, new_w, new_h);

    update_scene();
}

void NodeEditorView::resizeEvent(QResizeEvent* event)
{
    float f = event->oldSize().width();
    if (event->oldSize().width() > 1 && event->oldSize().height() > 1)
    {
        m_scene_range = QRectF(m_scene_range.x(), m_scene_range.y(), m_scene_range.width() * event->size().width() / event->oldSize().width(),
                               m_scene_range.height() * event->size().height() / event->oldSize().height());

        setSceneRect(m_scene_range);
    }
    Q_EMIT rect_changed();
    QGraphicsView::resizeEvent(event);
}

void NodeEditorView::set_tab_menu(TabSearchMenu* tab_menu)
{
    m_tab_menu = tab_menu;
}

TabSearchMenu* NodeEditorView::get_tab_menu() const
{
    return m_tab_menu;
}

BottomHintWidget* NodeEditorView::get_hint_widget()
{
    return m_hint;
}

void NodeEditorView::keyPressEvent(QKeyEvent* event)
{
    QGraphicsView::keyPressEvent(event);
    if (event->isAccepted())
    {
        return;
    }

    if (event->key() == Qt::Key_F)
    {
        fit_to_view();
    }
    else if (m_tab_menu && event->key() == Qt::Key_Tab && !m_tab_menu->isVisible())
    {
        m_tab_menu->exec(QCursor::pos());
    }
}

void NodeEditorView::fit_to_view()
{
    auto selected = scene()->selectedItems();
    if (selected.size() == 0)
        selected = scene()->items();

    if (selected.size() == 1)
    {
        m_scene_range = QRectF(0, 0, size().width(), size().height());
        m_scene_range.translate(selected[0]->sceneBoundingRect().center() - m_scene_range.center());
        update_scene();
    }
    else if (selected.size() > 1)
    {
        QRectF combined;
        for (auto item : selected)
        {
            combined = combined.united(item->sceneBoundingRect());
        }

        const auto alpha = (qreal)size().width() / size().height();
        const auto new_w = std::max((qreal)combined.width() * 1.1f, (qreal)size().width());
        const auto new_h = std::max((qreal)combined.height() * 1.1f, (qreal)size().height());
        const auto alpha_new = new_w / new_h;

        if (alpha_new > alpha)
            m_scene_range = QRectF(0, 0, new_w, new_w / alpha);
        else
            m_scene_range = QRectF(0, 0, new_h * alpha, new_h);

        m_scene_range.translate(combined.center() - m_scene_range.center());
        update_scene();
    }
}

NodeEditorScene* NodeEditorView::get_node_scene() const
{
    return qobject_cast<NodeEditorScene*>(scene());
}

void NodeEditorView::set_grid_type(GridType grid_type)
{
    m_grid_type = grid_type;
}

const std::unique_ptr<AlignSnapper>& NodeEditorView::get_align_snapper() const
{
    return m_align_snapper;
}

std::unique_ptr<AlignSnapper>& NodeEditorView::get_align_snapper()
{
    return m_align_snapper;
}

void NodeEditorView::clear_align_lines()
{
    if (m_align_snapper != nullptr)
    {
        m_align_snapper->clear_snap_lines();
    }
}

void NodeEditorView::enable_align_snapping(bool isEnabled)
{
    if (isEnabled && m_align_snapper == nullptr)
    {
        m_align_snapper = std::make_unique<AlignSnapper>(*this);
    }
    else if (!isEnabled && m_align_snapper != nullptr)
    {
        m_align_snapper.reset();
    }
}

void NodeEditorView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);

    if (m_grid_type == GridType::NoGrid)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setBrush(backgroundBrush());
    const int grid_size = 50;
    auto pen = QPen(QColor(75, 75, 75), 0.65);

    if (m_grid_type == GridType::GridLines)
        draw_grid_lines(painter, rect, pen, grid_size);

    if (m_grid_type == GridType::GridPoints)
    {
        const int point_size = 10;
        draw_grid_points(painter, rect, pen, grid_size, point_size);
    }

    painter->restore();
}

void NodeEditorView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
    if (!m_rubber_banding || m_rubber_band_rect.isEmpty())
        return;

    QPainter painter(viewport());
    QStyleOptionRubberBand option;
    option.initFrom(viewport());
    option.rect = m_rubber_band_rect;
    option.shape = QRubberBand::Rectangle;

    QStyleHintReturnMask mask;
    // painter clipping for masked rubberbands
    if (viewport()->style()->styleHint(QStyle::SH_RubberBand_Mask, &option, viewport(), &mask))
        painter.setClipRegion(mask.region, Qt::IntersectClip);

    viewport()->style()->drawControl(QStyle::CE_RubberBand, &option, &painter, viewport());
}

void NodeEditorView::update_scene()
{
    setSceneRect(m_scene_range);
    fitInView(m_scene_range, Qt::KeepAspectRatio);
    Q_EMIT scene_rect_changed();
}

void NodeEditorView::update_rubber_band(QMouseEvent* event)
{
    assert(m_rubber_banding);
    if ((m_mouse_press_view_pos - event->pos()).manhattanLength() < QApplication::startDragDistance())
        return;

    update_rubber_band_region();

    if ((event->buttons() & Qt::MouseButton::LeftButton) == 0)
    {
        m_rubber_banding = false;
        m_selection_operation = Qt::ReplaceSelection;
        m_rubber_band_rect = QRect();
        return;
    }

    QRect old_rubber_band = m_rubber_band_rect;
    const auto mp = mapFromScene(m_mouse_press_scene_pos);
    const auto ep = event->pos();
    m_rubber_band_rect = QRect(qMin(mp.x(), ep.x()), qMin(mp.y(), ep.y()), qAbs(mp.x() - ep.x()) + 1, qAbs(mp.y() - ep.y()) + 1);

    update_rubber_band_region();
}

void NodeEditorView::clear_rubber_band()
{
    m_rubber_banding = false;
    if (scene())
    {
        QPainterPath selection_area;
        selection_area.addPolygon(mapToScene(m_rubber_band_rect));
        selection_area.closeSubpath();
        scene()->setSelectionArea(selection_area, m_selection_operation, Qt::ItemSelectionMode::IntersectsItemShape, viewportTransform());
    }

    m_selection_operation = Qt::ReplaceSelection;
    m_rubber_band_rect = QRect();
    viewport()->update();
}

QRegion NodeEditorView::get_rubber_band_region(const QWidget* widget, const QRect& rect) const
{
    QStyleHintReturnMask mask;
    QStyleOptionRubberBand option;
    option.initFrom(widget);
    option.rect = rect;
    option.opaque = false;
    option.shape = QRubberBand::Rectangle;

    QRegion tmp;
    tmp += rect;
    if (widget->style()->styleHint(QStyle::SH_RubberBand_Mask, &option, widget, &mask))
        tmp &= mask.region;
    return tmp;
}

void NodeEditorView::update_rubber_band_region()
{
    if (viewportUpdateMode() != QGraphicsView::ViewportUpdateMode::NoViewportUpdate)
    {
        if (viewportUpdateMode() != QGraphicsView::ViewportUpdateMode::FullViewportUpdate)
            viewport()->update(get_rubber_band_region(viewport(), m_rubber_band_rect.adjusted(-1, -1, 1, 1)));
        else
            update();
    }
}

void NodeEditorView::draw_grid_lines(QPainter* painter, const QRectF& rect, QPen& pen, int grid_size) const
{
    auto left = int(rect.left());
    auto right = int(rect.right());
    auto top = int(rect.top());
    auto bottom = int(rect.bottom());

    auto first_left = left - (left % grid_size);
    auto first_top = top - (top % grid_size);
    QVector<QLineF> lines;
    for (int x = first_left; x <= right; x += grid_size)
        lines.push_back(QLineF(x, top, x, bottom));

    for (int y = first_top; y <= bottom; y += grid_size)
        lines.push_back(QLineF(left, y, right, y));

    painter->setPen(pen);
    painter->drawLines(lines);
}

void NodeEditorView::draw_grid_points(QPainter* painter, const QRectF& rect, QPen& pen, int grid_size, int point_size) const
{
    auto left = int(rect.left());
    auto right = int(rect.right());
    auto top = int(rect.top());
    auto bottom = int(rect.bottom());

    auto first_left = left - (left % grid_size);
    auto first_top = top - (top % grid_size);
    QVector<QLineF> lines;
    const qreal half_point_size = point_size / 2.0f;
    for (int x = first_left; x <= right; x += grid_size)
    {
        for (int y = first_top; y <= bottom; y += grid_size)
        {
            lines.push_back(QLineF(x, y - half_point_size, x, y + half_point_size));
            lines.push_back(QLineF(x - half_point_size, y, x + half_point_size, y));
        }
    }

    painter->setPen(pen);
    painter->drawLines(lines);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

BottomHintWidget::BottomHintWidget(NodeEditorView* view)
    : QWidget(view, Qt::WindowStaysOnTopHint)
    , m_view(view)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setGeometry(view->rect());
    setStyleSheet("font: 16px; background-color: rgba(48, 48, 48, 200);");

    auto layout = new QVBoxLayout();
    setLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);

    m_label = new QLabel(this);
    m_label->setSizePolicy(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Minimum);
    m_label->setUpdatesEnabled(true);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->installEventFilter(this);
    layout->addWidget(m_label, 0, Qt::AlignCenter);

    connect(view, &NodeEditorView::rect_changed, this, &BottomHintWidget::update_rect);
}

void BottomHintWidget::clear_text()
{
    if (m_label && !m_currentText.isEmpty())
    {
        m_koeff = 1;
        m_label->clear();
        m_currentText.clear();

        if (isVisible())
            QWidget::setVisible(false);
    }
}

void BottomHintWidget::update_text(const QString& text)
{
    if (!isVisible() && m_custom_visible)
        QWidget::setVisible(true);

    if (text != m_currentText)
    {

        m_koeff = 1 + text.count("\n");
        m_currentText = text;
        m_label->setText(text);
    }
}

void BottomHintWidget::update_rect()
{
    auto rect = m_view->rect();
    auto label_height = m_koeff * m_label->fontMetrics().height();
    rect.setY(rect.height() - label_height - 5);
    rect.setHeight(label_height);
    setGeometry(rect);
}

void BottomHintWidget::setVisible(bool visible)
{
    m_custom_visible = visible;
    QWidget::setVisible(visible);
}

bool BottomHintWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_label && (event->type() == QEvent::Resize))
    {
        update_rect();
        return true;
    }
    else
        return QObject::eventFilter(obj, event);
}

OPENDCC_NAMESPACE_CLOSE
