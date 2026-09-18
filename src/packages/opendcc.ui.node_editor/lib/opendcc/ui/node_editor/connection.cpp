// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/node_editor/connection.h"
#include "opendcc/ui/node_editor/node.h"
#include "opendcc/ui/node_editor/scene.h"
#include "opendcc/ui/node_editor/graph_model.h"
#include "opendcc/ui/node_editor/view.h"
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QDebug>
#include <QTimer>
#include <cmath>

OPENDCC_NAMESPACE_OPEN

namespace
{
    // TODO: get these values from Style class
    static const QColor s_display_color = QColor(112, 136, 163);
    static const QColor h_display_color = QColor(99, 176, 252);
    static constexpr qreal s_pen_width = 1.8;
    const Qt::PenStyle s_pen_style = Qt::PenStyle::DashLine;
    const Qt::PenCapStyle s_pen_cap_style = Qt::RoundCap;

    void make_horizontal_path(const QPointF& start_pos, const QPointF& end_pos, bool from_input_port, QPainterPath& path)
    {
        auto ctr_offset_x1 = start_pos.x();
        auto ctr_offset_x2 = end_pos.x();
        auto tangent = std::abs(ctr_offset_x1 - ctr_offset_x2);

        tangent = std::min(tangent, 150.0);
        if (from_input_port)
        {
            ctr_offset_x1 -= tangent;
            ctr_offset_x2 += tangent;
        }
        else
        {
            ctr_offset_x1 += tangent;
            ctr_offset_x2 -= tangent;
        }

        const QPointF ctr_point1(ctr_offset_x1, start_pos.y());
        const QPointF ctr_point2(ctr_offset_x2, end_pos.y());
        path.cubicTo(ctr_point1, ctr_point2, end_pos);
    }

    void make_vertical_path(const QPointF& start_pos, const QPointF& end_pos, bool from_input_port, QPainterPath& path)
    {
        auto ctr_offset_y1 = start_pos.y();
        auto ctr_offset_y2 = end_pos.y();
        auto tangent = std::abs(ctr_offset_y1 - ctr_offset_y2);

        tangent = std::min(tangent, 30.0);
        if (from_input_port)
        {
            ctr_offset_y1 -= tangent;
            ctr_offset_y2 += tangent;
        }
        else
        {
            ctr_offset_y1 += tangent;
            ctr_offset_y2 -= tangent;
        }

        QPointF ctr_point1(start_pos.x(), ctr_offset_y1);
        QPointF ctr_point2(end_pos.x(), ctr_offset_y2);
        path.cubicTo(ctr_point1, ctr_point2, end_pos);
    }

    QPainterPath make_painter_path(const QPointF& start_pos, const QPointF& end_pos, bool from_input_port, bool horizontal)
    {
        QPainterPath path;
        path.moveTo(start_pos);

        if (horizontal)
            make_horizontal_path(start_pos, end_pos, from_input_port, path);
        else
            make_vertical_path(start_pos, end_pos, from_input_port, path);
        return path;
    }

};

BasicConnectionItem::BasicConnectionItem(const GraphModel& graph_model, const ConnectionId& connection_id, bool horizontal)
    : ConnectionItem(graph_model, connection_id)
    , m_path_item(new QGraphicsPathItem(this))
    , m_horizontal(horizontal)
{
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsSelectable, true);
    setAcceptHoverEvents(true);
    setZValue(2);
    QPen pen;
    pen.setColor(s_display_color);
    pen.setCapStyle(s_pen_cap_style);
    m_path_item->setPen(pen);
}

BasicConnectionItem::~BasicConnectionItem() {}

void BasicConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {}

QVariant BasicConnectionItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == QGraphicsItem::GraphicsItemChange::ItemSelectedHasChanged)
    {
        QPen pen;
        pen.setColor(isSelected() ? s_display_color.lighter() : s_display_color);
        pen.setCapStyle(s_pen_cap_style);
        m_path_item->setPen(pen);
    }
    return QGraphicsItem::itemChange(change, value);
}

void BasicConnectionItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    QPen pen;
    pen.setColor(h_display_color);
    m_path_item->setPen(pen);
    Q_EMIT get_scene() -> connection_hovered(get_id());
    QGraphicsItem::hoverEnterEvent(event);
}

void BasicConnectionItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    QPen pen;
    pen.setColor(s_display_color);
    m_path_item->setPen(pen);
    Q_EMIT get_scene() -> connection_hovered(get_id(), false);
    QGraphicsItem::hoverLeaveEvent(event);
}

QPainterPath BasicConnectionItem::shape() const
{
    return m_shape;
}

QRectF BasicConnectionItem::boundingRect() const
{
    return m_bbox;
}

void BasicConnectionItem::set_start_pos(const QPointF& start_pos)
{
    m_start_pos = start_pos;
    m_path_item->setPath(make_painter_path(m_start_pos, m_end_pos, false, m_horizontal));
    m_shape = m_path_item->shape();
    m_bbox = m_path_item->boundingRect();
    prepareGeometryChange();
}

