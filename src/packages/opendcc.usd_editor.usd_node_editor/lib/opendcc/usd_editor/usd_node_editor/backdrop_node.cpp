// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/backdrop_node.h"
#include "opendcc/ui/node_editor/view.h"
#include "opendcc/ui/node_editor/node.h"
#include "opendcc/ui/node_editor/text_item.h"
#include "opendcc/usd_editor/usd_node_editor/graph_model.h"
#include "opendcc/base/commands_api/core/block.h"
#include "opendcc/usd_editor/usd_node_editor/node.h"
#include <pxr/usd/usdUI/backdrop.h>
#include <pxr/usd/usdUI/nodeGraphNodeAPI.h>
#include <QPalette>
#include <QApplication>
#include <QPainter>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsView>

#include "opendcc/base/commands_api/core/command_interface.h"
#include "usdUIExt/backdropUIAPI.h"

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN

using base::commands::UndoCommandBlock;
using base::commands::CommandInterface;
using base::commands::CommandArgs;

namespace
{
    const QSizeF s_default_start_size = QSizeF(80, 80);

    void draw_backdrop(QPainter* painter, const QRectF& rect, const QColor& display_color, bool is_selected)
    {
        painter->save();

        auto color = display_color;
        painter->setBrush(QColor(color.red(), color.green(), color.blue()));
        painter->setPen(Qt::NoPen);
        painter->drawRect(rect);

        auto top_rect = QRectF(0, 0, rect.width(), std::min(20.0, rect.height()));
        painter->setBrush(color.darker(110));
        painter->setPen(Qt::NoPen);
        painter->drawRect(top_rect);

        if (is_selected)
        {
            auto sel_color = QColor(0, 173, 240);
            sel_color.setAlpha(10);
            painter->setBrush(sel_color);
            painter->setPen(Qt::NoPen);
            painter->drawRect(rect);
        }

        QPainterPath path;
        path.addRect(rect);
        painter->setBrush(Qt::NoBrush);
        auto border_color = color;
        if (is_selected)
            border_color = QColor(0, 173, 240);
        painter->setPen(QPen(border_color, 1));
        painter->drawPath(path);
        painter->restore();
    }

};

class BackdropSizerItem : public QGraphicsItem
{
public:
    BackdropSizerItem(QGraphicsItem* parent)
        : QGraphicsItem(parent)
    {
        setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsMovable, true);
        setFlag(QGraphicsItem::GraphicsItemFlag::ItemSendsScenePositionChanges, true);
        setCursor(QCursor(Qt::SizeFDiagCursor));
    }

    virtual void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        parentItem()->setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsMovable, false);
        static_cast<NodeEditorScene*>(scene())->begin_resize(static_cast<BackdropNodeItem*>(parentItem())->get_id());

        QGraphicsItem::mousePressEvent(event);
        event->accept();
    }

    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        parentItem()->setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsMovable, true);
        static_cast<NodeEditorScene*>(scene())->end_resize();
        QGraphicsItem::mouseReleaseEvent(event);
        event->accept();
    }

    QRectF boundingRect() const override { return QRectF(0.5, 0.5, m_size, m_size); }

    virtual QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
    {
        if (change == GraphicsItemChange::ItemPositionChange)
        {
            float mx = s_default_start_size.width() - 20;
            float my = s_default_start_size.height() - 20;
            auto pos = value.toPointF();
            float x = pos.x() < mx ? mx : pos.x();
            float y = pos.y() < my ? my : pos.y();
            auto new_pos = QPointF(x, y);
            auto item = parentItem();
            if (item)
            {
                auto backdrop_item = static_cast<BackdropNodeItem*>(item);
                backdrop_item->on_sizer_pos_changed(new_pos);
            }
            return new_pos;
        }

        return QGraphicsItem::itemChange(change, value);
    }
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override
    {
        painter->save();
        auto rect = boundingRect();
        auto item = parentItem();
        auto backdrop_item = static_cast<BackdropNodeItem*>(item);
        auto color = QColor(70, 70, 70);
        if (item && item->isSelected())
            color = QColor(0, 173, 240);
        else
            color = color.darker(110);

        QPainterPath path;
        path.moveTo(rect.topRight());
        path.lineTo(rect.bottomRight());
        path.lineTo(rect.bottomLeft());
        painter->setBrush(color);
        painter->setPen(Qt::NoPen);
        painter->fillPath(path, painter->brush());
        painter->restore();
    }

    void set_size(float size) { m_size = size; }
    float get_size() const { return m_size; }
    void set_pos(float x, float y)
    {
        x -= m_size;
        y -= m_size;
        setPos(x, y);
    }

