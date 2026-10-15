// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/node.h"
#include "opendcc/usd_editor/usd_node_editor/graph_model.h"
#include "opendcc/usd_editor/usd_node_editor/node_disconnect_machine.h"
#include "opendcc/usd_editor/usd_node_editor/disconnect_after_shake_command.h"
#include "opendcc/ui/node_editor/text_item.h"
#include "opendcc/ui/node_editor/scene.h"
#include "opendcc/app/ui/node_icon_registry.h"
#include "usd/usd_fallback_proxy/core/usd_prim_fallback_proxy.h"
#include "opendcc/ui/node_editor/connection.h"
#include "opendcc/ui/node_editor/view.h"
#include "opendcc/base/commands_api/core/block.h"
#include <pxr/base/tf/stl.h>
#include <pxr/usd/usdUI/nodeGraphNodeAPI.h>
#include <pxr/usd/usdUI/backdrop.h>
#include <pxr/usd/usdUI/sceneGraphPrimAPI.h>
#include <pxr/usd/usd/primDefinition.h>
#include <QGraphicsSvgItem>
#include <QPainter>
#include <QFontMetrics>
#include <QGraphicsLinearLayout>
#include <QGraphicsWidget>
#include <QGraphicsSceneMouseEvent>
#include <QStyleOption>
#include <QStyle>
#include <QDrag>
#include <QMimeData>
#include <QMenu>
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "usdUIExt/nodeDisplayGroupUIAPI.h"
#include "opendcc/app/core/undo/block.h"

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN
using base::commands::UndoCommandBlock;
using base::commands::CommandInterface;
using base::commands::CommandArgs;
using base::commands::CommandRegistry;

const int s_time_delay = 500; // msec = 0.5 sec
const qreal s_port_spacing = 5.0;
const qreal s_port_radius = 5.0;
const qreal s_port_width = 10.0;
const qreal s_port_height = 10.0;
const qreal s_node_width = 160.0;
const qreal s_node_height = 35.0;
const qreal s_port_vert_offset = 3;
const qreal s_snapping_dist = 3;
const qreal s_snapping_dist_sq = s_snapping_dist * s_snapping_dist;
const qreal s_selection_pen_width = 2.0;

class NodeItemGeometry
{
public:
    NodeItemGeometry(UsdPrimNodeItemBase* node)
        : m_node(node)
    {
    }
    virtual ~NodeItemGeometry() {}

    virtual void update_ham(const TfToken& expansion_state) = 0;
    virtual void init_name(const std::string& display_name, bool can_rename) = 0;
    virtual void init_path(bool show_full_path) = 0;
    virtual void init_icon(const QString& icon_path) = 0;
    virtual QRectF get_bounding_rect() const = 0;
    virtual void invalidate() = 0;
    virtual QPointF get_header_in_port_center() const = 0;
    virtual QPointF get_header_out_port_center() const = 0;
    virtual void init_layout() = 0;
    virtual QRectF get_body_rect() const = 0;
    virtual bool on_mouse_release(QGraphicsSceneMouseEvent* event) = 0;
    virtual QGraphicsLinearLayout* get_prop_layout() const = 0;
    virtual QGraphicsWidget* get_prop_widget() const = 0;
    QGraphicsTextItem* get_full_path_item() const { return m_full_path_item; }

    QGraphicsTextItem* get_name_item() const { return m_name_item; }
    QGraphicsSvgItem* get_icon_item() const { return m_icon_item; }

protected:
    UsdPrimNodeItemBase* get_node() const { return m_node; }
    void set_full_path_item(QGraphicsTextItem* item) { m_full_path_item = item; }
    void set_name_item(QGraphicsTextItem* item) { m_name_item = item; }
    void set_icon_item(QGraphicsSvgItem* item) { m_icon_item = item; }

private:
    UsdPrimNodeItemBase* m_node = nullptr;
    QGraphicsTextItem* m_full_path_item = nullptr;
    QGraphicsTextItem* m_name_item = nullptr;
    QGraphicsSvgItem* m_icon_item = nullptr;
};

namespace
{

    std::vector<ConnectionId> get_connections_for_port(const QVector<ConnectionId>& connections, const PortId& port)
    {
        std::vector<ConnectionId> result;
        for (auto connection : connections)
        {
            if (connection.start_port == port || connection.end_port == port)
            {
                result.push_back(connection);
            }
        }
        return result;
    }

    std::vector<Port> get_opposite_connection_port_vector(const std::vector<ConnectionId>& connection_vector, const Port& port)
    {
        std::vector<Port> result;

        Port::Type result_type;
        switch (port.type)
        {
        case Port::Input:
        {
            result_type = Port::Output;
            break;
        }
        case Port::Output:
        {
            result_type = Port::Input;
            break;
        }
        default:
        {
            return result;
        }
        }

        for (auto connection : connection_vector)
        {
            Port opposite_port;
            opposite_port.type = result_type;
            if (connection.start_port == port.id)
            {
                opposite_port.id = connection.end_port;
                result.push_back(opposite_port);
            }
            else if (connection.end_port == port.id)
            {
                opposite_port.id = connection.start_port;
                result.push_back(opposite_port);
            }
        }
        return result;
    }

    QPainterPath get_port_shape(const QPointF& pos)
    {
        QPainterPath result;
        result.addEllipse(pos, s_port_radius, s_port_radius);
        return result;
    }

    QString get_ham_icon_for_mode(const TfToken& expansion_state)
    {
        if (expansion_state == UsdUITokens->minimized)
            return ":/icons/node_editor/ham_01";
        else if (expansion_state == UsdUITokens->closed)
            return ":/icons/node_editor/ham_02";
        return ":/icons/node_editor/ham_00";
    }

    QGraphicsTextItem* create_name_item(UsdPrimNodeItemBase* node, const std::string& display_name, bool can_rename)
    {
        if (can_rename)
        {
            return new NodeTextItem(
                display_name.c_str(), *node,
                [node](const QString& new_name) {
                    const auto new_id = SdfPath(node->get_id()).GetParentPath().AppendChild(TfToken(new_name.toLocal8Bit().data())).GetString();
                    return node->get_model().rename(node->get_id(), new_id);
                },
                node);
        }
        else
        {
            auto text_item = new QGraphicsTextItem(display_name.c_str(), node);
            text_item->setTextInteractionFlags(Qt::NoTextInteraction);
            return text_item;
        }
    }

    class HorizontalItemGeometry final : public NodeItemGeometry
    {
    public:
        HorizontalItemGeometry(UsdPrimNodeItemBase* node)
            : NodeItemGeometry(node)
        {
        }
        QGraphicsLinearLayout* get_prop_layout() const override { return m_prop_layout; }
        QGraphicsWidget* get_prop_widget() const override { return m_prop_widget; }
        void update_ham(const TfToken& expansion_state) override
        {
            delete m_ham_item;
            m_ham_item = new QGraphicsSvgItem(get_ham_icon_for_mode(expansion_state), get_node());
            m_ham_item->setScale(0.8);
            const auto ham_rect = m_ham_item->boundingRect();
            m_ham_item->setPos(s_node_width - ham_rect.width() - s_port_width - s_port_spacing, s_port_vert_offset);
        }
        void init_path(bool show_full_path) override
        {
            if (!show_full_path)
            {
                return;
            }

            auto node = get_node();
            auto& model = node->get_model();
            auto full_path_item = new QGraphicsTextItem(model.to_usd_path(node->get_id()).GetText(), node);
            full_path_item->setTextInteractionFlags(Qt::NoTextInteraction);
            full_path_item->setTextWidth(s_node_width);

            const auto text_rect = full_path_item->boundingRect();
            full_path_item->setPos(s_node_width / 2 - text_rect.width() / 2, -0.8 * text_rect.height());
            full_path_item->setParentItem(m_text_item);
            full_path_item->setDefaultTextColor(QColor(109, 180, 189));
            set_full_path_item(full_path_item);
        }
        void init_icon(const QString& icon_path) override
        {
            delete get_icon_item();

            auto svg_item = new QGraphicsSvgItem(icon_path, get_node());
            svg_item->setScale(20.0 / svg_item->boundingRect().width());
            set_icon_item(svg_item);
            svg_item->setPos(s_port_width + s_port_spacing, s_port_vert_offset);
        }
        void init_name(const std::string& display_name, bool can_rename) override
        {
            auto text_item = create_name_item(get_node(), display_name, can_rename);
            const auto text_rect = text_item->boundingRect();
            const auto text_x = s_node_width / 2 - text_rect.width() / 2;
            const auto text_y = -0.8 * text_rect.height();
            text_item->setPos(text_x, text_y);
            set_name_item(text_item);
        }

        void init_layout() override
        {
            m_prop_widget = new QGraphicsWidget(get_node());
            m_prop_layout = new QGraphicsLinearLayout(Qt::Vertical);
            m_prop_layout->setContentsMargins(0, 0, 0, 0);
            m_prop_layout->setSpacing(0);
            m_prop_widget->setLayout(m_prop_layout);
            m_prop_widget->setPos(0, 20);
            m_prop_widget->setContentsMargins(0, 0, 0, 2 * s_port_vert_offset);
        }
        QRectF get_body_rect() const override
        {
            return QRectF(s_port_width / 2, s_selection_pen_width / 2, s_node_width - s_port_width, m_height - s_selection_pen_width);
        }
        QRectF get_bounding_rect() const override { return QRectF(0, 0, s_node_width, m_height); }
        void invalidate() override
        {
            m_prop_layout->invalidate();
            m_prop_widget->adjustSize();
            m_height = m_prop_widget->pos().y() + m_prop_widget->geometry().height() + s_selection_pen_width;
        }
        QPointF get_header_in_port_center() const override
        {
            return QPointF(s_port_width / 2, s_port_height / 2 + s_port_spacing + s_port_vert_offset);
        }
        QPointF get_header_out_port_center() const override
        {
            return QPointF(s_node_width - s_port_width / 2, s_port_height / 2 + s_port_spacing + s_port_vert_offset);
        }
        bool on_mouse_release(QGraphicsSceneMouseEvent* event) override
        {
            const auto ham_rect = m_ham_item->sceneBoundingRect();
            if (ham_rect.contains(event->scenePos()))
            {
                auto node = get_node();
                const auto state = node->get_expansion_state();
                if (state == UsdUITokens->closed)
                    node->set_expansion_state(UsdUITokens->minimized);
                else if (state == UsdUITokens->minimized)
                    node->set_expansion_state(UsdUITokens->open);
                else if (state == UsdUITokens->open)
                    node->set_expansion_state(UsdUITokens->closed);
                return true;
            }
            return false;
        }

    private:
        QGraphicsSvgItem* m_ham_item = nullptr;
        QGraphicsItem* m_text_item = nullptr;
        QGraphicsTextItem* m_full_path_item = nullptr;
        QGraphicsLinearLayout* m_prop_layout = nullptr;
        QGraphicsWidget* m_prop_widget = nullptr;
        int m_height = 0;
    };

    class VerticalItemGeometry final : public NodeItemGeometry
    {
    public:
        VerticalItemGeometry(UsdPrimNodeItemBase* node)
            : NodeItemGeometry(node)
        {
        }

