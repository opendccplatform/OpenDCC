// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "channel_editor.h"

#include <QItemDelegate>
#include <QHeaderView>
#include <QMouseEvent>
#include <QApplication>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

#include "opendcc/anim_engine/core/session.h"
#include "opendcc/app/ui/node_icon_registry.h"
#include "opendcc/anim_engine/core/utils.h"
#include "opendcc/anim_engine/ui/graph_editor/utils.h"
#include "opendcc/ui/common_widgets/ladder_widget.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

namespace
{
    const int num_digits = 6;
    static std::set<TfToken> xformOp_attributes_tokens = { TfToken("xformOp:translate"), TfToken("xformOp:rotateXYZ"), TfToken("xformOp:scale") };
    const auto keyed_item_color = QColor(10, 166, 233);
    const auto animated_item_color = QColor(221, 114, 122);
    const auto default_item_color = QColor(10, 10, 10);
}

class LadderNumberWidgetWithCounter : public LadderNumberWidget
{
public:
    LadderNumberWidgetWithCounter(QWidget* parent = nullptr, bool as_int = false)
        : LadderNumberWidget(parent, as_int)
    {
    }

    size_t counter = 0; // need for some work around
};

class ComponentTreeItem : public QTreeWidgetItem
{
public:
    ComponentTreeItem(OPENDCC_NAMESPACE::AnimEngine::CurveId curve_id, QTreeWidgetItem* parent);
    ComponentTreeItem(UsdAttribute& attr, uint32_t component_idx, QTreeWidgetItem* parent);

    OPENDCC_NAMESPACE::AnimEngine::CurveId curve_id() const { return m_curve_id; }

    UsdAttribute& attribute() { return m_attribute; }
    uint32_t component() const { return m_component; }

private:
    OPENDCC_NAMESPACE::AnimEngine::CurveId m_curve_id;
    UsdAttribute m_attribute;
    uint32_t m_component;
};

std::string component_postfix(uint32_t component_idx, AttributeClass attribute_class)
{
    switch (attribute_class)
    {
    case AttributeClass::Scalar:
        break;
    case AttributeClass::Vector:
        if (component_idx == 0)
            return ".x";
        else if (component_idx == 1)
            return ".y";
        else if (component_idx == 2)
            return ".z";
        else if (component_idx == 3)
            return ".w";
        else
            return "[" + std::to_string(component_idx) + "]";
        break;

    case AttributeClass::Color:
        if (component_idx == 0)
            return ".r";
        else if (component_idx == 1)
            return ".g";
        else if (component_idx == 2)
            return ".b";
        else if (component_idx == 3)
            return ".a";
        else
            return "[" + std::to_string(component_idx) + "]";
        break;

    case AttributeClass::Other:
        return "[" + std::to_string(component_idx) + "]";
    }
    return "";
}

ComponentTreeItem::ComponentTreeItem(OPENDCC_NAMESPACE::AnimEngine::CurveId curve_id, QTreeWidgetItem* parent)
    : QTreeWidgetItem(parent)
    , m_curve_id(curve_id)
{
    auto curve = AnimEngineSession::instance().current_engine()->get_curve(curve_id);
    m_attribute = curve->attribute();
    m_component = curve->component_idx();

    std::string text = curve->attribute().GetBaseName().GetString();
    if (num_components_in_attribute(m_attribute) > 1)
        text += component_postfix(m_component, attribute_class(curve->attribute()));
    setText(0, text.c_str());
    if (num_components_in_attribute(m_attribute) > 1)
        setForeground(0, QBrush(color_for_component(curve->component_idx())));
}

ComponentTreeItem::ComponentTreeItem(UsdAttribute& attribute, uint32_t component_idx, QTreeWidgetItem* parent)
    : QTreeWidgetItem(parent)
    , m_attribute(attribute)
    , m_component(component_idx)
{
    std::string text = attribute.GetBaseName().GetString();
    if (num_components_in_attribute(m_attribute) > 1)
        text += component_postfix(m_component, attribute_class(m_attribute));

    setText(0, text.c_str());
}

class ValueEditorDelegate : public QItemDelegate
{
    ChannelEditor* m_tree = nullptr;

public:
    ValueEditorDelegate(ChannelEditor* tree)
        : QItemDelegate(tree)
        , m_tree(tree)
    {
    }
    virtual QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        if (index.column() != 1)
            return nullptr;

        auto value_widget = new LadderNumberWidgetWithCounter(parent, false);

        auto on_value_changed = [this, value_widget]() {
            ++value_widget->counter;
            if (value_widget->counter == 1) // not set values to selection in first time, when QItemDelegate setup a value to the widget
                return;

            m_tree->set_value(value_widget->text().toDouble());
        };

