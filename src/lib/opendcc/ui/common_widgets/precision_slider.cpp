// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <QPainter>
#include <QBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QStyleOption>
#include <QPalette>
#include "opendcc/ui/common_widgets/precision_slider.h"
#include <QWheelEvent>
#include <cmath>

OPENDCC_NAMESPACE_OPEN

PrecisionSlider::PrecisionSlider(QWidget* parent /*= nullptr*/)
    : QWidget(parent)
{
    setMinimumSize(85, 25);

    static const QSize handle_size(3, 10);
    m_handle = new Handle(handle_size);
    update_slider_rect();
    update_ticks();
}

PrecisionSlider::~PrecisionSlider()
{
    delete m_handle;
    for (auto const& tick : m_ticks)
        delete tick;
}

void PrecisionSlider::set_minimum(double min)
{
    set_range(min, m_max);
}

double PrecisionSlider::get_minimum() const
{
    return m_min;
}

void PrecisionSlider::set_maximum(double max)
{
    set_range(m_min, max);
}

double PrecisionSlider::get_maximum() const
{
    return m_max;
}

void PrecisionSlider::set_range(double min, double max)
{
    if (m_integer_slider)
    {
        min = std::round(min);
        max = std::round(max);
    }
    if (min > max)
        std::swap(min, max);

    const auto old_value = get_value();
    m_min = min;
    m_max = max;

    if (old_value >= m_max)
        set_slider_position(maximum());
    else if (old_value <= m_min)
        set_slider_position(minimum());
    else
        set_slider_position(convert_to_slider_position(old_value));

    update_ticks();
    update();
}

void PrecisionSlider::set_value(double value)
{
    if (m_integer_slider)
        value = std::round(value);
    if (value > m_max)
    {
        if (2 * value - m_min > m_autoscale_max)
        {
            m_max = m_autoscale_max;
            set_slider_position(convert_to_slider_position(value));
        }
        else
        {
            m_max = 2 * value - m_min;
            set_slider_position((maximum() - minimum()) / 2);
        }
    }
    else if (value < m_min)
    {
        set_slider_position((maximum() - minimum()) / 2);
        if (2 * value - m_max > m_autoscale_min)
        {
            m_min = m_autoscale_min;
            set_slider_position(convert_to_slider_position(value));
        }
        else
        {
            m_min = 2 * value - m_max;
            set_slider_position((maximum() - minimum()) / 2);
        }
    }
    else
    {
        set_slider_position(convert_to_slider_position(value));
    }
    m_val = value;
    value_changed(value);
    update_ticks();
    update();
}

double PrecisionSlider::get_value() const
{
    return m_val;
}

double PrecisionSlider::calc_value(int in_position) const
{
    return m_min + static_cast<double>(in_position - minimum()) * (m_max - m_min) / (maximum() - minimum());
}

void PrecisionSlider::set_integer_slider(bool is_integer)
{
    m_integer_slider = is_integer;
    update_ticks();
    update();
}

void PrecisionSlider::set_autoscale_limits(double min, double max)
{
    m_autoscale_max = min;
    m_autoscale_max = max;
}

int PrecisionSlider::minimum() const
{
    return m_scale.left();
}

int PrecisionSlider::maximum() const
{
    return m_scale.right();
}

int PrecisionSlider::slider_position() const
{
    return m_handle->center_pos();
}

void PrecisionSlider::set_slider_position(int pos)
{
    m_handle->move_center(pos);
    update();
}

void PrecisionSlider::set_slider_down(bool is_down)
{
    m_slider_down = is_down;
    if (m_slider_down)
        slider_pressed();
    else
        slider_released();
}

void PrecisionSlider::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update_slider_rect();
    set_value(m_val);
    set_slider_down(false);
}

void PrecisionSlider::wheelEvent(QWheelEvent* e)
{
    e->ignore();
}

void PrecisionSlider::mouseMoveEvent(QMouseEvent* ev)
{
    QWidget::mouseMoveEvent(ev);
    if (m_slider_down)
        slider_mouse_change(ev->pos().x());
}