private:
    float m_size = 20.0;
};

BackdropLiveNodeItem::BackdropLiveNodeItem(UsdGraphModel& model, const PXR_NS::TfToken& name, const PXR_NS::TfToken& type, const SdfPath& parent_path,
                                           QGraphicsItem* parent)
    : QGraphicsRectItem(parent)
    , m_model(model)
    , m_name(name)
    , m_type(type)
    , m_parent_path(parent_path)
{
    setZValue(-2);
    const auto width = 150;
    setBrush(QColor(70, 70, 70));
    setPen(QColor(0, 173, 240));
    setRect(0, 0, s_default_start_size.width(), s_default_start_size.height());
}

void BackdropLiveNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    const auto pen_half_w = pen().widthF() / 2;
    const auto adj_rect = boundingRect().adjusted(pen_half_w, pen_half_w, -pen_half_w, -pen_half_w);
    draw_backdrop(painter, adj_rect, brush().color(), true);
}

void BackdropLiveNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    m_start_pos = event->scenePos() - QPointF(s_default_start_size.width(), s_default_start_size.height());
    event->accept();
}

void BackdropLiveNodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_start_pos.isNull())
    {
        setPos(event->scenePos() - QPointF(s_default_start_size.width(), s_default_start_size.height()));

        if (const auto& snapper = get_scene()->get_view()->get_align_snapper())
        {
            const auto snap = snapper->try_snap(*this);
            if (!snap.isNull())
                setPos(snap);
        }

        return;
    }

    const auto cur_pos = event->scenePos();
    setRect(0, 0, cur_pos.x() - m_start_pos.x(), cur_pos.y() - m_start_pos.y());
}

void BackdropLiveNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    UndoCommandBlock block("create_node_editor_backdrop");
    auto prim = m_model.create_usd_prim(m_name, m_type, m_parent_path, false);
    if (!prim)
    {
        get_scene()->remove_grabber_item();
        return;
    }

    UsdUIExtBackdropUIAPI::Apply(prim);

    auto backdrop_prim = UsdUINodeGraphNodeAPI::Apply(prim);
    if (backdrop_prim)
    {
        const auto start_to_end = event->scenePos() - m_start_pos;
        QPointF start_scene_pos;
        QPointF start_model_pos;
        QSizeF start_size;

        if (boundingRect().width() < s_default_start_size.width() || boundingRect().height() < s_default_start_size.height())
        {
            start_size = QSizeF(std::max(s_default_start_size.width(), boundingRect().width()),
                                std::max(s_default_start_size.height(), boundingRect().height()));
            start_model_pos = to_model_position(scenePos(), start_size.width());
        }
        else
        {
            start_size = QSizeF(boundingRect().width(), boundingRect().height());
            start_model_pos = to_model_position(scenePos(), start_size.width());
        }

        GfVec2f size_val(start_size.width(), start_size.height());
        backdrop_prim.CreateSizeAttr(VtValue(size_val));
        backdrop_prim.CreatePosAttr(VtValue(GfVec2f(start_model_pos.x(), start_model_pos.y())));
    }

    CommandInterface::execute("select", CommandArgs().arg(prim));

    get_scene()->remove_grabber_item();
}

QVariant BackdropLiveNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == QGraphicsItem::ItemSceneHasChanged && scene())
    {
        const auto view = static_cast<NodeEditorScene*>(scene())->get_view();
        const auto view_pos = view->mapFromGlobal(QCursor::pos());
        const auto scene_pos = view->mapToScene(view_pos);
        setPos(scene_pos - QPointF(rect().width(), rect().height()));
    }
    return QGraphicsRectItem::itemChange(change, value);
}

NodeEditorScene* BackdropLiveNodeItem::get_scene()
{
    return static_cast<NodeEditorScene*>(scene());
}