        auto on_editing_finished = [this, value_widget]() {
            m_tree->m_undo_block.reset();
        };

        connect(value_widget, &LadderNumberWidget::textChanged, this, on_value_changed);
        connect(value_widget, &LadderNumberWidget::editingFinished, this, on_editing_finished);
        return value_widget;
    }
};

ChannelEditor::ChannelEditor(bool simplified_version, QWidget* parent)
    : QTreeWidget(parent)
    , m_is_simplified_version(simplified_version)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::AllEditTriggers);
    if (!simplified_version)
    {
        setItemDelegate(new ValueEditorDelegate(this));
        m_application_events_handles[Application::EventType::SELECTION_CHANGED] =
            Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] { update_content(); });
        m_application_events_handles[Application::EventType::CURRENT_STAGE_CHANGED] =
            Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this]() { update_content(); });
        m_application_events_handles[Application::EventType::CURRENT_TIME_CHANGED] =
            Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this]() { update_values(); });
        m_application_events_handles[Application::EventType::BEFORE_CURRENT_STAGE_CLOSED] =
            Application::instance().register_event_callback(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED, [this]() { update_content(); });
    }
    setAlternatingRowColors(true);
    update_content();
}

void ChannelEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MouseButton::MiddleButton)
    {
        auto item = itemAt(e->pos());
        if (!item)
        {
            QTreeWidget::mousePressEvent(e);
            return;
        }
        m_current_item = dynamic_cast<ComponentTreeItem*>(item);
        if (!m_current_item)
        {
            QTreeWidget::mousePressEvent(e);
            return;
        }

        if (!get_usd_attribute_component(m_current_item->attribute(), m_current_item->component(), m_start_value))
            m_start_value = 1;

        bool is_current_item_in_selection = false;
        for (auto item : selectedItems())
        {
            if (item == m_current_item)
            {
                is_current_item_in_selection = true;
                break;
            }
        }
        if (!is_current_item_in_selection)
        {
            clearSelection();
            m_current_item->setSelected(true);
        }

        QApplication::setOverrideCursor(Qt::SizeHorCursor);

        m_pos = e->globalPos();
        e->accept();
        m_activated = true;
        auto m_as_int = false;

        if (m_as_int)
            m_ladder = new LadderScale(this, 0.9, 1000, 10);
        else
            m_ladder = new LadderScale();
        m_ladder->updateGeometry();
        m_ladder->show();
        if (m_as_int)
            m_ladder->move(e->globalPos() - QPoint(0, m_ladder->height() - 15));
        else
            m_ladder->move(e->globalPos() - QPoint(0, m_ladder->height() / 2));
        m_ladder->pointer_changed(m_pos);
    }
    else
        QTreeWidget::mousePressEvent(e);
}

void ChannelEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_activated)
    {
        auto m_enable_clamp = false;

        auto pos = e->globalPos();
        if (!m_ladder->pointer_changed(pos))
        {
            auto delta = (pos.x() - m_pos.x()) / LADDER_SENS * m_ladder->get_scale();
            qreal val = m_start_value + delta;
            m_ladder->set_target_value(m_start_value + delta);
            set_value(val);
        }
        else
        {
            m_pos = pos;
            m_ladder->set_target_value(m_start_value);
        }
    }
    else
        QTreeWidget::mouseMoveEvent(e);
}

void ChannelEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_activated)
    {
        QApplication::restoreOverrideCursor();
        m_activated = false;
        delete m_ladder;
        m_ladder = nullptr;
        m_current_item = nullptr;
        m_undo_block.reset();
    }
    else
        QTreeWidget::mouseReleaseEvent(e);
}