        bool on_mouse_release(QGraphicsSceneMouseEvent* event) override { return false; }
        void update_ham(const TfToken& expansion_state) override
        {
            // doesn't supported in vertical layout
        }
        void init_name(const std::string& display_name, bool can_rename) override
        {
            auto text_item = create_name_item(get_node(), display_name, can_rename);
            const auto text_rect = text_item->boundingRect();
            const auto text_x = s_node_width + 15;
            const auto text_y = s_node_height / 2 - text_rect.height() / 2;
            text_item->setPos(text_x, text_y);
            set_name_item(text_item);
        }
        void init_path(bool show_full_path) override
        {
            if (!show_full_path)
            {
                return;
            }

            auto node = get_node();
            auto& model = node->get_model();
            m_full_path_item = new QGraphicsTextItem(model.to_usd_path(node->get_id()).GetText(), node);
            m_full_path_item->setTextInteractionFlags(Qt::NoTextInteraction);
            m_full_path_item->setTextWidth(s_node_width);

            const auto text_rect = m_full_path_item->boundingRect();
            m_full_path_item->setPos(0, -0.8 * text_rect.height());
            m_full_path_item->setParentItem(get_name_item());
            m_full_path_item->setDefaultTextColor(QColor(109, 180, 189));
        }
        void init_icon(const QString& icon_path) override
        {
            auto icon = new QGraphicsSvgItem(icon_path, get_node());
            icon->setScale(25.0 / icon->boundingRect().width());
            if (auto item = get_icon_item())
            {
                for (const auto& child : item->children())
                {
                    child->setParent(icon);
                }
                delete item;
            }
            set_icon_item(icon);
        }
        QRectF get_bounding_rect() const override
        {
            auto w = std::max(s_node_width, m_prop_layout->count() * (s_port_height + s_port_spacing + 1));
            const auto h = get_icon_item()->y() + get_icon_item()->boundingRect().height() + s_port_spacing + s_port_height;
            return QRectF(0, 0, w, h);
        }
        void invalidate() override
        {
            for (int i = 0; i < m_prop_layout->count(); ++i)
            {
                m_prop_layout->itemAt(i)->updateGeometry();
            }
            m_prop_layout->invalidate();
            m_prop_layout->activate();
            m_prop_widget->adjustSize();
        }
        QPointF get_header_in_port_center() const override { return get_header_out_port_center(); }
        QPointF get_header_out_port_center() const override
        {
            return QPointF(get_bounding_rect().width() / 2, get_bounding_rect().height() + s_port_radius) +
                   QPointF(s_selection_pen_width / 4, s_selection_pen_width / 2);
        }
        void init_layout() override
        {
            m_prop_widget = new QGraphicsWidget(get_node());
            m_prop_layout = new QGraphicsLinearLayout(Qt::Horizontal);
            m_prop_layout->setContentsMargins(0, 0, 0, 0);
            m_prop_layout->setSpacing(0);

            m_prop_widget->setPreferredSize(s_node_width, s_port_height * 2);
            m_prop_widget->setLayout(m_prop_layout);
            m_prop_widget->setPos(0, -s_port_radius);
            m_prop_widget->setContentsMargins(0, 0, 0, 0);
        }
        QRectF get_body_rect() const override
        {
            const auto w = std::max(s_node_width, m_prop_layout->count() * (s_port_height + 1 + s_port_spacing));
            const auto h = get_icon_item()->y() + get_icon_item()->boundingRect().height() + s_port_spacing + s_port_height;
            return QRectF(s_selection_pen_width / 2.0f, s_selection_pen_width / 2.0f, w - s_selection_pen_width, h - s_selection_pen_width);
        }
        QGraphicsLinearLayout* get_prop_layout() const override { return m_prop_layout; }
        QGraphicsWidget* get_prop_widget() const override { return m_prop_widget; }

    private:
        QGraphicsWidget* m_prop_widget = nullptr;
        QGraphicsLinearLayout* m_prop_layout = nullptr;
        QGraphicsTextItem* m_full_path_item = nullptr;
    };

};

class PropertyGroupItem::GroupHeaderWidget : public QGraphicsWidget
{
public:
    GroupHeaderWidget(QGraphicsItem* parent = nullptr)
        : QGraphicsWidget(parent)
    {
        setPreferredWidth(s_node_width / 2.0f);
        setPreferredHeight(s_port_height + s_port_spacing);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAcceptHoverEvents(true);

        m_open_state << QPointF(-s_port_radius + s_port_spacing, s_port_vert_offset) << QPointF(s_port_radius, s_port_height)
                     << QPointF(s_port_width, s_port_vert_offset);

        QTransform transform;
        transform.rotate(-90);

        transform.translate(-s_port_height, 0);

        m_close_state = transform.map(m_open_state);

        m_tooltip_timer = new QTimer(this);
        m_tooltip_timer->setInterval(s_time_delay);
        m_tooltip_timer->callOnTimeout([&, this]() {
            auto view = get_scene()->get_view();
            if (!m_pos_for_tooltip.isNull() && m_pos_for_tooltip == view->mapToScene(view->mapFromGlobal(QCursor::pos())))
            {
                Q_EMIT get_scene() -> group_need_tool_tip(m_text.toStdString());

                m_tooltip_timer->stop();
                m_pos_for_tooltip = QPointF();
            }
            else
            {
                m_pos_for_tooltip = view->mapToScene(view->mapFromGlobal(QCursor::pos()));
                m_tooltip_timer->start();
            }
        });
    }

    NodeEditorScene* get_scene() const { return static_cast<NodeEditorScene*>(scene()); }

    void hoverEnterEvent(QGraphicsSceneHoverEvent* event)
    {
        m_hovered = true;

        auto view = get_scene()->get_view();
        if (m_tooltip_timer && view)
        {
            m_pos_for_tooltip = view->mapToScene(view->mapFromGlobal(QCursor::pos()));
            m_tooltip_timer->start();
        }

        QGraphicsWidget::hoverEnterEvent(event);

        Q_EMIT get_scene() -> group_hovered(m_text.toStdString(), true);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
    {
        m_hovered = false;

        if (m_tooltip_timer)
        {
            m_tooltip_timer->stop();
            m_pos_for_tooltip = QPointF();
        }

        QGraphicsWidget::hoverLeaveEvent(event);

        Q_EMIT get_scene() -> group_hovered(m_text.toStdString(), false);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget /*= nullptr*/) override
    {
        painter->save();

        const auto& display_color = m_hovered ? m_display_color.lighter() : m_display_color;

        painter->setBrush(display_color);
        painter->setPen(display_color);

        painter->drawPolygon(m_opened ? m_open_state : m_close_state);

        const auto rect = boundingRect();
        Qt::Alignment alignment = Qt::AlignLeft;

        const auto fm = painter->fontMetrics();
        const QRectF text_rect = rect.adjusted(s_port_width + s_port_spacing, 0, s_port_width + s_port_spacing, s_port_spacing);
        QString elided_text = fm.elidedText(m_text, Qt::ElideRight, s_node_width / 2.0f);
        painter->drawText(text_rect, elided_text, alignment);
        painter->restore();
    }

    void set_text(const QString& text) { m_text = text; }

    const QString& get_text() const { return m_text; }

    void set_state(bool state)
    {
        m_opened = state;
        update();
    }

private:
    QTimer* m_tooltip_timer = nullptr;
    QPointF m_pos_for_tooltip;

    QString m_text;
    QPolygonF m_open_state;
    QPolygonF m_close_state;
    bool m_opened = true;

    const QColor m_display_color = QColor(179, 179, 179);
    bool m_hovered = false;
};

#pragma region other

MorePortLayoutItem::MorePortLayoutItem(UsdGraphModel& model, UsdPrimNodeItemBase* node, const PortId& id)
    : NamedPropertyLayoutItem(model, node, id, TfToken("more"), Port::Type::Both)
{
}

Port MorePortLayoutItem::get_port_at(const QPointF& point) const
{
    static const Port empty;
    auto result = NamedPropertyLayoutItem::get_port_at(point);
    if (result.type == Port::Type::Unknown)
        return empty;

    return select_port(result.type);
}

bool MorePortLayoutItem::try_snap(const BasicLiveConnectionItem& connection, QPointF& snap_point) const
{
    return false;
}

void MorePortLayoutItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    const auto port_center_y = s_port_vert_offset + s_port_radius;
    Port::Type type = Port::Type::Unknown;
    if ((get_port_type() & Port::Type::Input) && get_port_shape(QPointF(5, port_center_y)).contains(event->scenePos()))
    {
        type = Port::Type::Input;
    }
    else if ((get_port_type() & Port::Type::Output) &&
             get_port_shape(QPointF(boundingRect().width() - s_port_width / 2 - 1, port_center_y)).contains(event->scenePos()))
    {
        type = Port::Type::Output;
    }

    if (type != Port::Type::Unknown)
    {
        const auto port = select_port(type);
        if (port.type != Port::Type::Unknown)
            Q_EMIT get_scene() -> port_pressed(port);
    }
}

Port MorePortLayoutItem::select_port(Port::Type type) const
{
    Port result;

    auto node_layout = get_node_item()->get_prop_layout();
    QMenu more_ports;
    for (int i = 0; i < node_layout->count(); ++i)
    {
        auto layout_item = node_layout->itemAt(i);
        auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(layout_item);
        if (item && ((item->get_port_type() & type) != 0) && !item->has_connections())
        {
            add_property_to_menu(item, &more_ports, type);
        }

        if (auto group = dynamic_cast<PropertyGroupItem*>(layout_item))
        {
            QMenu* group_menu = new QMenu(group->get_group_name());
            more_ports.addMenu(group_menu);

            for (int i = 0; i < group->get_prop_count(); i++)
            {
                auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(group->get_prop_item(i));
                if (item && ((item->get_port_type() & type) != 0) && !item->has_connections())
                {
                    add_property_to_menu(item, group_menu, type);
                }
            }
        }
    }

    if (more_ports.actions().empty())
        return result;

    if (auto selected_port = more_ports.exec(QCursor::pos()))
    {
        result.type = type;
        result.id = selected_port->data().toString().toStdString();
    }
    else
    {
        return result;
    }
    return result;
}

void MorePortLayoutItem::add_property_to_menu(PropertyWithPortsLayoutItem* item, QMenu* menu, Port::Type type) const
{
    auto live_connection = dynamic_cast<BasicLiveConnectionItem*>(get_scene()->get_grabber_item());

    std::function<bool(const Port& dest)> can_connect;
    if (live_connection)
    {
        can_connect = [this, src = live_connection->get_source_port()](const Port& dest) {
            return get_model().can_connect(src, dest);
        };
    }
    else
    {
        can_connect = [](const Port& dest) {
            return true;
        };
    }

    auto display_text = item->data(static_cast<int>(Data::DisplayText));
    auto display_str = display_text.isValid() ? display_text.toString() : QString();
    if (display_str.isEmpty() || item == this)
        return;

    auto action = new QAction(display_str);
    action->setData(item->get_id().data());
    action->setEnabled(can_connect(Port { item->get_id(), type }));

    menu->addAction(action);
}

UsdConnectionSnapper::UsdConnectionSnapper(const NodeEditorView& view, const UsdGraphModel& model)
    : m_view(view)
    , m_model(model)
{
}

QPointF UsdConnectionSnapper::try_snap(const BasicLiveConnectionItem& live_connection)
{
    QRectF snap_rect(live_connection.get_end_pos() - QPointF(s_snapping_dist / 2, s_snapping_dist / 2),
                     live_connection.get_end_pos() + QPointF(s_snapping_dist / 2, s_snapping_dist / 2));

    qreal min_dist_sq = std::numeric_limits<qreal>::max();
    QPointF candidate;
    for (auto item : m_view.items(m_view.mapFromScene(snap_rect)))
    {
        const auto node = dynamic_cast<UsdPrimNodeItemBase*>(item);
        if (!node)
            continue;
        QPointF snap_pos;
        if (node->try_snap(live_connection, snap_pos))
        {
            const auto dir = snap_pos - live_connection.get_end_pos();
            const auto dist_sq = QPointF::dotProduct(dir, dir);
            if (dist_sq < min_dist_sq)
            {
                min_dist_sq = dist_sq;
                candidate = snap_pos;
            }
        }
    }

    return min_dist_sq != std::numeric_limits<qreal>::max() ? candidate : live_connection.get_end_pos();
}

