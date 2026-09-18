// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <QBoxLayout>
#include <QPainter>
#include <QMouseEvent>

#include "opendcc/ui/common_widgets/tiny_slider.h"
#include "opendcc/ui/common_widgets/qt_compat.h"
#include "opendcc/ui/common_widgets/canvas_widget.h"
#include "opendcc/ui/common_widgets/ramp.h"

OPENDCC_NAMESPACE_OPEN

HTinySlider::HTinySlider(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(10, 3);
    setContentsMargins(0, 0, 0, 0);
    auto layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    auto slider_canvas = new CanvasWidget(this);
    layout->addWidget(slider_canvas);

    slider_canvas->paint_event = [this, slider_canvas](QPaintEvent*) {
        auto pos = (m_value - m_min) / (m_max - m_min) * width();

        QPainter painter(slider_canvas);

        QPen pen;
        pen.setStyle(Qt::PenStyle::NoPen);
        painter.setPen(pen);

        QBrush brush;
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        brush.setColor(m_slider_color);
        painter.setBrush(brush);
        painter.drawRect(0, 0, width(), height());

        double h, s, v;
        get_hsv_f(m_slider_color, &h, &s, &v);

        brush.setColor(QColor::fromHsvF(h, s, v * 0.5));
        painter.setBrush(brush);
        painter.drawRect(0, 0, pos, height());

        brush.setColor({ 255, 255, 255 });
        painter.setBrush(brush);

        painter.drawRect(pos - 3, 0, 6, height());
    };

    slider_canvas->mouse_press_event = [this, slider_canvas](QMouseEvent* e) {
        m_pressed = true;
        start_changing(m_value);
        m_value = m_min + clamp(e->localPos().x() / width(), 0., 1.) * (m_max - m_min);
        slider_canvas->update();
    };

    slider_canvas->mouse_move_event = [this, slider_canvas](QMouseEvent* e) {
        if (m_pressed)
            m_value = m_min + clamp(e->localPos().x() / width(), 0., 1.) * (m_max - m_min);
        changing(m_value);
        slider_canvas->update();
    };

    slider_canvas->mouse_release_event = [this, slider_canvas](QMouseEvent* e) {
        m_value = m_min + clamp(e->localPos().x() / width(), 0., 1.) * (m_max - m_min);
        m_pressed = false;
        changing(m_value);
        end_changing(m_value);
        slider_canvas->update();
    };
}

void HTinySlider::max(double val)
{
    m_max = val;
    update();
}

double HTinySlider::max() const
{
    return m_max;
}

void HTinySlider::min(double val)
{
    m_min = val;
    update();
}

double HTinySlider::min() const
{
    return m_min;
}

void HTinySlider::value(double val)
{
    m_value = val;
    update();
}

double HTinySlider::value() const
{
    return m_value;
}

void HTinySlider::slider_color(QColor val)
{
    m_slider_color = val;
    update();
}

QColor HTinySlider::slider_color() const
{
    return m_slider_color;
}

VTinySlider::VTinySlider(QWidget* parent /*= 0*/)
{
    setMinimumSize(3, 10);
    setContentsMargins(0, 0, 0, 0);
    auto layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    auto slider_canvas = new CanvasWidget(this);
    layout->addWidget(slider_canvas);

    slider_canvas->paint_event = [this, slider_canvas](QPaintEvent*) {
        auto pos = (m_value - m_min) / (m_max - m_min) * height();

        QPainter painter(slider_canvas);

        QPen pen;
        pen.setStyle(Qt::PenStyle::NoPen);
        painter.setPen(pen);

        QBrush brush;
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        brush.setColor(m_slider_color);
        painter.setBrush(brush);
        painter.drawRect(0, 0, width(), height());

        double h, s, v;
        get_hsv_f(m_slider_color, &h, &s, &v);

        brush.setColor(QColor::fromHsvF(h, s, v * 0.5));
        painter.setBrush(brush);
        painter.drawRect(0, 0, width(), pos);

        brush.setColor({ 255, 255, 255 });
        painter.setBrush(brush);
        painter.drawRect(0, pos - 3, width(), 6);
    };

    slider_canvas->mouse_press_event = [this, slider_canvas](QMouseEvent* e) {
        m_pressed = true;
        start_changing(m_value);
        m_value = m_min + clamp(e->localPos().y() / height(), 0., 1.) * (m_max - m_min);
        slider_canvas->update();
    };

    slider_canvas->mouse_move_event = [this, slider_canvas](QMouseEvent* e) {
        if (m_pressed)
            m_value = m_min + clamp(e->localPos().y() / height(), 0., 1.) * (m_max - m_min);
        changing(m_value);
        slider_canvas->update();
    };

    slider_canvas->mouse_release_event = [this, slider_canvas](QMouseEvent* e) {
        m_value = m_min + clamp(e->localPos().y() / height(), 0., 1.) * (m_max - m_min);
        m_pressed = false;
        changing(m_value);
        end_changing(m_value);
        slider_canvas->update();
    };
}

QColor VTinySlider::slider_color() const
{
    return m_slider_color;
}

void VTinySlider::slider_color(QColor val)
{
    m_slider_color = val;
    update();
}

double VTinySlider::value() const
{
    return m_value;
}

void VTinySlider::value(double val)
{
    m_value = val;
    update();
}

double VTinySlider::min() const
{
    return m_min;
}

void VTinySlider::min(double val)
{
    m_min = val;
    update();
}

double VTinySlider::max() const
{
    return m_max;
}

void VTinySlider::max(double val)
{
    m_max = val;
    update();
}

OPENDCC_NAMESPACE_CLOSE