void ChannelEditor::update_prim_item(QTreeWidgetItem* prim_item, SdfPath path)
{
    foreach (auto i, prim_item->takeChildren())
        delete i;

    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
        return;

    auto prim = stage->GetPrimAtPath(path);
    if (!prim)
    {
        delete prim_item;
        return;
    }

    OPENDCC_NAMESPACE::AnimEnginePtr engine = AnimEngineSession::instance().current_engine();

    auto add_items_for_attribute = [&engine, &prim_item](UsdAttribute& attribute) {
        if (!attribute)
            return;

        bool is_in_channel_box = false;
        auto is_in_channel_box_vtvalue = attribute.GetCustomDataByKey(TfToken("isInChannelBox"));
        if (!is_in_channel_box_vtvalue.IsEmpty())
            is_in_channel_box = is_in_channel_box_vtvalue.Get<bool>();

        if (xformOp_attributes_tokens.find(attribute.GetName()) == xformOp_attributes_tokens.end() && !is_in_channel_box)
            return;

        auto num_components = num_components_in_attribute(attribute);

        for (size_t component_idx = 0; component_idx < num_components; ++component_idx)
        {
            auto* curve_item = new ComponentTreeItem(attribute, component_idx, prim_item);
            double value = 0;
            bool ok = get_usd_attribute_component(attribute, component_idx, value, Application::instance().get_current_time());

            curve_item->setText(1, QString::number(value, 'g', num_digits));
            curve_item->setForeground(1, QBrush(Qt::white));

            if (attribute.GetNumTimeSamples() > 0)
            {
                curve_item->setBackground(1, QBrush(keyed_item_color));
            }
            else
            {
                if (engine && engine->is_attribute_animated(attribute, component_idx))
                    curve_item->setBackground(1, QBrush(animated_item_color));
                else
                    curve_item->setBackground(1, QBrush(default_item_color));
            }
            curve_item->setFlags(curve_item->flags() | Qt::ItemIsEditable);
            curve_item->setTextAlignment(0, Qt::AlignRight);
            curve_item->setTextAlignment(1, Qt::AlignLeft);
        } // for components_indices
    };

    auto xformOpOrder = prim.GetAttribute(UsdGeomTokens->xformOpOrder);
    VtTokenArray xform_tokens;
    if (xformOpOrder)
    {
        xformOpOrder.Get(&xform_tokens);
        for (auto& token : xform_tokens)
        {
            auto attr = prim.GetAttribute(token);
            add_items_for_attribute(attr);
        }
    }

    for (auto attribute : prim.GetAttributes())
    {
        if (std::find(xform_tokens.begin(), xform_tokens.end(), attribute.GetName()) == xform_tokens.end())
            add_items_for_attribute(attribute);
    } // for attributes_tokens
}

void ChannelEditor::on_objects_changed(UsdNotice::ObjectsChanged const& notice, UsdStageWeakPtr const& sender)
{
    if (m_ignore_stage_changing)
        return;
    std::unordered_set<SdfPath, SdfPath::Hash> prims_paths_for_resync;
    std::unordered_set<SdfPath, SdfPath::Hash> prims_paths_for_update;
    using PathRange = UsdNotice::ObjectsChanged::PathRange;
    const PathRange paths_to_resync = notice.GetResyncedPaths();
    const PathRange paths_to_update = notice.GetChangedInfoOnlyPaths();

    for (PathRange::const_iterator path_iter = paths_to_resync.begin(); path_iter != paths_to_resync.end(); ++path_iter)
    {
        auto prim_path = path_iter->GetAbsoluteRootOrPrimPath();
        auto prim = notice.GetStage()->GetPrimAtPath(prim_path);
        if (!prim)
        {
            update_content();
            return;
        }

        if (m_items_map.find(prim_path) != m_items_map.end())
            prims_paths_for_resync.insert(prim_path);
    }

    for (PathRange::const_iterator path_iter = paths_to_update.begin(); path_iter != paths_to_update.end(); ++path_iter)
    {
        auto prim_path = path_iter->GetAbsoluteRootOrPrimPath();
        if (m_items_map.find(prim_path) != m_items_map.end())
            prims_paths_for_update.insert(prim_path);
    }

    for (auto path : prims_paths_for_resync)
    {
        update_prim_item(m_items_map.at(path), path);
    }

    for (auto path : prims_paths_for_update)
    {
        if (prims_paths_for_resync.find(path) != prims_paths_for_resync.end())
            continue;

        QTreeWidgetItem* prim_item = m_items_map.at(path);
        for (int i = 0; i < prim_item->childCount(); ++i)
        {
            ComponentTreeItem* item = dynamic_cast<ComponentTreeItem*>(prim_item->child(i));
            if (!item)
                continue;
            double value = 0;
            bool ok = get_usd_attribute_component(item->attribute(), item->component(), value, Application::instance().get_current_time());
            item->setText(1, QString::number(value, 'g', num_digits));
        }
    }
}

void ChannelEditor::update_values()
{
    for (auto item : m_items_map)
    {
        update_prim_item(item.second, item.first);
    }
}