PropertyLayoutItem::PropertyLayoutItem(UsdGraphModel& model, UsdPrimNodeItemBase* node, const PortId& id)
    : QGraphicsLayoutItem()
    , m_model(model)
    , m_node(node)
    , m_id(id)
{
    setGraphicsItem(this);
}

UsdPrimNodeItemBase* PropertyLayoutItem::get_node_item() const
{
    return m_node;
}

PropertyWithPortsLayoutItem::PropertyWithPortsLayoutItem(UsdGraphModel& model, UsdPrimNodeItemBase* node, const PortId& id, Port::Type port_type,
                                                         bool horizontal)
    : PropertyLayoutItem(model, node, id)
    , m_port_type(port_type)
    , m_horizontal(horizontal)
{
    setAcceptHoverEvents(true);
    set_port_brush(QColor(179, 179, 179));
    set_port_pen(QColor(57, 57, 57));

    m_port_tooltip_timer = new QTimer(this);
    m_port_tooltip_timer->setInterval(s_time_delay);
    m_port_tooltip_timer->callOnTimeout([&, this]() {
        auto scene = get_scene();
        if (!scene)
        {
            return;
        }
        auto view = scene->get_view();
        if (!m_pos_for_port_tooltip.isNull() && m_pos_for_port_tooltip == view->mapToScene(view->mapFromGlobal(QCursor::pos())))
        {
            Port port_info;
            port_info.id = get_id();
            port_info.type = get_port_type();
            Q_EMIT get_scene() -> port_need_tool_tip(port_info);

            m_port_tooltip_timer->stop();
            m_pos_for_port_tooltip = QPointF();
        }
        else
        {
            m_pos_for_port_tooltip = view->mapToScene(view->mapFromGlobal(QCursor::pos()));
            m_port_tooltip_timer->start();
        }
    });
}

PropertyWithPortsLayoutItem::~PropertyWithPortsLayoutItem() {}

void PropertyWithPortsLayoutItem::add_connection(ConnectionItem* connection)
{
    auto basic_con = static_cast<BasicConnectionItem*>(connection);
    if (!basic_con)
        return;
    m_connections.insert(basic_con);
    if (basic_con->get_id().start_port == get_id())
        basic_con->set_start_pos(get_out_connection_pos());
    else
        basic_con->set_end_pos(get_in_connection_pos());
}

void PropertyWithPortsLayoutItem::remove_connection(ConnectionItem* connection)
{
    m_connections.remove(connection);
}

bool PropertyWithPortsLayoutItem::has_connections() const
{
    return !m_connections.empty();
}

void PropertyWithPortsLayoutItem::move_connections()
{
    const auto in_pos = get_in_connection_pos();
    const auto out_pos = get_out_connection_pos();
    for (auto connection : m_connections)
    {
        auto basic_con = static_cast<BasicConnectionItem*>(connection);
        if (connection->get_id().start_port == get_id())
            basic_con->set_start_pos(out_pos);
        else
            basic_con->set_end_pos(in_pos);
    }
}

QSet<ConnectionItem*>& PropertyWithPortsLayoutItem::get_connections()
{
    return m_connections;
}

QPointF PropertyWithPortsLayoutItem::get_in_connection_pos() const
{
    if (m_horizontal)
    {
        const auto pos = scenePos();
        return QPointF(pos.x(), pos.y() + s_port_vert_offset + 5);
    }
    else
    {
        return mapToScene(QPointF(boundingRect().width() / 2, 0));
    }
}

QPointF PropertyWithPortsLayoutItem::get_out_connection_pos() const
{
    if (m_horizontal)
    {
        const auto pos = scenePos();
        const auto rect = boundingRect();
        return QPointF(pos.x() + rect.width() - 1, pos.y() + s_port_vert_offset + 5);
    }
    else
    {
        return mapToScene(QPointF(boundingRect().width() / 2, boundingRect().height()));
    }
}

void PropertyWithPortsLayoutItem::draw_port(QPainter* painter, const QStyleOptionGraphicsItem* option, const QPointF& pos) const
{
    painter->setPen(m_port_pen);
    if (!option->state.testFlag(QStyle::State_Enabled))
        painter->setBrush(m_port_brush.color().darker());
    else if (option->state.testFlag(QStyle::State_MouseOver))
        painter->setBrush(m_port_brush.color().lighter());
    else
        painter->setBrush(m_port_brush);

    painter->drawEllipse(pos, get_radius(), get_radius());
}

QPainterPath PropertyWithPortsLayoutItem::get_port_shape(const QPointF& pos) const
{
    QPainterPath result;
    const auto pen_width = m_port_pen.widthF();

    result.addEllipse(mapToScene(pos), get_radius() + pen_width, get_radius() + pen_width);
    return result;
}

QPointF PropertyWithPortsLayoutItem::get_port_center(Port::Type type) const
{
    if (m_horizontal)
    {
        const auto rect = boundingRect();
        const auto port_center_y = s_port_vert_offset + get_radius();
        if (type == Port::Type::Input)
            return QPointF(get_radius(), port_center_y);
        // output
        return QPointF(rect.width() - get_radius() - 1, port_center_y);
    }
    else
    {
        const auto rect = boundingRect();
        return QPointF(rect.width() / 2, rect.height() / 2);
    }
}

QRectF PropertyWithPortsLayoutItem::boundingRect() const
{
    QRectF result(QPointF(0, 0), geometry().size());
    return result;
}

void PropertyWithPortsLayoutItem::setGeometry(const QRectF& rect)
{
    PropertyLayoutItem::setGeometry(rect);
    prepareGeometryChange();
    setPos(rect.topLeft());
}

Port PropertyWithPortsLayoutItem::get_port_at(const QPointF& point) const
{
    Port result;
    result.id = get_id();
    if (get_port_shape(get_port_center(Port::Type::Input)).contains(point))
        result.type = Port::Type::Input;
    else if (get_port_shape(get_port_center(Port::Type::Output)).contains(point))
        result.type = Port::Type::Output;
    else
        result.type = Port::Type::Unknown;
    return std::move(result);
}

Port::Type PropertyWithPortsLayoutItem::get_port_type() const
{
    return m_port_type;
}

bool PropertyWithPortsLayoutItem::try_snap(const BasicLiveConnectionItem& connection, QPointF& snap_point) const
{
    bool snapped = false;
    const auto& source_port = connection.get_source_port();
    const auto& end_pos = connection.get_end_pos();
    const auto rect = boundingRect();
    const auto port_center_y = s_port_vert_offset + get_radius();

    Port end_port;
    end_port.id = get_id();

    auto dist_sq_to_port = [this, &end_pos](const QPointF& port_center) {
        const auto start_to_center = port_center - end_pos;
        const auto dist_sq = QPointF::dotProduct(start_to_center, start_to_center);
        return dist_sq;
    };
    auto can_connect = [this, &source_port, &end_port](Port::Type type) {
        end_port.type = type;
        return (m_port_type & type) && get_model().can_connect(source_port, end_port);
    };
    auto snap = [this, &can_connect, &dist_sq_to_port, &source_port, &end_pos, &end_port, &snapped](Port::Type type, const QPointF& port_center) {
        if (can_connect(type))
        {
            const auto dist_sq = dist_sq_to_port(port_center);
            if (dist_sq < s_snapping_dist_sq)
            {
                snapped = true;

                return true;
            }
        }
        return false;
    };

    const auto input_pos = mapToScene(QPointF(get_radius(), port_center_y));
    const auto output_pos = mapToScene(QPointF(rect.width() - get_radius() - 1, port_center_y));

    if (boundingRect().contains(mapFromScene(end_pos)))
    {
        qreal dist_sq_to_input = std::numeric_limits<qreal>::max();
        qreal dist_sq_to_output = std::numeric_limits<qreal>::max();
        if (can_connect(Port::Type::Input))
            dist_sq_to_input = dist_sq_to_port(input_pos);
        if (can_connect(Port::Type::Output))
            dist_sq_to_output = dist_sq_to_port(output_pos);

        if ((dist_sq_to_input == dist_sq_to_output) && dist_sq_to_input == std::numeric_limits<qreal>::max())
            return false;

        snap_point = dist_sq_to_input < dist_sq_to_output ? get_in_connection_pos() : get_out_connection_pos();
        return true;
    }

    // snap to nearest port in radius
    if (snap(Port::Type::Input, input_pos))
        snap_point = get_in_connection_pos();
    else if (snap(Port::Type::Output, output_pos))
        snap_point = get_out_connection_pos();
    return snapped;
}

QSizeF PropertyWithPortsLayoutItem::sizeHint(Qt::SizeHint which, const QSizeF& constraint /*= QSizeF()*/) const
{
    if (m_horizontal)
    {
        return QSizeF(s_node_width + m_port_pen.widthF(), get_radius() * 2 + m_port_pen.widthF() + s_port_vert_offset);
    }
    else
    {
        qreal port_size;
        if (auto layout = dynamic_cast<QGraphicsLayout*>(parentLayoutItem()))
        {
            const auto widget_width = std::max(s_node_width, layout->count() * (get_radius() * 2 + m_port_pen.widthF() + s_port_spacing));
            port_size = widget_width / layout->count();
        }
        else
        {
            port_size = get_radius() * 2 + m_port_pen.widthF();
        }

        return QSizeF(port_size, get_radius() * 2 + m_port_pen.widthF());
    }
}

qreal PropertyWithPortsLayoutItem::get_port_spacing()
{
    return s_port_spacing;
}

void PropertyWithPortsLayoutItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget /* = nullptr */)
{
    painter->save();

    if (m_port_type & Port::Type::Input)
        draw_port(painter, option, get_port_center(Port::Type::Input)); // QPointF(half_port_width, port_center_y));
    if (m_port_type & Port::Type::Output)
        draw_port(painter, option, get_port_center(Port::Type::Output)); // QPointF(rect.width() - half_port_width - 1, port_center_y));

    painter->restore();
}

const QBrush& PropertyWithPortsLayoutItem::get_port_brush() const
{
    return m_port_brush;
}

const QPen& PropertyWithPortsLayoutItem::get_port_pen() const
{
    return m_port_pen;
}

void PropertyWithPortsLayoutItem::set_port_pen(const QPen& pen)
{
    if (m_port_pen == pen)
        return;

    prepareGeometryChange();
    m_port_pen = pen;
}

void PropertyWithPortsLayoutItem::set_port_brush(const QBrush& brush)
{
    if (m_port_brush == brush)
        return;

    m_port_brush = brush;
    update();
}

void PropertyWithPortsLayoutItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    auto view = get_scene()->get_view();
    if (m_port_tooltip_timer && view)
    {
        m_pos_for_port_tooltip = view->mapToScene(view->mapFromGlobal(QCursor::pos()));
        m_port_tooltip_timer->start();
    }

    if (m_port_type & Port::Type::Input)
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Input;
        Q_EMIT get_scene() -> port_hovered(port);
    }
    else if (m_port_type & Port::Type::Output)
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Output;
        Q_EMIT get_scene() -> port_hovered(port);
    }
    else
    {
        PropertyLayoutItem::hoverEnterEvent(event);
    }
    update();
}

void PropertyWithPortsLayoutItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    if (m_port_tooltip_timer)
    {
        m_port_tooltip_timer->stop();
        m_pos_for_port_tooltip = QPointF();
    }

    if (m_port_type & Port::Type::Input)
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Input;
        Q_EMIT get_scene() -> port_hovered(port, false);
    }
    else if (m_port_type & Port::Type::Output)
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Output;
        Q_EMIT get_scene() -> port_hovered(port, false);
    }
    else
    {
        PropertyLayoutItem::hoverLeaveEvent(event);
    }
    update();
}

void PropertyWithPortsLayoutItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if ((m_port_type & Port::Type::Input) && get_port_shape(get_port_center(Port::Type::Input)).contains(event->scenePos()))
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Input;
        Q_EMIT get_scene() -> port_pressed(port);
    }
    else if ((m_port_type & Port::Type::Output) && get_port_shape(get_port_center(Port::Type::Output)).contains(event->scenePos()))
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Output;
        Q_EMIT get_scene() -> port_pressed(port);
    }
    else
    {
        PropertyLayoutItem::mousePressEvent(event);
    }
}

void PropertyWithPortsLayoutItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    m_scene_mouse_pos = event->scenePos();
    PropertyLayoutItem::mouseMoveEvent(event);
}

void PropertyWithPortsLayoutItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    PropertyLayoutItem::mouseReleaseEvent(event);
}

NamedPropertyLayoutItem::NamedPropertyLayoutItem(UsdGraphModel& model, UsdPrimNodeItemBase* node, const PortId& id, const PXR_NS::TfToken& name,
                                                 Port::Type port_type)
    : PropertyWithPortsLayoutItem(model, node, id, port_type)
    , m_text(name.GetText())
{
    setData(static_cast<int>(Data::DisplayText), m_text);
}

void NamedPropertyLayoutItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget /* = nullptr */)
{
    PropertyWithPortsLayoutItem::paint(painter, option, widget);
    painter->save();
    const auto rect = boundingRect();
    Qt::Alignment alignment = Qt::AlignTop;
    if (get_port_type() == Port::Type::Output)
        alignment |= Qt::AlignRight;
    else
        alignment |= Qt::AlignLeft;
    if (!option->state.testFlag(QStyle::State_Enabled))
    {
        painter->setPen(painter->pen().color().darker());
    }
    else if (option->state.testFlag(QStyle::State_MouseOver))
    {
        painter->setPen(painter->pen().color().lighter());
    }
    else
    {
        painter->setPen(painter->pen().color());
    }

    const auto fm = painter->fontMetrics();
    const QRectF text_rect = rect.adjusted(s_port_width + s_port_spacing, 0, -s_port_width - s_port_spacing, 0);
    QString elided_text = fm.elidedText(m_text, Qt::ElideMiddle, text_rect.width());
    painter->drawText(text_rect, elided_text, alignment);
    painter->restore();
}

UsdLiveNodeItem::UsdLiveNodeItem(UsdGraphModel& model, const PXR_NS::TfToken& name, const PXR_NS::TfToken& type, const SdfPath& parent_path,
                                 bool horizontal, QGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_model(model)
    , m_name(name)
    , m_type(type)
    , m_parent_path(parent_path)
{
    setZValue(3);
    qreal width, height;
    if (horizontal)
    {
        width = (s_node_width - s_port_width);
        auto name_item = new QGraphicsTextItem(name.GetText(), this);
        const auto text_rect = name_item->boundingRect();
        const auto text_x = width / 2 - text_rect.width() / 2;
        const auto text_y = -text_rect.height();
        name_item->setPos(text_x, text_y);

        height = 20;
    }
    else
    {
        width = s_node_width;
        height = s_node_height;

        auto name_item = new QGraphicsTextItem(name.GetText(), this);
        const auto text_rect = name_item->boundingRect();
        const auto text_x = width + 15;
        ;
        const auto text_y = height / 2 - text_rect.height() / 2;
        name_item->setPos(text_x, text_y);
    }

    setRect(0, 0, width, height);

    setBrush(QColor(64, 64, 64));

    setPen(QColor(0, 173, 240));
    m_pre_connection = std::make_unique<PreConnectionSnapper>();
}

void UsdLiveNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

void UsdLiveNodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    auto pos = event->scenePos();
    setPos(pos - QPointF(boundingRect().width() / 2, boundingRect().height() / 2));

    if (auto connection = dynamic_cast<BasicConnectionItem*>(get_scene()->get_connection_item(pos)))
    {
        m_pre_connection->update_cover_connection(connection, pos);
    }
    else
    {
        m_pre_connection->clear_pre_connection_line();
    }

    if (const auto& snapper = get_scene()->get_view()->get_align_snapper())
    {
        const auto snap = snapper->try_snap(*this);
        if (!snap.isNull())
            setPos(snap);
    }
}

void UsdLiveNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    m_pre_connection->clear_pre_connection_line();
    UndoCommandBlock block("create_node_editor_usd_prim");

    auto prim = m_model.create_usd_prim(m_name, m_type, m_parent_path, false);
    if (!prim)
    {
        get_scene()->remove_grabber_item();
        return;
    }

    if (auto node_prim = UsdUINodeGraphNodeAPI::Apply(prim))
    {
        const auto pos = to_model_position(scenePos(), boundingRect().width());
        node_prim.CreatePosAttr(VtValue(GfVec2f(pos.x(), pos.y())));
    }

    on_prim_created(prim);

    if (auto node_item =
            dynamic_cast<UsdPrimNodeItemBase*>(get_scene()->get_item_for_node(m_model.from_usd_path(prim.GetPath(), m_model.get_root()))))
    {
        UsdPrimNodeItemBase::InsertionData data;
        if (node_item->can_insert_into_connection(event->scenePos(), data))
            node_item->insert_node_into_connection(data);
    }

    event->accept();
    get_scene()->remove_grabber_item();

    CommandInterface::execute("select", CommandArgs().arg(prim));
}

NodeEditorScene* UsdLiveNodeItem::get_scene()
{
    return static_cast<NodeEditorScene*>(scene());
}

#pragma endregion
void UsdPrimNodeItemBase::update_expansion_state()
{
    auto expansion_state = get_model().get_expansion_state(get_id());
    if (m_expansion_state == expansion_state)
        return;

    m_expansion_state = expansion_state;
    m_aligner->update_ham(m_expansion_state);
    on_update_expansion_state();
}

void UsdPrimNodeItemBase::update_pos()
{
    const auto pos = get_node_pos();
    setPos(pos);
}

void UsdPrimNodeItemBase::update_color()
{
    const auto& prim = get_model().get_prim_for_node(get_id());
    if (!prim)
        return;

    const auto& api = UsdUINodeGraphNodeAPI(prim);
    if (!api)
        return;

    GfVec3f color;

    if (api.GetDisplayColorAttr() && api.GetDisplayColorAttr().Get(&color))
        m_display_color.setRgbF(color[0], color[1], color[2]);
    else
        m_display_color.setRgb(64, 64, 64);

    m_border_color.setRgb(m_display_color.darker(200).rgb());

    update();
}

void UsdPrimNodeItemBase::update_color(const NodeId& node_id)
{
    const auto prim = get_model().get_prim_for_node(node_id);
    assert(prim);

    update_color();
}

void UsdPrimNodeItemBase::update_port(const PortId& port_id)
{
    // handle basic UsdUI properties first
    const auto prop_name = TfToken(get_model().get_property_name(port_id));
    if (prop_name == UsdUITokens->uiNodegraphNodePos)
    {
        update_pos();
        return;
    }
    else if (prop_name == UsdUITokens->uiNodegraphNodeExpansionState)
    {
        update_expansion_state();
        return;
    }
    else if (prop_name == UsdUITokens->uiNodegraphNodeDisplayColor)
    {
        update_color();
        return;
    }

    // Try to find a prop item with specified id and remove it if it doesn't exists in the model
    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto item = static_cast<PropertyLayoutItem*>(get_prop_layout()->itemAt(i));
        if (item->get_id() == port_id)
        {
            if (!get_model().has_port(port_id))
            {
                get_prop_layout()->removeItem(item);
                delete item;
                invalidate_layout();
            }
            return;
        }
    }

    // Layout hasn't property with the specified id, try to create one
    int position = -1;
    if (auto port = make_port(port_id, get_model().get_prim_for_node(get_id()), position))
    {
        port->setParentItem(m_aligner->get_prop_widget());
        get_prop_layout()->insertItem(position, port);
        invalidate_layout();
    }
}

UsdPrimNodeItemBase::UsdPrimNodeItemBase(UsdGraphModel& model, const NodeId& node_id, const std::string& display_name, Orientation orient,
                                         bool can_rename, bool show_full_path)
    : NodeItem(model, node_id)
{
    setZValue(3);
    // check if item is movable
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsMovable, true);
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsSelectable, true);
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemSendsScenePositionChanges, true);
    setAcceptHoverEvents(true);

    if (orient == Orientation::Horizontal)
        m_aligner = std::make_unique<HorizontalItemGeometry>(this);
    else
        m_aligner = std::make_unique<VerticalItemGeometry>(this);

    m_aligner->init_name(display_name, can_rename);
    m_aligner->init_path(show_full_path);
    m_aligner->init_layout();

    m_pre_connection = std::make_unique<PreConnectionSnapper>();
    m_disconnect_fsm = std::make_unique<DisconnectFSM>(this);
}

UsdPrimNodeItemBase::~UsdPrimNodeItemBase() {}

void UsdPrimNodeItemBase::update_node()
{
    const auto prim = get_model().get_prim_for_node(get_id());
    assert(prim);

    update_icon(prim);
    update_expansion_state();
    update_ports(prim);
    update_pos();
    update_color();
}

void UsdPrimNodeItemBase::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    painter->save();
    const auto rect = m_aligner->get_body_rect();
    painter->setBrush(m_display_color);
    if (isSelected())
    {
        QPen pen(m_selected_border_color, s_selection_pen_width);
        painter->setPen(pen);
    }
    else
    {
        painter->setPen(QPen(m_border_color, s_selection_pen_width));
    }
    painter->drawRoundedRect(rect, 2, 2);
    painter->restore();

    if (m_expansion_state == UsdUITokens->closed)
        draw_header_ports(painter, option, widget);
}

QVariant UsdPrimNodeItemBase::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == GraphicsItemChange::ItemScenePositionHasChanged)
    {
        if (m_dragging)
            m_moved = true;
    }
    return NodeItem::itemChange(change, value);
}