void BasicConnectionItem::set_end_pos(const QPointF& end_pos)
{
    m_end_pos = end_pos;
    m_path_item->setPath(make_painter_path(m_start_pos, m_end_pos, false, m_horizontal));
    m_shape = m_path_item->shape();
    m_bbox = m_path_item->boundingRect();
    prepareGeometryChange();
}

BasicLiveConnectionItem::BasicLiveConnectionItem(GraphModel& model, const QPointF& start_pos, const Port& source_port, ConnectionSnapper* snapper,
                                                 bool horizontal)
    : QGraphicsPathItem()
    , m_model(model)
    , m_snapper(snapper)
    , m_start_pos(start_pos)
    , m_end_pos(start_pos)
    , m_source_port(source_port)
    , m_horizontal(horizontal)
{
    setZValue(4);
    auto dash_timer = new QTimer(this);
    connect(dash_timer, &QTimer::timeout, [this] {
        m_dash_offset -= 7.5;
        update();
    });
    dash_timer->start(100);
    if (m_snapper)
        m_snapper->setParent(this);
}

BasicLiveConnectionItem::~BasicLiveConnectionItem() {}

const Port& BasicLiveConnectionItem::get_source_port() const
{
    return m_source_port;
}

const QPointF& BasicLiveConnectionItem::get_end_pos() const
{
    return m_end_pos;
}

const QPointF& BasicLiveConnectionItem::get_start_pos() const
{
    return m_start_pos;
}

void BasicLiveConnectionItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
    Q_EMIT mouse_pressed(event);
}

void BasicLiveConnectionItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    m_end_pos = event->scenePos();
    if (m_snapper)
        m_end_pos = m_snapper->try_snap(*this);
    setPath(make_painter_path(m_start_pos, m_end_pos, m_source_port.type == Port::Type::Input, m_horizontal));
}

void BasicLiveConnectionItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    Q_EMIT mouse_released(event);
}

NodeEditorScene* BasicLiveConnectionItem::get_scene() const
{
    return static_cast<NodeEditorScene*>(scene());
}

void BasicLiveConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    assert(scene());

    QPen pen(s_display_color, s_pen_width);
    pen.setStyle(s_pen_style);
    pen.setCapStyle(s_pen_cap_style);
    pen.setDashOffset(m_dash_offset);

    painter->save();
    painter->setPen(pen);

    painter->setRenderHint(QPainter::RenderHint::Antialiasing, true);
    painter->drawPath(path());
    painter->restore();
}

//------------------------------------------------------------------------------------------------

PreConnectionSnapper::PreConnectionSnapper()
{
    m_snap_pen.setColor(s_display_color.lighter());
    m_snap_pen.setWidth(s_pen_width);
    m_snap_pen.setCapStyle(s_pen_cap_style);
}

PreConnectionSnapper::~PreConnectionSnapper()
{
    clear_pre_connection_line();
    delete m_start_pre_connection;
    delete m_end_pre_connection;
}

bool PreConnectionSnapper::is_equal_connections_pos(BasicConnectionItem* connection)
{
    if (!m_base_connection || !connection)
    {
        return false;
    }

    if (m_base_connection != connection)
    {
        return false;
    }

    bool is_equal_start_pos = connection->get_start_pos() == m_connection_start_pos;
    bool is_equal_ens_pos = connection->get_end_pos() == m_connection_end_pos;
    return is_equal_start_pos && is_equal_ens_pos;
}

void PreConnectionSnapper::update_cover_connection(BasicConnectionItem* connection, const QPointF& cursor_pos)
{
    update_cover_connection(connection, cursor_pos, cursor_pos);
}

void PreConnectionSnapper::update_cover_connection(BasicConnectionItem* connection, const QPointF& input_pos, const QPointF& output_pos)
{
    if (!connection)
    {
        return;
    }

    if (m_base_connection != connection)
    {
        m_base_connection = connection;
    }

    clear_pre_connection_line();

    if (!is_equal_connections_pos(connection))
    {
        m_connection_start_pos = m_base_connection->get_start_pos();
        m_connection_end_pos = m_base_connection->get_end_pos();
    }

    bool is_horizontal = m_base_connection->is_horizontal();

    auto start_path = make_painter_path(m_connection_start_pos, input_pos, false, is_horizontal);
    auto end_path = make_painter_path(m_connection_end_pos, output_pos, true, is_horizontal);

    m_start_pre_connection = m_base_connection->get_scene()->addPath(start_path, m_snap_pen);
    m_end_pre_connection = m_base_connection->get_scene()->addPath(end_path, m_snap_pen);
}

void PreConnectionSnapper::clear_pre_connection_line()
{
    if (m_start_pre_connection && m_start_pre_connection->scene())
    {
        m_start_pre_connection->scene()->removeItem(m_start_pre_connection);
        if (!m_connection_start_pos.isNull())
        {
            m_connection_start_pos = QPointF(); // clear the pos
        }
    }

    if (m_end_pre_connection && m_end_pre_connection->scene())
    {
        m_end_pre_connection->scene()->removeItem(m_end_pre_connection);
        if (!m_connection_end_pos.isNull())
        {
            m_connection_end_pos = QPointF(); // clear the pos
        }
    }
}

OPENDCC_NAMESPACE_CLOSE