BackdropNodeItem::BackdropNodeItem(UsdGraphModel& model, const NodeId& node_id, const std::string& display_name)
    : NodeItem(model, node_id)
{
    setZValue(-2);
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsMovable, true);
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemIsSelectable, true);
    setFlag(QGraphicsItem::GraphicsItemFlag::ItemSendsScenePositionChanges, true);
    m_height = 1;
    m_width = 1;
    m_text_item = new NodeTextItem(
        display_name.c_str(), *this,
        [this](const QString& new_name) {
            const auto new_id = SdfPath(get_id()).GetParentPath().AppendChild(TfToken(new_name.toLocal8Bit().data())).GetString();
            return static_cast<const UsdGraphModel&>(get_model()).rename(get_id(), new_id);
        },
        this);
    m_sizer = new BackdropSizerItem(this);
    float sizer_size = 20;
    m_sizer->set_size(sizer_size);

    m_description_text_item = new NodeTextEditor(m_desc, sceneBoundingRect(), this);
    m_description_text_item->set_sizer_size(sizer_size);

    m_display_color = QColor(70, 70, 70);
}

BackdropNodeItem::~BackdropNodeItem() {}

void BackdropNodeItem::update_color(const PXR_NS::UsdUINodeGraphNodeAPI& api)
{
    assert(api);

    GfVec3f color;
    if (api.GetDisplayColorAttr() && api.GetDisplayColorAttr().Get(&color))
        m_display_color.setRgbF(color[0], color[1], color[2]);
    else
        m_display_color.setRgb(64, 64, 64);

    update();
}
void BackdropNodeItem::update_size(const PXR_NS::UsdUINodeGraphNodeAPI& prim)
{
    assert(prim.GetPrim());
    GfVec2f size;
    if (prim.GetSizeAttr() && prim.GetSizeAttr().Get(&size))
    {
        // if node size has changed then we have to also update its pos,
        // since its calculation depends on size
        resize(size[0], size[1]);
        update_pos();
        // UsdNodeItem::update_port(UsdUITokens->uiNodegraphNodePos);
    }
    else
    {
        resize(s_default_start_size.width(), s_default_start_size.height());
    }
}
void BackdropNodeItem::update_descr(const PXR_NS::UsdUIBackdrop& prim)
{
    assert(prim.GetPrim());
    TfToken desc;
    if (prim.GetDescriptionAttr() && prim.GetDescriptionAttr().Get(&desc))
    {

        m_desc = QString::fromStdString(desc.GetString());
        m_description_text_item->setPlainText(m_desc);
    }
    update();
}

void BackdropNodeItem::update_pos()
{
    auto pos = get_model().get_node_position(get_id());
    setPos(to_scene_position(pos, boundingRect().width()));
}

void BackdropNodeItem::update_descr_font_scale()
{
    auto ui_prim = UsdUIExtBackdropUIAPI(get_model().get_prim_for_node(get_id()));
    assert(ui_prim);

    float font_scale;
    if (ui_prim.GetUiDescriptionFontScaleAttr() && ui_prim.GetUiDescriptionFontScaleAttr().Get(&font_scale))
    {
        m_description_text_item->set_font_scale(font_scale);

        if (!m_desc.isEmpty())
            m_description_text_item->setPlainText(m_desc);
    }
}

void BackdropNodeItem::update_title_vis()
{
    auto ui_prim = UsdUIExtBackdropUIAPI(get_model().get_prim_for_node(get_id()));
    assert(ui_prim);

    bool is_currently_visible = m_text_item->isVisible();
    bool is_enabled = is_currently_visible;

    if (ui_prim.GetUiBackdropNodeShowTitleAttr() && ui_prim.GetUiBackdropNodeShowTitleAttr().Get(&is_enabled))
    {
        if (is_enabled ^ is_currently_visible)
            m_text_item->setVisible(is_enabled);
    }
}

void BackdropNodeItem::update_descr_vis()
{
    auto ui_prim = UsdUIExtBackdropUIAPI(get_model().get_prim_for_node(get_id()));
    assert(ui_prim);

    bool is_currently_visible = m_description_text_item->isVisible();
    bool is_enabled = is_currently_visible;

    if (ui_prim.GetUiBackdropNodeShowDescriptionAttr() && ui_prim.GetUiBackdropNodeShowDescriptionAttr().Get(&is_enabled))
    {
        if (is_enabled ^ is_currently_visible)
            m_description_text_item->setVisible(is_enabled);
    }
}

void BackdropNodeItem::update_node()
{
    NodeItem::update_node();

    auto prim = get_model().get_prim_for_node(get_id());
    auto api = UsdUINodeGraphNodeAPI(prim);
    update_color(api);
    update_size(api);
    if (auto backdrop = UsdUIBackdrop(prim))
        update_descr(backdrop);
    if (UsdUIExtBackdropUIAPI(prim))
    {
        update_descr_font_scale();
        update_descr_vis();
        update_title_vis();
    }
    update_pos();
    align_label();
}