void UsdPrimNodeItemBase::add_connection(ConnectionItem* connection)
{
    if (!connection)
        return;

    m_prop_connections.insert(connection);

    auto prop_layout = get_prop_layout();
    const auto connection_id = connection->get_id();

    PropertyLayoutItem* item = nullptr;

    for (int i = 0; i < prop_layout->count(); ++i)
    {
        auto prop_item = prop_layout->itemAt(i);
        item = dynamic_cast<PropertyLayoutItem*>(prop_item);

        if (item && (item->get_id() == connection_id.start_port || item->get_id() == connection_id.end_port))
        {
            item->add_connection(connection);
            break;
        }

        if (auto group = dynamic_cast<PropertyGroupItem*>(prop_item))
        {
            for (int i = 0; i < group->get_prop_count(); ++i)
            {
                item = group->get_prop_item(i);
                if (item && (item->get_id() == connection_id.start_port || item->get_id() == connection_id.end_port))
                {
                    item->add_connection(connection);
                    if (!group->get_open_state())
                    {
                        if (auto base_connection = dynamic_cast<BasicConnectionItem*>(connection))
                        {
                            const auto port_y = s_port_height - s_port_vert_offset / 2.0f;
                            QPointF pos(0, port_y);
                            base_connection->set_end_pos(group->mapToScene(pos));
                        }
                    }

                    break;
                }

                item = nullptr;
            }
            if (item != nullptr)
                break;
        }
    }

    if (m_expansion_state == UsdUITokens->closed)
    {
        move_connection_to_header(connection);
    }
    else if (item && m_expansion_state == UsdUITokens->minimized)
    {
        auto group_name = item->get_group();
        if (group_name.empty())
        {
            item->setVisible(true);
            invalidate_layout();
        }
        else if (auto group_item = m_prop_groups[group_name])
        {
            group_item->show_minimized();
        }
    }
}

void UsdPrimNodeItemBase::remove_connection(ConnectionItem* connection)
{
    if (!connection)
        return;

    m_prop_connections.remove(connection);
    auto prop_layout = get_prop_layout();
    const auto connection_id = connection->get_id();

    PropertyLayoutItem* item = nullptr;
    PropertyGroupItem* group = nullptr;

    for (int i = 0; i < prop_layout->count(); ++i)
    {
        auto layout_item = prop_layout->itemAt(i);
        item = dynamic_cast<PropertyLayoutItem*>(layout_item);

        if (item && (item->get_id() == connection_id.start_port || item->get_id() == connection_id.end_port))
        {
            break;
        }
        else if (group = dynamic_cast<PropertyGroupItem*>(layout_item))
        {
            for (int i = 0; i < group->get_prop_count(); ++i)
            {
                item = group->get_prop_item(i);

                if (item && (item->get_id() == connection_id.start_port || item->get_id() == connection_id.end_port))
                {
                    break;
                }

                item = nullptr;
            }

            if (item)
                break;
        }
    }

    if (item)
    {
        item->remove_connection(connection);

        if (m_expansion_state == UsdUITokens->minimized && !item->has_connections())
        {
            if (group)
                group->show_minimized();
            else
                item->setVisible(false);
        }
    }

    invalidate_layout();
}

bool UsdPrimNodeItemBase::is_in_header_port_area(const QPointF& pos, Port::Type header_type /*= Port::Unknown*/) const
{
    auto in_area = [&pos](const QPointF& center_pos) -> bool {
        const int offset = s_port_radius + s_snapping_dist;
        bool in_x_area = pos.x() <= center_pos.x() + offset && pos.x() >= center_pos.x() - offset;
        bool in_y_area = pos.y() <= center_pos.y() + offset && pos.y() >= center_pos.y() - offset;
        return in_x_area && in_y_area;
    };

    if (header_type == Port::Type::Input)
    {
        return in_area(mapToScene(get_header_in_port_center()));
    }

    if (header_type == Port::Type::Output)
    {
        return in_area(mapToScene(get_header_out_port_center()));
    }

    bool is_input = in_area(mapToScene(get_header_in_port_center()));
    bool is_output = in_area(mapToScene(get_header_out_port_center()));
    return is_input || is_output;
}

QPointF UsdPrimNodeItemBase::get_port_connection_pos(const Port& port) const
{
    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto item_at = get_prop_layout()->itemAt(i);
        if (auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(item_at))
        {
            if (item->get_id() == port.id)
            {
                if (port.type == Port::Type::Input)
                    return item->get_in_connection_pos();
                return item->get_out_connection_pos();
            }
        }

        if (auto group = dynamic_cast<PropertyGroupItem*>(item_at))
        {
            for (int i = 0; i < group->get_prop_count(); ++i)
            {
                if (auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(group->get_prop_item(i)))
                {
                    if (item->get_id() == port.id)
                        return item->get_in_connection_pos();
                }
            }
        }
    }
    return QPointF();
}

QRectF UsdPrimNodeItemBase::boundingRect() const
{
    return m_aligner->get_bounding_rect();
}

void UsdPrimNodeItemBase::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    m_disconnect_fsm->get_data().set_start_x(event->scenePos().x());
    m_disconnect_fsm->start();
    if (!m_disconnect_cmd)
    {
        m_disconnect_cmd = CommandRegistry::create_command<DisconnectAfterShakeCommand>("node_editor_shake_disconnect");
        m_disconnect_cmd->start_block();
    }

    if (event->buttons() == Qt::MouseButton::MiddleButton)
    {
        if (auto prim = get_model().get_prim_for_node(get_id()))
        {
            auto drag = new QDrag(this);
            auto mime_data = new QMimeData;
            mime_data->setData("application/x-sdfpaths", prim.GetPath().GetString().c_str());
            drag->setMimeData(mime_data);
            drag->exec();
            event->accept();
        }
        return;
    }

    m_moved = false;
    m_dragging = true;
    QVector<NodeId> moving_items;
    for (auto item : get_scene()->selectedItems())
    {
        auto node = qgraphicsitem_cast<NodeItem*>(item);
        if (!node || (node->flags() & QGraphicsItem::ItemIsMovable) == 0)
            continue;
        moving_items.push_back(node->get_id());
    }
    moving_items.append(get_id());
    get_scene()->begin_move(moving_items);
    setZValue(4);
    for (auto connection : m_prop_connections)
        connection->setZValue(4);

    NodeItem::mousePressEvent(event);
}

void UsdPrimNodeItemBase::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    draw_pre_connection(event->scenePos());

    m_disconnect_fsm->get_data().set_current_x(event->scenePos().x());
    m_disconnect_fsm->update();

    NodeItem::mouseMoveEvent(event);
}

void UsdPrimNodeItemBase::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    m_dragging = false;
    setZValue(3);
    for (auto connection : m_prop_connections)
        connection->setZValue(2);

    m_pre_connection->clear_pre_connection_line();

    auto end_move_lambda = [&]() {
        if (!m_moved)
        {
            auto group_item = get_scene()->itemAt(event->scenePos(), QTransform());

            if (group_item)
            {
                if (auto group = qgraphicsitem_cast<PropertyGroupItem*>(group_item->parentWidget()))
                {
                    group->on_mouse_release(event);
                }
            }
            m_aligner->on_mouse_release(event);
        }
        else
        {
            get_scene()->end_move();
        }
    };

    m_disconnect_fsm->stop();
    InsertionData data;
    if (can_insert_into_connection(event->scenePos(), data))
    {
        UndoCommandBlock block("insert_node_into_connection");

        if (m_disconnected)
        {
            m_disconnect_cmd->end_block();
            CommandInterface::finalize(m_disconnect_cmd);
            m_disconnected = false;
        }

        insert_node_into_connection(data);
        end_move_lambda();
    }
    else
    {
        if (m_disconnected)
        {
            UndoCommandBlock block("disconnect_node_after_shake");
            end_move_lambda();
            m_disconnect_cmd->end_block();
            CommandInterface::finalize(m_disconnect_cmd);
            m_disconnected = false;
        }
        else
        {
            end_move_lambda();
        }
    }

    m_disconnect_cmd = nullptr;

    m_moved = false;
    NodeItem::mouseReleaseEvent(event);
}

void UsdPrimNodeItemBase::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    assert(get_scene());
    auto grabber = get_scene()->get_grabber_item();
    auto live_connection = dynamic_cast<BasicLiveConnectionItem*>(grabber);
    if (!live_connection)
    {
        NodeItem::hoverEnterEvent(event);
        return;
    }
    const auto& source_port = live_connection->get_source_port();

    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(get_prop_layout()->itemAt(i));
        if (!item)
            continue;

        if (dynamic_cast<MorePortLayoutItem*>(item))
            continue;

        Port dst_port;
        dst_port.id = item->get_id();
        dst_port.type = item->get_port_type();

        item->setEnabled(get_model().can_connect(source_port, dst_port));
    }
}

void UsdPrimNodeItemBase::update_icon(const UsdPrim& prim)
{
    const auto icon_path = get_icon_path(prim);
    if (icon_path == m_icon_path && m_aligner->get_icon_item())
        return;

    m_icon_path = icon_path;
    m_aligner->init_icon(icon_path);
}

QGraphicsLinearLayout* UsdPrimNodeItemBase::get_prop_layout() const
{
    return m_aligner->get_prop_layout();
}

PropertyLayoutItem* UsdPrimNodeItemBase::get_layout_item_for_port(const PortId& port) const
{
    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto prop = static_cast<PropertyLayoutItem*>(get_prop_layout()->itemAt(i));
        if (prop && prop->get_id() == port)
            return prop;
    }
    return nullptr;
}

void UsdPrimNodeItemBase::invalidate_layout()
{
    prepareGeometryChange();
    m_aligner->invalidate();
    move_connections();
}

UsdGraphModel& UsdPrimNodeItemBase::get_model()
{
    return static_cast<UsdGraphModel&>(NodeItem::get_model());
}

const UsdGraphModel& UsdPrimNodeItemBase::get_model() const
{
    return static_cast<const UsdGraphModel&>(NodeItem::get_model());
}

void UsdPrimNodeItemBase::reset_hover()
{
    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(get_prop_layout()->itemAt(i));
        if (!item)
            continue;
        item->setEnabled(true);
    }
}

bool UsdPrimNodeItemBase::try_snap(const BasicLiveConnectionItem& live_connection, QPointF& snap_pos) const
{
    bool snapped = false;
    auto prop_layout = get_prop_layout();

    if (prop_layout == nullptr)
        return false;

    for (int i = 0; i < prop_layout->count(); ++i)
    {
        auto item = prop_layout->itemAt(i);
        if (auto prop = dynamic_cast<PropertyLayoutItem*>(item))
        {
            snapped |= prop->try_snap(live_connection, snap_pos);
        }
        else if (auto group = dynamic_cast<PropertyGroupItem*>(item))
        {
            snapped |= group->try_snap(live_connection, snap_pos);
        }
    }

    return snapped;
}

void UsdPrimNodeItemBase::set_all_groups(bool is_expanded)
{
    if (m_prop_groups.empty())
        return;

    for (auto& group : m_prop_groups)
    {
        if (group.second != nullptr)
            group.second->set_expanded(is_expanded);
    }
}

void UsdPrimNodeItemBase::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    assert(get_scene());
    auto grabber = get_scene()->get_grabber_item();
    auto live_connection = dynamic_cast<BasicLiveConnectionItem*>(grabber);
    if (live_connection)
        reset_hover();
    else
        NodeItem::hoverLeaveEvent(event);
}

void UsdPrimNodeItemBase::move_connections()
{
    auto prop_layout = get_prop_layout();
    if (prop_layout == nullptr)
        return;

    for (int i = 0; i < prop_layout->count(); ++i)
    {
        auto item_at = prop_layout->itemAt(i);
        if (auto item = dynamic_cast<PropertyLayoutItem*>(item_at))
            item->move_connections();

        if (auto group = dynamic_cast<PropertyGroupItem*>(item_at))
        {
            for (int i = 0; i < group->get_prop_count(); ++i)
            {
                if (auto item = group->get_prop_item(i))
                {
                    item->move_connections();

                    if (item->has_connections() && !group->get_open_state())
                        group->move_connections_to_header(item);
                }
            }
        }
    }
    if (m_expansion_state == UsdUITokens->closed)
    {
        for (auto connection : get_prop_connections())
            move_connection_to_header(connection);
    }
}

