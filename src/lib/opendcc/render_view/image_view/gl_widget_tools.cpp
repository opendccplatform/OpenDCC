// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "gl_widget_tools.hpp"
#include "gl_widget.h"
#include <opendcc/render_view/image_view/app.h>

#include <QtGui/QMouseEvent>

OPENDCC_NAMESPACE_OPEN

void RenderViewGLWidgetTool::set_tool()
{
    m_glwidget->set_current_tool(this);
    m_action->setChecked(true);
}

void GLWidgetPanZoomTool::mouse_press(QMouseEvent* mouse_event)
{
    bool Alt = (mouse_event->modifiers() & Qt::AltModifier);

    m_glwidget->update_mouse_pos(mouse_event);

    Qt::MouseButton button = mouse_event->button();

    if (button == Qt::LeftButton)
        m_mousemode = MouseMode::MouseModePan;
    else if (button == Qt::RightButton && Alt)
        m_mousemode = MouseMode::MouseModeZoom;
}

void GLWidgetPanZoomTool::mouse_move(QMouseEvent* mouse_event)
{
    QPoint point = mouse_event->pos();
    bool Alt = (mouse_event->modifiers() & Qt::AltModifier);

    if (m_mousemode == MouseMode::MouseModePan)
    {
        float dx = (point.x() - m_glwidget->m_mousex) / m_glwidget->m_zoom;
        float dy = (point.y() - m_glwidget->m_mousey) / m_glwidget->m_zoom;
        m_glwidget->m_centerx -= dx;
        m_glwidget->m_centery -= dy;
        m_glwidget->update();
    }
    else if (m_mousemode == MouseMode::MouseModeZoom && Alt)
    {

        float dx = (point.x() - m_glwidget->m_mousex);
        float dy = (point.y() - m_glwidget->m_mousey);

        float z = m_glwidget->m_zoom * (1.0 + 0.005 * (dx + dy));
        z = z < 0.01f ? 0.01f : z;
        z = z > 256.0f ? 256.0f : z;
        m_glwidget->m_zoom = z;
        m_glwidget->update();
    }

    m_glwidget->update_mouse_pos(mouse_event);
}

void GLWidgetPanZoomTool::mouse_release(QMouseEvent* mouse_event)
{
    m_glwidget->update_mouse_pos(mouse_event);
    m_mousemode = MouseMode::MouseModeNone;
}

void GLWidgetPanZoomTool::wheel_event(QWheelEvent* event)
{
    m_glwidget->m_zoom += event->angleDelta().y() / 1500.0 * m_glwidget->m_zoom;
    if (m_glwidget->m_zoom > 60)
    {
        m_glwidget->m_zoom = 60;
    }
    else if (m_glwidget->m_zoom < 0.05)
    {
        m_glwidget->m_zoom = 0.05;
    }
    m_glwidget->update();
}

void GLWidgetPanZoomTool::init_action()
{
    m_action = new QAction(i18n("render_view.tool.pan", "Pan Tool"), this);
    m_action->setCheckable(true);
    m_action->setShortcut(i18n("render_view.tool.pan.shortcut", "1"));
    m_action->setIcon(QIcon(":icons/render_view/pan"));
    connect(m_action, SIGNAL(triggered()), this, SLOT(set_tool()));
}

void GLWidgetCropRegionTool::mouse_press(QMouseEvent* mouse_event)
{
    Qt::MouseButton button = mouse_event->button();
    m_start_crop = false;
    if (button == Qt::LeftButton)
    {

        m_glwidget->update_mouse_pos(mouse_event);
        m_glwidget->set_crop_display(false);
        m_start_crop = true;
        m_xstart = m_glwidget->m_mousex;
        m_ystart = m_glwidget->m_mousey;
    }
    else
    {
        GLWidgetPanZoomTool::mouse_press(mouse_event);
    }
}

void GLWidgetCropRegionTool::mouse_move(QMouseEvent* mouse_event)
{
    if (m_start_crop)
    {
        m_glwidget->update_mouse_pos(mouse_event);

        m_glwidget->set_crop_display(true);

        RenderViewGLWidget::ROI region;

        float xstart, ystart, xend, yend;
        m_glwidget->widget_to_image_pos(m_xstart, m_ystart, xstart, ystart);
        m_glwidget->widget_to_image_pos(m_glwidget->m_mousex, m_glwidget->m_mousey, xend, yend);

        // we should swap start and end point if user will draw it inverted
        // arnold renderer do not understand region this way and result in render error
        region.xstart = xstart > xend ? xend : xstart;
        region.ystart = ystart > yend ? yend : ystart;
        region.xend = xend < xstart ? xstart : xend;
        region.yend = yend < ystart ? ystart : yend;

        m_glwidget->set_crop_region(region);
        m_glwidget->update();
    }
    else
    {
        GLWidgetPanZoomTool::mouse_move(mouse_event);
    }
}

void GLWidgetCropRegionTool::mouse_release(QMouseEvent* mouse_event)
{
    m_start_crop = false;
    auto region = m_glwidget->get_crop_region();
    emit region_update(m_glwidget->is_crop_displayed(), region.xstart, region.xend, region.ystart, region.yend);
    GLWidgetPanZoomTool::mouse_release(mouse_event);
}

void GLWidgetCropRegionTool::init_action()
{
    m_action = new QAction(i18n("render_view.tool.crop", "Crop Tool"), this);
    m_action->setCheckable(true);
    m_action->setShortcut(i18n("render_view.tool.crop.shortcut", "3"));
    m_action->setIcon(QIcon(":icons/render_view/crop"));
    connect(m_action, SIGNAL(triggered()), this, SLOT(set_tool()));
}

OPENDCC_NAMESPACE_CLOSE