void ChannelEditor::set_value(double value)
{
    m_ignore_stage_changing = true;
    if (!m_undo_block)
        m_undo_block = std::make_unique<commands::UsdEditsUndoBlock>();

    auto selected_items = selectedItems();

    std::vector<ComponentTreeItem*> components_items;
    for (auto item : selected_items)
    {
        ComponentTreeItem* attr_item = dynamic_cast<ComponentTreeItem*>(item);
        if (attr_item && attr_item->attribute())
            components_items.push_back(attr_item);
    }

    if (components_items.size() == 0)
        components_items.push_back(m_current_item);

    for (ComponentTreeItem* attr_item : components_items)
    {
        auto stack = attr_item->attribute().GetStage()->GetLayerStack();
        auto blocked = false;
        if (attr_item->attribute().GetNumTimeSamples() > 0)
        {
            blocked = true;
        }
        else
        {
            for (auto item : stack)
            {
                if (item == attr_item->attribute().GetStage()->GetEditTarget().GetLayer())
                    break;

                if (attr_item->attribute().IsAuthoredAt(item))
                {
                    blocked = true;
                    break;
                }
            }
        }
        if (!blocked)
        {
            set_usd_attribute_component(attr_item->attribute(), attr_item->component(), value);
            attr_item->setText(1, QString::number(value, 'g', num_digits));
        }
    }
    m_ignore_stage_changing = false;
}

ChannelEditor::~ChannelEditor()
{
    if (!m_is_simplified_version)
        TfNotice::Revoke(m_objects_changed_notice_key);

    for (auto it : m_application_events_handles)
        Application::instance().unregister_event_callback(it.first, it.second);
}

void ChannelEditor::update_content()
{
    clear();
    m_items_map.clear();
    if (!m_is_simplified_version)
        TfNotice::Revoke(m_objects_changed_notice_key);

    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
        return;

    if (!m_is_simplified_version)
        m_objects_changed_notice_key = TfNotice::Register(TfCreateWeakPtr(this), &ChannelEditor::on_objects_changed, stage);

    SelectionList selection = Application::instance().get_selection();
    auto& node_icon_registry = NodeIconRegistry::instance();

    if (m_is_simplified_version)
    {
        OPENDCC_NAMESPACE::AnimEnginePtr engine = AnimEngineSession::instance().current_engine();
        if (!engine)
            return;
        setColumnCount(1);
        for (auto it : selection)
        {
            SdfPath path = it.first;
            auto curves_ids = engine->curves(path);
            if (curves_ids.size() == 0)
                continue;

            auto prim = stage->GetPrimAtPath(path);
            if (!prim)
                continue;

            auto* prim_item = new QTreeWidgetItem(this);

            prim_item->setText(0, path.GetName().c_str());
            prim_item->setExpanded(true);
            prim_item->setSelected(true);

            auto type_icon = QPixmap(QString((":icons/" + prim.GetTypeName().GetString()).c_str()).toLower());
            prim_item->setIcon(0, type_icon);

            for (auto curve_id : curves_ids)
            {
                auto* curve_item = new ComponentTreeItem(curve_id, prim_item);
            }
        }
    }
    else
    {
        setColumnCount(2);
        setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);
        auto time = Application::instance().get_current_time();
        for (auto it : selection)
        {
            SdfPath path = it.first;
            auto prim = stage->GetPrimAtPath(path);
            if (!prim)
                continue;

            auto* prim_item = new QTreeWidgetItem(this);
            m_items_map[path] = prim_item;
            prim_item->setText(0, path.GetName().c_str());
            prim_item->setExpanded(true);
            prim_item->setSelected(true);

            auto type_icon = QPixmap(QString((":icons/" + prim.GetTypeName().GetString()).c_str()).toLower());
            prim_item->setIcon(0, type_icon);

            update_prim_item(prim_item, path);
        } // for selection

        header()->setSectionResizeMode(0, QHeaderView::Stretch);
        header()->setSectionResizeMode(1, QHeaderView::Fixed);
        header()->resizeSection(1, 70);
        header()->setStretchLastSection(false);
        // setStyleSheet("QTreeView::item {  border: 0px;  padding: 0 15px; border: 1px solid rgb(103, 141, 178) }");
    }
}

std::set<OPENDCC_NAMESPACE::AnimEngine::CurveId> ChannelEditor::selected_curves_ids()
{
    auto selected_items = selectedItems();
    std::set<OPENDCC_NAMESPACE::AnimEngine::CurveId> result;

    for (auto* item : selected_items)
    {
        ComponentTreeItem* curve_item = dynamic_cast<ComponentTreeItem*>(item);
        if (curve_item)
        {
            result.insert(curve_item->curve_id());
        }
        else
        {
            for (int i = 0; i < item->childCount(); ++i)
            {
                ComponentTreeItem* curve_item = dynamic_cast<ComponentTreeItem*>(item->child(i));
                if (curve_item && curve_item->curve_id().valid())
                    result.insert(curve_item->curve_id());
            }
        }
    }
    return result;
}

OPENDCC_NAMESPACE_CLOSE