QPointF UsdPrimNodeItemBase::get_header_in_port_center() const
{
    return m_aligner->get_header_in_port_center();
}

QPointF UsdPrimNodeItemBase::get_header_out_port_center() const
{
    return m_aligner->get_header_out_port_center();
}

std::unordered_map<std::string, PropertyGroupItem*>& UsdPrimNodeItemBase::get_prop_groups()
{
    return m_prop_groups;
}

QPointF UsdPrimNodeItemBase::get_node_pos() const
{
    return to_scene_position(get_model().get_node_position(get_id()), boundingRect().width());
}

void UsdPrimNodeItemBase::draw_header_ports(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget /*= nullptr*/)
{
    painter->save();
    painter->setPen(QColor(57, 57, 57));
    painter->setBrush(QColor(179, 179, 179));
    if (m_expansion_state == UsdUITokens->closed)
        painter->drawEllipse(get_header_in_port_center(), s_port_radius, s_port_radius);

    painter->drawEllipse(get_header_out_port_center(), s_port_radius, s_port_radius);
    painter->restore();
}

QGraphicsTextItem* UsdPrimNodeItemBase::get_display_name_item() const
{
    return m_aligner->get_name_item();
}

QGraphicsItem* UsdPrimNodeItemBase::get_icon_item() const
{
    return m_aligner->get_icon_item();
}

QRectF UsdPrimNodeItemBase::get_body_rect() const
{
    return m_aligner->get_body_rect();
}

bool UsdPrimNodeItemBase::can_insert_into_connection(const QPointF& pos, InsertionData& data)
{
    if (!find_hovered_connection_ports(data.connection_start, data.connection_end, pos))
    {
        return false;
    }

    if (!find_available_ports(data.node_input, data.node_output, data.connection_start, data.connection_end))
    {
        return false;
    }

    if (need_cut_from_connector(data) && !can_cut_from_connection(data))
    {
        return false;
    }

    return true;
}

void UsdPrimNodeItemBase::insert_node_into_connection(const InsertionData& data)
{
    commands::UsdEditsUndoBlock undo_block;

    if (data.can_cut && data.all_vectors_fill())
    {
        cut_node_from_connection(data);
    }

    if (data.all_port_data_is_valid())
    {
        reconnect_ports_to_insert(data.node_input, data.node_output, data.connection_start, data.connection_end);
    }
}

void UsdPrimNodeItemBase::draw_pre_connection(const QPointF& cursor_pos)
{
    if (auto connection = dynamic_cast<BasicConnectionItem*>(get_scene()->get_connection_item(cursor_pos)))
    {
        if (has_connection(connection))
        {
            m_pre_connection->clear_pre_connection_line();
            return;
        }

        Port connection_start;
        Port connection_end;
        if (!find_ports_for_connection(connection_start, connection_end, connection))
        {
            return;
        }

        Port node_available_input;
        Port node_available_output;
        if (!find_available_ports(node_available_input, node_available_output, connection_start, connection_end))
        {
            return;
        }

        auto input_pos = get_port_connection_pos(node_available_input);
        auto output_pos = get_port_connection_pos(node_available_output);
        if (input_pos.isNull() || output_pos.isNull() || input_pos == output_pos)
        {
            m_pre_connection->clear_pre_connection_line();
            return;
        }
        m_pre_connection->update_cover_connection(connection, input_pos, output_pos);
    }
    else
    {
        m_pre_connection->clear_pre_connection_line();
    }
}

bool UsdPrimNodeItemBase::find_hovered_connection_ports(Port& start_port, Port& end_port, const QPointF& pos)
{
    if (auto connection = dynamic_cast<BasicConnectionItem*>(get_scene()->get_connection_item(pos)))
    {
        if (!has_connection(connection))
        {
            return find_ports_for_connection(start_port, end_port, connection);
        }
    }
    return false;
}

bool UsdPrimNodeItemBase::find_ports_for_connection(Port& start_port, Port& end_port, BasicConnectionItem* connection)
{
    auto get_port_by_id_lambda = [&](const PortId& port_id) -> Port {
        Port port;
        port.id = port_id;
        if (auto parent_node_for_port =
                dynamic_cast<UsdPrimNodeItemBase*>(get_scene()->get_item_for_node(get_model().get_node_id_from_port(port_id))))
        {
            auto prop_layout = parent_node_for_port->get_prop_layout();
            for (int layout_idx = 0; layout_idx < prop_layout->count(); layout_idx++)
            {
                auto prop_item = prop_layout->itemAt(layout_idx);

                if (auto port_item = dynamic_cast<PropertyWithPortsLayoutItem*>(prop_item))
                {
                    if (port_item->get_id() != port_id)
                    {
                        continue;
                    }

                    port.type = port_item->get_port_type();
                    return port;
                }
                else if (auto group = dynamic_cast<PropertyGroupItem*>(prop_item))
                {
                    for (int group_idx = 0; group_idx < group->get_prop_count(); group_idx++)
                    {
                        auto port_item = dynamic_cast<PropertyWithPortsLayoutItem*>(group->get_prop_item(group_idx));
                        if (!port_item || port_item->get_id() != port_id)
                        {
                            continue;
                        }

                        port.type = port_item->get_port_type();
                        return port;
                    }
                }
            }
        }
        return Port();
    };

    auto start = get_port_by_id_lambda(connection->get_id().start_port);
    auto end = get_port_by_id_lambda(connection->get_id().end_port);

    if (start.id.empty() || end.id.empty() || start.type == end.type)
    {
        return false;
    }

    start_port = start;
    end_port = end;
    return true;
}

bool UsdPrimNodeItemBase::has_connection(ConnectionItem* item) const
{
    auto all_node_connections = get_scene()->get_connection_items_for_node(get_id());
    if (!all_node_connections.empty())
    {
        for (auto node_connection : all_node_connections)
        {
            if (item == node_connection)
            {
                return true;
            }
        }
    }
    return false;
}

bool UsdPrimNodeItemBase::need_cut_from_connector(InsertionData& data)
{
    if (get_scene()->get_connection_items_for_node(get_id()).empty())
    {
        return false;
    }

    if (!data.all_port_data_is_valid())
    {
        return false;
    }

    auto all_connections = get_model().get_connections_for_node(get_id());
    if (all_connections.empty())
    {
        return false;
    }

    data.all_connection_with_node_input = get_connections_for_port(all_connections, data.node_input.id);
    data.all_connection_with_node_output = get_connections_for_port(all_connections, data.node_output.id);
    if (data.all_connection_with_node_input.empty() && data.all_connection_with_node_output.empty())
    {
        return false;
    }
    return true;
}

bool UsdPrimNodeItemBase::can_disconnect_after_shake(InsertionData& data)
{
    for (const auto node_item : get_scene()->get_selected_node_items())
    {
        auto node = dynamic_cast<UsdPrimNodeItemBase*>(node_item);
        if (!node)
        {
            return false;
        }

        auto node_ports = node->get_ports();
        if (node_ports.empty())
        {
            return false;
        }

        auto get_port_with_current_type_lambda = [&node_ports](Port::Type type) -> std::vector<Port> {
            std::vector<Port> result;

            Port port;
            port.type = type;
            for (auto node_port : node_ports)
            {
                if (node_port.type != type)
                {
                    continue;
                }

                port.id = node_port.id;
                result.push_back(port);
            }
            return result;
        };
        auto all_node_input = get_port_with_current_type_lambda(Port::Type::Input);
        auto all_node_output = get_port_with_current_type_lambda(Port::Type::Output);

        std::vector<ConnectionId> all_connection_with_node_input;
        std::vector<ConnectionId> all_connection_with_node_output;

        auto all_connections = node->get_model().get_connections_for_node(node->get_id());
        auto get_connections_for_port_vector_lambda = [&all_connections](const std::vector<Port>& port_vector) -> std::vector<ConnectionId> {
            std::vector<ConnectionId> result;
            for (const auto& port : port_vector)
            {
                auto new_connections = get_connections_for_port(all_connections, port.id);
                result.insert(result.end(), new_connections.begin(), new_connections.end());
            }
            return result;
        };
        all_connection_with_node_input = get_connections_for_port_vector_lambda(all_node_input);
        all_connection_with_node_output = get_connections_for_port_vector_lambda(all_node_output);

        if (all_connection_with_node_input.empty() && all_connection_with_node_output.empty())
        {
            return false;
        }

        std::vector<Port> all_input_from_connection;
        std::vector<Port> all_output_from_connection;

        auto get_opposite_connection_ports_for_port_vector_lambda = [](const std::vector<ConnectionId>& connections,
                                                                       const std::vector<Port>& port_vector) -> std::vector<Port> {
            std::vector<Port> result;
            for (const auto& port : port_vector)
            {
                for (const auto& opposite_port : get_opposite_connection_port_vector(connections, port))
                {
                    if (std::find(result.begin(), result.end(), opposite_port) == result.end())
                    {
                        result.push_back(opposite_port);
                    }
                }
            }
            return result;
        };

        all_output_from_connection = get_opposite_connection_ports_for_port_vector_lambda(all_connection_with_node_input, all_node_input);
        all_input_from_connection = get_opposite_connection_ports_for_port_vector_lambda(all_connection_with_node_output, all_node_output);
        if (all_output_from_connection.empty() && all_input_from_connection.empty())
        {
            return false;
        }

        auto erase_data_of_selected_node_with_opposite_port_lambda = [this](std::vector<ConnectionId>& all_connection,
                                                                            std::vector<Port>& all_opposite_port) {
            std::vector<ConnectionId> need_erase_connections;
            std::vector<Port> need_erase_ports;
            for (const auto& connector : all_connection)
            {
                Port opposite_port;
                for (const auto& port : all_opposite_port)
                {
                    if (port.id == connector.start_port || port.id == connector.end_port)
                    {
                        opposite_port = port;
                        break;
                    }
                }

                auto node_on_opposite_port = get_scene()->get_item_for_node(get_model().get_node_id_from_port(opposite_port.id));
                if (node_on_opposite_port && node_on_opposite_port->isSelected())
                {
                    need_erase_connections.push_back(connector);
                    need_erase_ports.push_back(opposite_port);
                }
            }

            for (int i = 0; i < need_erase_connections.size(); i++)
            {
                all_connection.erase(std::find(all_connection.begin(), all_connection.end(), need_erase_connections[i]));
                all_opposite_port.erase(std::find(all_opposite_port.begin(), all_opposite_port.end(), need_erase_ports[i]));
            }
        };

        erase_data_of_selected_node_with_opposite_port_lambda(all_connection_with_node_input, all_output_from_connection);
        data.all_connection_with_node_input.insert(data.all_connection_with_node_input.end(), all_connection_with_node_input.begin(),
                                                   all_connection_with_node_input.end());
        data.all_output_from_connection.insert(data.all_output_from_connection.end(), all_output_from_connection.begin(),
                                               all_output_from_connection.end());

        erase_data_of_selected_node_with_opposite_port_lambda(all_connection_with_node_output, all_input_from_connection);
        data.all_connection_with_node_output.insert(data.all_connection_with_node_output.end(), all_connection_with_node_output.begin(),
                                                    all_connection_with_node_output.end());
        data.all_input_from_connection.insert(data.all_input_from_connection.end(), all_input_from_connection.begin(),
                                              all_input_from_connection.end());
    }

    if (data.cut_data_is_empty())
    {
        return false;
    }

    data.can_cut = true;
    return true;
}

