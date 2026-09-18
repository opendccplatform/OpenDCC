// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "opendcc/opendcc.h"
#include <QtCore/QObject>
#include <QAction>

class QMouseEvent;
class QWheelEvent;

OPENDCC_NAMESPACE_OPEN

class RenderViewGLWidget;

class RenderViewGLWidgetTool : public QObject
{
    Q_OBJECT
public:
    RenderViewGLWidgetTool(RenderViewGLWidget* gl_widget)
        : m_glwidget(gl_widget)
    {
    }
    virtual ~RenderViewGLWidgetTool() {}

    virtual void init_action() {};
    virtual void mouse_press(QMouseEvent* mouse_event) = 0;
    virtual void mouse_move(QMouseEvent* mouse_event) = 0;
    virtual void mouse_release(QMouseEvent* mouse_event) = 0;
    virtual void wheel_event(QWheelEvent* event) = 0;

    QAction* tool_action() { return m_action; }

public Q_SLOTS:
    void set_tool();

protected:
    RenderViewGLWidget* m_glwidget;
    QAction* m_action;
};

class GLWidgetPanZoomTool : public RenderViewGLWidgetTool
{
public:
    GLWidgetPanZoomTool(RenderViewGLWidget* gl_widget)
        : RenderViewGLWidgetTool(gl_widget)
    {
    }
    virtual ~GLWidgetPanZoomTool() {};

    enum class MouseMode
    {
        MouseModeNone,
        MouseModePan,
        MouseModeZoom
    };

    virtual void init_action() override;
    virtual void mouse_press(QMouseEvent* mouse_event) override;
    virtual void mouse_move(QMouseEvent* mouse_event) override;
    virtual void mouse_release(QMouseEvent* mouse_event) override;
    virtual void wheel_event(QWheelEvent* event) override;

protected:
    MouseMode m_mousemode;
};

class GLWidgetCropRegionTool : public GLWidgetPanZoomTool
{
    Q_OBJECT
public:
    GLWidgetCropRegionTool(RenderViewGLWidget* gl_widget)
        : GLWidgetPanZoomTool(gl_widget)
    {
    }
    virtual ~GLWidgetCropRegionTool() {};

    void init_action() override;
    void mouse_press(QMouseEvent* mouse_event) override;
    void mouse_move(QMouseEvent* mouse_event) override;
    void mouse_release(QMouseEvent* mouse_event) override;

Q_SIGNALS:
    void region_update(bool display, int region_min_x, int region_max_x, int region_min_y, int region_max_y);

protected:
    bool m_start_crop = false;
    int m_xstart = 0;
    int m_ystart = 0;
};

OPENDCC_NAMESPACE_CLOSE