void BackdropNodeItem::update_port(const PortId& port_id)
{
    NodeItem::update_port(port_id);
    const auto& prim = get_model().get_prim_for_node(get_id());
    assert(prim);
    const auto api = UsdUINodeGraphNodeAPI(prim);
    const auto& property_name = get_model().get_property_name(port_id);
    if (property_name == UsdUITokens->uiNodegraphNodeDisplayColor)
        update_color(api);
    else if (property_name == UsdUITokens->uiNodegraphNodeSize)
        update_size(api);
    else if (property_name == UsdUITokens->uiDescription)
        update_descr(UsdUIBackdrop(prim));
    else if (property_name == UsdUITokens->uiNodegraphNodePos)
        update_pos();
    else if (property_name == UsdUIExtTokens->uiBackdropNodeShowTitle)
        update_title_vis();
    else if (property_name == UsdUIExtTokens->uiBackdropNodeShowDescription)
        update_descr_vis();
    else if (property_name == UsdUIExtTokens->uiDescriptionFontScale)
        update_descr_font_scale();

    align_label();
}

QRectF BackdropNodeItem::boundingRect() const
{
    return QRectF(0, 0, m_width, m_height);
}

void BackdropNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    draw_backdrop(painter, boundingRect(), m_display_color, isSelected());
}

void BackdropNodeItem::resize(float width, float height)
{
    // it's also will trigger node resize
    m_sizer->setPos(width - m_sizer->boundingRect().width(), height - m_sizer->boundingRect().height());
}

void BackdropNodeItem::on_sizer_pos_changed(QPointF pos)
{
    auto size = m_sizer->get_size();
    prepareGeometryChange();
    m_width = pos.x() + size;
    m_height = pos.y() + size;
    auto prim = UsdUINodeGraphNodeAPI(get_model().get_prim_for_node(get_id()));
    assert(prim);

    align_label();
    m_description_text_item->update_bounding_rect(QRectF(0, 0, m_width, m_height));

    if (!m_desc.isEmpty())
        m_description_text_item->setPlainText(m_desc);

    update();
}

void BackdropNodeItem::remove_connection(ConnectionItem* item) {}

UsdGraphModel& OPENDCC_NAMESPACE::BackdropNodeItem::get_model()
{
    return static_cast<UsdGraphModel&>(NodeItem::get_model());
}

void BackdropNodeItem::add_connection(ConnectionItem* item) {}

void BackdropNodeItem::align_label()
{
    auto text_rect = m_text_item->boundingRect();
    auto text_x = m_width / 2 - text_rect.width() / 2;
    auto text_y = -text_rect.height();
    m_text_item->setPos(text_x, text_y);
}

void BackdropNodeItem::update_overlapped_nodes_list()
{
    m_nodes.clear();
    const auto contained_items = scene()->items(sceneBoundingRect(), Qt::ItemSelectionMode::ContainsItemShape);
    for (const auto item : contained_items)
    {
        if (auto node_item = qgraphicsitem_cast<NodeItem*>(item))
            m_nodes.push_back(node_item->get_id());
    }
}

QVariant BackdropNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == GraphicsItemChange::ItemPositionChange && m_dragging)
    {
        const auto new_value = value.toPointF();
        const auto delta = new_value - pos();
        for (auto node : m_nodes)
        {
            if (auto item = get_scene()->get_item_for_node(node))
            {
                auto new_pos = item->scenePos() + delta;
                item->setPos(new_pos);
            }
        }
    }

    return NodeItem::itemChange(change, value);
}

void BackdropNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    update_overlapped_nodes_list();
    m_start_drag = event->scenePos();
    m_dragging = true;
    QVector<NodeId> nodes_to_move = m_nodes;
    nodes_to_move.push_back(get_id());
    get_scene()->begin_move(nodes_to_move);
    NodeItem::mousePressEvent(event);
}

void BackdropNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    m_start_drag = QPointF();
    m_dragging = false;
    get_scene()->end_move();
    NodeItem::mouseReleaseEvent(event);
}

void BackdropNodeItem::move_connections()
{
    // does nothing
}

OPENDCC_NAMESPACE_CLOSE