bool UsdPrimNodeItemBase::can_cut_from_connection(InsertionData& data)
{
    if (!data.all_port_data_is_valid())
    {
        return false;
    }

    data.all_output_from_connection = get_opposite_connection_port_vector(data.all_connection_with_node_input, data.node_input);
    data.all_input_from_connection = get_opposite_connection_port_vector(data.all_connection_with_node_output, data.node_output);
    if (data.all_output_from_connection.empty() && data.all_input_from_connection.empty())
    {
        return false;
    }

    data.can_cut = true;
    return true;
}

bool UsdPrimNodeItemBase::find_available_ports(Port& input, Port& output, const Port& connection_start, const Port& connection_end)
{
    std::vector<PropertyWithPortsLayoutItem*> port_items = get_port_items();
    if (port_items.empty())
    {
        return false;
    }

    auto get_available_port_in_node_lambda = [&](Port::Type type) -> Port {
        Port port;
        port.type = type;

        for (auto port_item : port_items)
        {
            if (port_item->get_port_type() != type)
            {
                continue;
            }

            port.id = port_item->get_id();

            if (get_model().can_connect(connection_start, port) || get_model().can_connect(port, connection_end))
            {
                return port;
            }
        }
        return Port();
    };
    auto input_port = get_available_port_in_node_lambda(Port::Type::Input);
    auto output_port = get_available_port_in_node_lambda(Port::Type::Output);

    if (input_port.id.empty() || output_port.id.empty())
    {
        return false;
    }

    input = input_port;
    output = output_port;
    return true;
}

void UsdPrimNodeItemBase::cut_node_from_connection(const InsertionData& data)
{
    for (auto input : data.all_input_from_connection)
    {
        for (auto output : data.all_output_from_connection)
        {
            if (get_model().connect_ports(output, input))
            {
                break;
            }
        }
    }

    auto remove_old_connection_lambda = [this](const std::vector<ConnectionId>& connection_vector) {
        for (auto connection : connection_vector)
        {
            if (get_scene()->get_item_for_connection(connection))
            {
                get_model().delete_connection(connection);
            }
        }
    };

    remove_old_connection_lambda(data.all_connection_with_node_input);
    remove_old_connection_lambda(data.all_connection_with_node_output);
}

void UsdPrimNodeItemBase::reconnect_ports_to_insert(const Port& node_input, const Port& node_output, const Port& connection_start,
                                                    const Port& connection_end)
{
    if (!node_input.id.empty() && !node_output.id.empty())
    {
        if (!get_model().connect_ports(connection_start, node_input))
        {
            get_model().connect_ports(connection_end, node_input);
        }

        if (!get_model().connect_ports(node_output, connection_end))
        {
            get_model().connect_ports(node_output, connection_start);
        }

        ConnectionId connection;
        connection.start_port = connection_start.id;
        connection.end_port = connection_end.id;
        if (get_scene()->get_item_for_connection(connection))
        {
            get_model().delete_connection(connection);
        }
    }
}

QGraphicsTextItem* UsdPrimNodeItemBase::get_full_path_text_item() const
{
    return m_aligner->get_full_path_item();
}

void UsdPrimNodeItemBase::move_connection_to_header(ConnectionItem* item)
{
    auto con = static_cast<BasicConnectionItem*>(item);
    const auto is_outcoming = get_model().get_node_id_from_port(item->get_id().start_port) == get_id();
    const auto port_y = get_header_in_port_center().y();
    if (is_outcoming)
        con->set_start_pos(mapToScene(QPointF(s_node_width, port_y)));
    else
        con->set_end_pos(mapToScene(QPointF(0, port_y)));
}

void UsdPrimNodeItemBase::move_connection_to_group(ConnectionItem* item, const PropertyGroupItem* group)
{
    auto con = static_cast<BasicConnectionItem*>(item);
    if (!con || !group)
        return;

    const auto is_outcoming = get_model().get_node_id_from_port(item->get_id().start_port) == get_id();
    const auto port_y = group->y();
    if (is_outcoming)
        con->set_start_pos(mapToScene(QPointF(s_node_width, port_y)));
    else
        con->set_end_pos(mapToScene(QPointF(0, port_y)));
}

void UsdPrimNodeItemBase::update_ports(const PXR_NS::UsdPrim& prim)
{
    auto prop_layout = get_prop_layout();
    const auto prop_count = prop_layout->count();
    for (int i = 0; i < prop_count; ++i)
    {
        auto item = prop_layout->itemAt(0);
        prop_layout->removeAt(0);
        delete item;
    }

    auto ports = make_ports(prim);
    for (auto port : ports)
    {
        prop_layout->addItem(port);
    }

    for (const auto& group : m_prop_groups)
    {
        if (group.second != nullptr)
        {
            prop_layout->addItem(group.second);
            group.second->set_name(QString::fromStdString(group.first));
        }
    }

    prop_layout->activate();
    on_update_expansion_state();
}

void UsdPrimNodeItemBase::on_update_expansion_state()
{
    prepareGeometryChange();
    auto prop_layout = get_prop_layout();
    const auto prop_count = prop_layout->count();

    if (m_expansion_state != UsdUITokens->minimized)
    {
        const auto show = m_expansion_state == UsdUITokens->open;
        for (int i = 0; i < prop_count; ++i)
        {
            auto layout_item = prop_layout->itemAt(i)->graphicsItem();
            assert(layout_item);

            if (auto group = qgraphicsitem_cast<PropertyGroupItem*>(layout_item))
            {
                group->set_visible(show);
            }
            else
            {
                layout_item->setVisible(show);
            }
        }

        const auto last_ind = prop_count - 1;
        if (last_ind >= 0)
        {
            if (auto item = dynamic_cast<MorePortLayoutItem*>(prop_layout->itemAt(last_ind)))
            {
                prop_layout->removeAt(last_ind);
                delete item;
            }
        }
    }
    else
    {
        for (int i = 0; i < prop_count; ++i)
        {
            auto layout_item = prop_layout->itemAt(i)->graphicsItem();
            assert(layout_item);

            if (auto group = qgraphicsitem_cast<PropertyGroupItem*>(layout_item))
            {
                group->show_minimized();
            }
            else if (auto item = qgraphicsitem_cast<PropertyLayoutItem*>(layout_item))
            {
                item->setVisible(item->has_connections());
            }
        }

        prop_layout->addItem(new MorePortLayoutItem(get_model(), this, get_id() + "#.more"));
    }

    invalidate_layout();
}

QVector<PropertyLayoutItem*> UsdPrimNodeItemBase::get_more_ports(Port::Type type) const
{
    if (m_expansion_state != UsdUITokens->minimized)
        return {};

    QVector<PropertyLayoutItem*> result;
    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto layout_item = get_prop_layout()->itemAt(i);
        auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(layout_item);
        if (item && ((item->get_port_type() & type) != 0) && !item->has_connections())
            result.push_back(item);

        if (auto group = dynamic_cast<PropertyGroupItem*>(layout_item))
        {
            for (int i = 0; i < group->get_prop_count(); i++)
            {
                auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(group->get_prop_item(i));
                if (item && ((item->get_port_type() & type) != 0) && !item->has_connections())
                    result.push_back(item);
            }
        }
    }
    return std::move(result);
}

QSet<ConnectionItem*>& UsdPrimNodeItemBase::get_prop_connections()
{
    return m_prop_connections;
}

std::vector<PropertyWithPortsLayoutItem*> UsdPrimNodeItemBase::get_port_items() const
{
    std::vector<PropertyWithPortsLayoutItem*> port_items;
    for (int i = 0; i < get_prop_layout()->count(); ++i)
    {
        auto prop_item = get_prop_layout()->itemAt(i);
        if (auto port_item = dynamic_cast<PropertyWithPortsLayoutItem*>(prop_item))
        {
            port_items.push_back(port_item);
        }
        else if (auto group = dynamic_cast<PropertyGroupItem*>(prop_item))
        {
            for (int group_idx = 0; group_idx < group->get_prop_count(); group_idx++)
            {
                if (auto port_item = dynamic_cast<PropertyWithPortsLayoutItem*>(group->get_prop_item(group_idx)))
                {
                    port_items.push_back(port_item);
                }
            }
        }
    }

    return port_items;
}

std::vector<Port> UsdPrimNodeItemBase::get_ports() const
{
    std::vector<Port> result;
    auto items = get_port_items();
    for (const auto& item : items)
    {
        Port port;
        port.id = item->get_id();
        port.type = item->get_port_type();
        result.push_back(port);
    }
    return result;
}

UsdPrimNodeItem::UsdPrimNodeItem(UsdGraphModel& model, const NodeId& node_id, const std::string& display_name, bool is_external)
    : UsdPrimNodeItemBase(model, node_id, display_name, Orientation::Horizontal, true, is_external)
{
}

void UsdPrimNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget /*= nullptr*/)
{
    UsdPrimNodeItemBase::paint(painter, option, widget);
    if (get_expansion_state() == UsdUITokens->closed) // already drawn
        return;
    draw_header_ports(painter, option, widget);
}

void UsdPrimNodeItem::remove_connection(ConnectionItem* connection)
{
    m_prim_connections.remove(connection);
    UsdPrimNodeItemBase::remove_connection(connection);
}

QPointF UsdPrimNodeItem::get_port_connection_pos(const Port& port) const
{
    if (port.id == get_id())
    {
        const auto port_y = get_header_in_port_center().y();
        return mapToScene(QPointF(s_node_width, port_y));
    }
    return UsdPrimNodeItemBase::get_port_connection_pos(port);
}

void UsdPrimNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    for (auto connection : m_prim_connections)
        connection->setZValue(2);
    UsdPrimNodeItemBase::mouseReleaseEvent(event);
}

void UsdPrimNodeItem::move_connections()
{
    for (auto connection : m_prim_connections)
        move_connection_to_header(connection);
    UsdPrimNodeItemBase::move_connections();
}

void UsdPrimNodeItemBase::set_expansion_state(const PXR_NS::TfToken& new_state)
{
    if (m_expansion_state == new_state)
        return;

    get_model().block_usd_notifications(true);
    get_model().set_expansion_state(get_id(), new_state);
    get_model().block_usd_notifications(false);
    update_expansion_state();
}

const TfToken& UsdPrimNodeItemBase::get_expansion_state() const
{
    return m_expansion_state;
}

void UsdPrimNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (get_port_shape(mapToScene(get_header_in_port_center())).contains(event->scenePos()) && get_expansion_state() == UsdUITokens->closed)
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Output;
        Q_EMIT get_scene() -> port_pressed(port);
        return;
    }
    else if (get_port_shape(mapToScene(get_header_out_port_center())).contains(event->scenePos()))
    {
        Port port;
        port.id = get_id();
        port.type = Port::Type::Output;
        Q_EMIT get_scene() -> port_pressed(port);
        return;
    }

    for (auto connection : m_prim_connections)
        connection->setZValue(3);
    UsdPrimNodeItemBase::mousePressEvent(event);
}

void UsdPrimNodeItem::add_connection(ConnectionItem* connection)
{
    if (!connection)
        return;
    if (connection->get_id().start_port == get_id())
    {
        m_prim_connections.insert(connection);
        move_connection_to_header(connection);
    }
    else
    {
        UsdPrimNodeItemBase::add_connection(connection);
    }
}