void PrecisionSlider::mousePressEvent(QMouseEvent* ev)
{
    QWidget::mousePressEvent(ev);
    set_slider_down(true);

    slider_mouse_change(ev->pos().x());
}

void PrecisionSlider::mouseReleaseEvent(QMouseEvent* ev)
{
    QWidget::mouseReleaseEvent(ev);
    if (m_slider_down)
        set_slider_down(false);
}

void PrecisionSlider::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    QPen pen;
    pen.setColor(palette().windowText().color());
    pen.setWidth(1);

    QPainter painter(this);
    painter.setPen(pen);

    QLine groove(m_scale.left(), m_scale.center().y(), m_scale.width(), m_scale.center().y());
    painter.drawLine(groove);

    static const int m_font_size = 6;
    QFont painter_font = painter.font();
    painter_font.setPointSize(m_font_size);
    painter.setFont(painter_font);
    QFontMetrics const font_metrics(painter_font);

    // drawing ticks
    for (const auto& tick : m_ticks)
    {
        const int current_x = tick->m_position;
        const int str_width = font_metrics.horizontalAdvance(tick->m_str);
        const int text_x = tick->m_value == m_min   ? current_x // first
                           : tick->m_value == m_max ? current_x - str_width // last
                                                    : current_x - str_width / 2; // others

        painter.drawLine(current_x, m_scale.top(), current_x, m_scale.bottom());
        painter.drawText(text_x, m_scale.bottom() + font_metrics.height(), tick->m_str);
    }

    // drawing handle
    pen.setColor(palette().light().color());
    painter.setPen(pen);
    painter.drawRect(m_handle->rect().adjusted(-1, -1, 0, 0));
    painter.fillRect(m_handle->rect(), palette().text().color());
}

int PrecisionSlider::convert_to_slider_position(double value) const
{
    return minimum() + static_cast<int>(std::floor((value - m_min) * (maximum() - minimum()) / (m_max - m_min)));
}

void PrecisionSlider::update_ticks()
{
    static const int comfort_gap = 10; // between ticks

    const QFontMetrics fm(font());
    const auto left_width = fm.horizontalAdvance(get_value_string(m_min, false));
    const auto right_width = fm.horizontalAdvance(get_value_string(m_max, false));
    const double font_width = std::max(left_width, right_width) * 1.5 + comfort_gap;

    double num_sectors = m_scale.width() / font_width;
    auto scale_step = (m_max - m_min) / num_sectors;

    const double width_power = std::pow(10, std::floor(std::log10(scale_step)));
    scale_step /= width_power;
    int subdivisions = 0;

    if (scale_step > 5)
    {
        scale_step = 5;
        subdivisions = 5;
    }
    else if (scale_step > 2)
    {
        scale_step = 2;
        subdivisions = 2;
    }
    else
    {
        scale_step = 1;
        subdivisions = 10;
    }

    scale_step = scale_step * width_power;

    if (subdivisions == 0)
    {
        if (scale_step >= 10.0)
            subdivisions = 5;
    }

    if (m_integer_slider)
    {
        scale_step = std::round(scale_step);
        scale_step = scale_step ? scale_step : 1;
    }

    const auto new_min_val = std::ceil(m_min / scale_step) * scale_step;
    num_sectors = (m_max - new_min_val) / scale_step;

    while (m_scale.width() / (num_sectors * subdivisions) < comfort_gap)
    {
        subdivisions = subdivisions == 0 ? 5 : (subdivisions / 2);
    }

    for (const auto& tick : m_ticks)
        delete tick;
    m_ticks.clear();
    m_ticks.shrink_to_fit();
    num_sectors = std::trunc(num_sectors);

    // main ticks
    m_ticks.reserve((size_t)num_sectors + 1);
    for (int i = 0; i < num_sectors + 1; ++i)
    {
        auto val = i * scale_step + m_min;
        auto pos = convert_to_slider_position(val);
        auto text = get_value_string(val);

        m_ticks.emplace_back(new Tick(val, pos, text));
    }
    auto index_last_tick = m_ticks.size() - 1;

    // check if the last tick (m_max) is missing
    if (m_ticks.back()->m_value < m_max)
    {
        auto text = get_value_string(m_max);
        auto pos = m_scale.right();
        auto last_tick = new Tick(m_max, pos, text);

        auto distance = pos - m_ticks.back()->m_position;
        if (distance < font_width) // doesn't fit
            m_ticks.back()->m_str.clear();

        m_ticks.push_back(last_tick);
        ++index_last_tick;
    }

    // subdivisions
    if (!m_integer_slider && subdivisions > 0)
    {
        m_ticks.reserve(m_ticks.size() + (num_sectors + 2) * (subdivisions - 1));
        for (int i = -1; i < num_sectors + 1; ++i)
        {
            auto val1 = i * scale_step + m_min;
            for (int j = 1; j < subdivisions; j++)
            {
                auto val2 = (i + 1) * scale_step + m_min;
                auto value = val1 + ((double)j / subdivisions) * (val2 - val1);
                auto pos = convert_to_slider_position(value);

                if (m_ticks.at(index_last_tick)->m_position - pos > comfort_gap)
                    m_ticks.emplace_back(new Tick(value, pos, ""));
            }
        }
    }

    update();
}

