// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <QGuiApplication>
#include "opendcc/ui/common_widgets/marking_menu.h"
#include <QDebug>
#include "QPushButton"
#include "qevent.h"
#include <QDialog>
#include "QApplication"
#include "QPainter"
#include "QTimer"
#include <QStyleOptionButton>
#include "QBrush"
#include <QAction>
#include <QActionGroup>
#include "QWidget"
#include <QPaintEngine>
#include <QScreen>
#include <stack>
#include <cmath>

OPENDCC_NAMESPACE_OPEN

MarkingMenu::MarkingMenu(const QPoint& global_pos, QWidget* parent /*= nullptr*/)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    const auto* point_screen = QGuiApplication::screenAt(global_pos);
    if (!point_screen)
        point_screen = QGuiApplication::primaryScreen();
    setGeometry(point_screen->availableGeometry());
    m_mouse_pos = this->mapFromGlobal(global_pos);
    setAttribute(Qt::WA_TranslucentBackground);
}

void MarkingMenu::set_extended_menu(QMenu* menu)
{
    if (!menu)
        return;

    m_menu_stack.push(menu);
    m_polyline.emplace_back(m_mouse_pos);
    m_can_go_back = false;
    const auto& actions = menu->actions();
    std::vector<ItemInfo> item_infos(actions.size());
    for (size_t i = 0; i < actions.size(); i++)
    {
        auto action_rect = menu->actionGeometry(actions[i]);
        auto pos = m_mouse_pos + get_widget_pos(i, action_rect);
        ItemInfo info;
        info.rect = QRect(pos.x(), pos.y(), action_rect.width(), action_rect.height());
        info.action = actions[i];
        item_infos[i] = std::move(info);
    }
    m_actions[menu] = item_infos;
}

void MarkingMenu::on_mouse_move(const QPoint& global_pos)
{
    m_mouse_pos = this->mapFromGlobal(global_pos);
    static const float min_distance_from_center = 15;

    const auto& cur_point = m_polyline[m_polyline.size() - 1];
    double dist_to_cur_point = std::sqrt(QPointF::dotProduct(m_mouse_pos - cur_point, m_mouse_pos - cur_point));

    if (m_polyline.size() > 1)
    {
        const auto& prev_point = m_polyline[m_polyline.size() - 2];
        double dist_to_prev_point = std::sqrt(QPointF::dotProduct(m_mouse_pos - prev_point, m_mouse_pos - prev_point));

        if (!m_can_go_back && dist_to_prev_point > min_distance_from_center)
        {
            m_can_go_back = true;
        }
        else if (m_can_go_back && m_menu_stack.size() != 1 && dist_to_prev_point <= min_distance_from_center)
        {
            m_menu_stack.pop();
            m_polyline.pop_back();
        }
    }
    else if (dist_to_cur_point <= min_distance_from_center)
    {
        m_hovered_action = nullptr;
        update();
        return;
    }

    double min_dist_to_action = std::numeric_limits<double>::max();
    for (const auto& info : m_actions[m_menu_stack.top()])
    {
        if (info.action->menu() && info.rect.contains(m_mouse_pos))
        {
            set_extended_menu(info.action->menu());
            on_mouse_move(global_pos);
            return;
        }

        // evaluate distance to menu item rect
        QPointF clamped_point = m_mouse_pos;
        clamped_point.setX(std::max(std::min(m_mouse_pos.x(), info.rect.x() + info.rect.width()), info.rect.x()));
        clamped_point.setY(std::max(std::min(m_mouse_pos.y(), info.rect.y() + info.rect.height()), info.rect.y()));
        auto x = (m_mouse_pos.x() - clamped_point.x());
        auto y = (m_mouse_pos.y() - clamped_point.y());
        double dist_to_rect = std::sqrt(x * x + y * y);
        if (dist_to_rect < min_dist_to_action)
        {
            m_hovered_action = info.action;
            min_dist_to_action = dist_to_rect;
        }
    }

    update();
}

void MarkingMenu::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::gray, 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawPolyline(m_polyline.data(), m_polyline.size());

    painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(QBrush(Qt::darkGray));
    for (auto i = 0; i < m_polyline.size() - 1; i++)
    {
        painter.drawEllipse(m_polyline[i], 7, 7);
    }

    const auto cur_menu = m_menu_stack.top();
    if (cur_menu)
    {
        for (const auto& info : m_actions[cur_menu])
        {
            QStyleOptionMenuItem menu_option;
            menu_option.initFrom(cur_menu);
            menu_option.rect = info.rect;
            cur_menu->style()->drawPrimitive(QStyle::PE_PanelMenu, &menu_option, &painter);

            QStyleOptionMenuItem menu_item_option;
            menu_item_option.initFrom(cur_menu);
            menu_item_option.rect = info.rect;
            menu_item_option.text = info.action->text();
            menu_item_option.state |= QStyle::State_Active | QStyle::State_Enabled;
            if (m_hovered_action == info.action)
            {
                menu_item_option.state |= QStyle::State_Selected;
            }
            menu_item_option.menuHasCheckableItems = info.action->isCheckable();
            if (info.action->isCheckable())
            {
                menu_item_option.checkType = (info.action->actionGroup() && info.action->actionGroup()->isExclusive())
                                                 ? QStyleOptionMenuItem::Exclusive
                                                 : QStyleOptionMenuItem::NonExclusive;
                menu_item_option.checked = info.action->isChecked();
            }
            if (info.action->menu())
                menu_item_option.menuItemType = QStyleOptionMenuItem::SubMenu;
            else
                menu_item_option.menuItemType = QStyleOptionMenuItem::Normal;
            if (info.action->isIconVisibleInMenu())
                menu_item_option.icon = info.action->icon();
            menu_item_option.menuRect = info.rect;

            m_menu_stack.top()->style()->drawControl(QStyle::CE_MenuItem, &menu_item_option, &painter);
        }
    }

    painter.setPen(QPen(Qt::gray, 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(m_polyline.back(), m_mouse_pos);

    painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(QBrush(Qt::darkGray));
    painter.drawEllipse(m_polyline.back(), 7, 7);

    painter.end();
}

OPENDCC_NAMESPACE_CLOSE