QVector<PropertyLayoutItem*> UsdPrimNodeItem::make_ports(const UsdPrim& prim)
{
    static const std::unordered_set<TfToken, TfToken::HashFunctor> s_ignored_attrs = [] {
        std::unordered_set<TfToken, TfToken::HashFunctor> ignored;
        for (auto schema : { UsdUIBackdrop::GetSchemaAttributeNames(false), UsdUINodeGraphNodeAPI::GetSchemaAttributeNames(false),
                             UsdUISceneGraphPrimAPI::GetSchemaAttributeNames(false) })
        {
            ignored.insert(schema.begin(), schema.end());
        }
        return std::move(ignored);
    }();

    QVector<PropertyLayoutItem*> result;
    if (get_expansion_state() == UsdUITokens->closed)
        return result;

    UsdPrimFallbackProxy proxy(prim);

    for (const auto prop : proxy.get_all_property_proxies())
    {
        const auto prop_path = prim.GetPath().AppendProperty(prop->get_name_token());
        if (s_ignored_attrs.find(prop_path.GetNameToken()) != s_ignored_attrs.end())
            continue;

        std::vector<ConnectionItem*> connections;
        for (auto connection : get_prop_connections())
        {
            if (SdfPath(connection->get_id().start_port) == prop_path || SdfPath(connection->get_id().end_port) == prop_path)
                connections.push_back(connection);
        }

        if (get_expansion_state() == UsdUITokens->minimized && connections.empty())
            continue;

        auto item = new NamedPropertyLayoutItem(get_model(), this, prop_path.GetString(), prop->get_name_token(), Port::Type::Both);

        for (auto connection : connections)
            item->add_connection(connection);
        result.push_back(item);
    }
    return std::move(result);
}

QString UsdPrimNodeItem::get_icon_path(const UsdPrim& prim) const
{
    static const QString fallback = ":icons/node_editor/withouttype";
    const auto prim_type = prim.GetTypeName().GetString();
    auto& icon_registry = NodeIconRegistry::instance();
    if (icon_registry.is_svg_exists(TfToken("USD"), prim_type))
        return QString::fromStdString(icon_registry.get_svg(TfToken("USD"), prim_type));
    return fallback;
}

PropertyLayoutItem* UsdPrimNodeItem::make_port(const PortId& port_id, const PXR_NS::UsdPrim& prim, int& position)
{
    UsdPrimFallbackProxy proxy(prim);
    auto prop = proxy.get_property_proxy(SdfPath(port_id).GetNameToken());
    if (!prop)
        return nullptr;
    return new NamedPropertyLayoutItem(get_model(), this, port_id, prop->get_name_token(), Port::Type::Both);
}

PropertyGroupItem::PropertyGroupItem(UsdPrimNodeItemBase* node, const QString& name)
    : QGraphicsWidget(node)
    , m_node(node)
{
    m_head = new GroupHeaderWidget(this);
    m_head->set_text(name);

    setup_expansion_state();

    m_properties_layout = new QGraphicsLinearLayout(Qt::Vertical);

    auto back_layout = new QGraphicsLinearLayout(Qt::Vertical, this);
    back_layout->setContentsMargins(0, s_port_vert_offset, 0, s_port_vert_offset);
    back_layout->setSpacing(0);
    back_layout->addItem(m_head);
    back_layout->addItem(m_properties_layout);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setLayout(back_layout);

    connect(this, &PropertyGroupItem::visibleChanged, this, [this]() { m_minimized = false; });
}

void PropertyGroupItem::set_visible(bool state)
{
    setVisible(state);

    if (state)
        set_expanded(m_content_visible);
}

void PropertyGroupItem::set_expanded(bool state)
{
    update_ui_api(state);

    m_minimized = false;
    m_content_visible = state;

    const auto prop_count = m_properties_layout->count();
    for (int i = 0; i < prop_count; ++i)
    {
        auto item = get_prop_item(i);

        if (item)
        {
            item->setVisible(m_content_visible);
        }
    }

    update_layouts();
}

void PropertyGroupItem::move_connections_to_header(PropertyLayoutItem* item)
{
    if (m_minimized || item == nullptr)
        return;

    if (auto prop_layout_item = dynamic_cast<PropertyWithPortsLayoutItem*>(item))
    {
        for (auto& connection : prop_layout_item->get_connections())
        {
            auto con = dynamic_cast<BasicConnectionItem*>(connection);
            const auto is_outcoming = m_node->get_model().get_node_id_from_port(connection->get_id().start_port) == m_node->get_id();

            const auto port_y = s_port_height - s_port_vert_offset / 2.0f;
            if (is_outcoming)
                con->set_start_pos(mapToScene(QPointF(s_node_width, port_y)));
            else
                con->set_end_pos(mapToScene(QPointF(0, port_y)));
        }
    }
}

Port PropertyGroupItem::select_port(Port::Type type) const
{
    Port result;
    auto more_ports_arr = get_more_ports(type);
    if (more_ports_arr.empty())
        return result;

    auto live_connection = dynamic_cast<BasicLiveConnectionItem*>(m_node->get_scene()->get_grabber_item());

    std::function<bool(const Port& dest)> can_connect;
    if (live_connection)
    {
        can_connect = [this, src = live_connection->get_source_port()](const Port& dest) {
            return m_node->get_model().can_connect(src, dest);
        };
    }
    else
    {
        can_connect = [](const Port& dest) {
            return true;
        };
    }

    QMenu more_ports;
    for (int i = 0; i < more_ports_arr.size(); ++i)
    {
        auto item = more_ports_arr[i];
        auto display_text = item->data(static_cast<int>(PropertyLayoutItem::Data::DisplayText));
        auto display_str = display_text.isValid() ? display_text.toString() : QString();
        if (display_str.isEmpty())
            continue;

        auto action = new QAction(display_str);
        action->setData(i);
        action->setEnabled(can_connect(Port { item->get_id(), type }));
        more_ports.addAction(action);
    }

    if (more_ports.actions().empty())
        return result;

    if (auto selected_port = more_ports.exec(QCursor::pos()))
    {
        const auto selected_port_id = selected_port->data().toInt();
        result.type = type;
        result.id = more_ports_arr[selected_port_id]->get_id();
    }
    else
    {
        return result;
    }

    return result;
}

QVector<PropertyLayoutItem*> PropertyGroupItem::get_more_ports(Port::Type type) const
{
    if (m_minimized)
        return {};

    QVector<PropertyLayoutItem*> result;
    for (int i = 0; i < m_properties_layout->count(); ++i)
    {
        auto item = dynamic_cast<PropertyWithPortsLayoutItem*>(m_properties_layout->itemAt(i));
        if (item && ((item->get_port_type() & type) != 0) && !item->has_connections())
            result.push_back(item);
    }
    return std::move(result);
}

bool PropertyGroupItem::try_snap(const BasicLiveConnectionItem& connection, QPointF& snap_point) const
{
    bool snapped = false;

    for (int i = 0; i < m_properties_layout->count(); ++i)
    {
        if (auto prop = get_prop_item(i))
        {
            snapped |= prop->try_snap(connection, snap_point);
        }
    }

    return snapped;
}

const QString& PropertyGroupItem::get_group_name() const
{
    return m_head->get_text();
}

void PropertyGroupItem::update_ui_api(bool state)
{
    assert(m_node);
    if (state == m_content_visible)
        return;

    auto token = TfToken(m_head->get_text().toStdString());
    if (token.IsEmpty())
        return;

    auto api = UsdUIExtNodeDisplayGroupUIAPI(m_node->get_model().get_prim_for_node(m_node->get_id()));
    if (!api)
        return;

    if (auto collapsed_attr = api.GetUiDisplayGroupNodeCollapsedAttr())
    {
        VtArray<TfToken> collapsed;

        if (!collapsed_attr.Get(&collapsed))
            return;

        if (!state)
        {
            collapsed.push_back(token);
        }
        else
        {
            auto token_it = std::find(collapsed.begin(), collapsed.end(), token);
            if (token_it != collapsed.end())
                collapsed.erase(token_it);
        }

        api.CreateUiDisplayGroupNodeCollapsedAttr(VtValue(collapsed));
    }
}

void PropertyGroupItem::setup_expansion_state()
{
    assert(m_node);

    auto api = UsdUIExtNodeDisplayGroupUIAPI(m_node->get_model().get_prim_for_node(m_node->get_id()));
    if (!api)
        return;

    if (auto collapsed_attr = api.GetUiDisplayGroupNodeCollapsedAttr())
    {
        VtArray<TfToken> collapsed;
        collapsed_attr.Get(&collapsed);

        if (collapsed.empty())
            return;

        auto token = TfToken(m_head->get_text().toStdString());
        if (token.IsEmpty())
            return;

        if (std::find(collapsed.begin(), collapsed.end(), token) != collapsed.end())
            m_content_visible = false;
        else
            m_content_visible = true;
    }
}

void PropertyGroupItem::update_layouts()
{
    m_head->set_state(m_content_visible || m_minimized);
    layout()->invalidate();
    m_properties_layout->invalidate();
    m_node->invalidate_layout();
}

void PropertyGroupItem::show_minimized()
{
    setVisible(false);
    bool group_has_connections = false;
    for (int i = 0; i < m_properties_layout->count(); ++i)
    {
        if (auto item = dynamic_cast<PropertyLayoutItem*>(m_properties_layout->itemAt(i)))
        {
            bool item_has_connections = item->has_connections();

            group_has_connections |= item_has_connections;

            if (item_has_connections && !m_minimized)
            {
                setVisible(true);
                m_minimized = true;
            }

            item->setVisible(item_has_connections);
        }
    }

    if (group_has_connections)
    {
        update_layouts();
    }
}

void PropertyGroupItem::on_mouse_release(QGraphicsSceneMouseEvent* event)
{
    if (m_head->sceneBoundingRect().contains(event->scenePos()) && !m_minimized)
        set_expanded(!m_content_visible);
    else
        event->ignore();
}

bool PropertyGroupItem::get_open_state() const
{
    return m_content_visible;
}

int PropertyGroupItem::get_prop_count() const
{
    return m_properties_layout->count();
}

QSizeF PropertyGroupItem::sizeHint(Qt::SizeHint which, const QSizeF& constraint) const
{
    if (m_content_visible || m_minimized)
        return layout()->effectiveSizeHint(which, constraint);

    return m_head->preferredSize();
}

PropertyLayoutItem* PropertyGroupItem::get_prop_item(int index) const
{
    return dynamic_cast<PropertyLayoutItem*>(m_properties_layout->itemAt(index));
}

void PropertyGroupItem::add_item(PropertyLayoutItem* item)
{
    item->set_group(m_head->get_text().toStdString());
    m_properties_layout->addItem(item);
}

void PropertyGroupItem::set_name(const QString& name)
{
    m_head->set_text(name);
}

bool UsdPrimNodeItemBase::InsertionData::all_port_data_is_valid() const
{
    auto port_is_valid_lambda = [](const Port& port) -> bool {
        return !(port.id.empty() || port.type == Port::Type::Unknown);
    };
    return port_is_valid_lambda(connection_start) && port_is_valid_lambda(connection_end) && port_is_valid_lambda(node_input) &&
           port_is_valid_lambda(node_output);
}

bool UsdPrimNodeItemBase::InsertionData::all_vectors_fill() const
{
    return !all_connection_with_node_input.empty() && !all_connection_with_node_output.empty() && !all_input_from_connection.empty() &&
           !all_output_from_connection.empty();
}

bool UsdPrimNodeItemBase::InsertionData::cut_data_is_empty() const
{
    return all_connection_with_node_input.empty() && all_connection_with_node_output.empty() ||
           all_output_from_connection.empty() && all_input_from_connection.empty();
}

OPENDCC_NAMESPACE_CLOSE