int PrecisionSlider::closest_tick(int pos)
{
    for (int i = 0; i < m_ticks.size(); ++i)
    {
        if (m_ticks[i]->is_snap(pos))
            return i;
    }
    return -1;
}

void PrecisionSlider::slider_mouse_change(int pos)
{
    if (pos > maximum())
        pos = maximum();
    else if (pos < minimum())
        pos = minimum();

    if (m_integer_slider)
    {
        set_value(calc_value(pos));
    }
    else
    {
        int const tick_number = closest_tick(pos);
        if (tick_number >= 0) // found
        {
            set_value(m_ticks.at(tick_number)->m_value); // set an exact value
            pos = m_ticks.at(tick_number)->m_position; // get an exact position
            set_slider_position(pos); // ?
        }
        else
        {
            m_val = calc_value(pos); //!
            set_slider_position(pos);
        }
    }
    slider_moved();
    update();
}

void PrecisionSlider::update_slider_rect()
{
    QRect widget_rect = rect();
    const auto side_margin = m_handle->rect().width() / 2 + 1; // to fit the handle into the widget borders
    widget_rect.adjust(side_margin, 0, -side_margin, 0);

    static const int tick_length = 5;
    m_scale.setRect(widget_rect.left(), widget_rect.height() / 2 - tick_length / 2, widget_rect.width(), tick_length);
    m_handle->set_center(
        QPoint(slider_position(), m_scale.center().y() - m_handle->get_size().height() * 0.25)); // 1/4 is below the scale, 3/4 - above
    update();
}

QString PrecisionSlider::get_value_string(double value, bool beautify /* = true*/)
{
    QString result;
    if (m_integer_slider)
        result = QString::number(value, 'f', 0);
    else
    {
        if (beautify)
            result = QString::number(value, 'g', int(std::log10(1 + std::abs(value)) + 3));
        else
            result = QString::asprintf("%0.3f", value);
    }
    return result;
}

PrecisionSlider::Handle::Handle(QSize const& size)
{
    m_rect.setSize(size);
}

void PrecisionSlider::Handle::set_center(QPoint const& pos)
{
    m_rect.moveCenter(pos);
}

void PrecisionSlider::Handle::move_center(int const pos)
{
    m_rect.moveCenter(QPoint(pos, m_rect.center().y()));
}

int PrecisionSlider::Handle::center_pos() const
{
    return m_rect.center().x();
}

QSize PrecisionSlider::Handle::get_size() const
{
    return m_rect.size();
}

QRect PrecisionSlider::Handle::rect() const
{
    return m_rect;
}

PrecisionSlider::Tick::Tick(double val, int pos, QString str)
    : m_value(val)
    , m_position(pos)
    , m_str(str)
{
}

bool PrecisionSlider::Tick::is_snap(int pos) const
{
    static const int s_tick_snap_px = 3;
    return std::abs(m_position - pos) < s_tick_snap_px;
}

OPENDCC_NAMESPACE_CLOSE
