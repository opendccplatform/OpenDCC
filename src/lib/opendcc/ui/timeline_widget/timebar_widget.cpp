// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "timebar_widget.h"
#include <iostream>
#include <cmath>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QGuiApplication>
#include <QDebug>
#include <QPainterPath>
#include <cmath>
// #include "opendcc/app/core/application.h"
#include "audio_decoder.h"
#include "opendcc/ui/color_theme/color_theme.h"
#include "opendcc/app/ui/application_ui.h"

OPENDCC_NAMESPACE_OPEN

namespace
{
    const double eps = 1e-5;
}

inline int zero_to_one(int v)
{
    return v == 0 ? 1 : v;
}

inline double zero_to_one(double v)
{
    if (std::abs(v) < eps)
        return 1;
    else
        return v;
}

TimeBarWidget::TimeBarWidget(CurrentTimeIndicator current_time_indicator, bool subdivisions, QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    m_current_time_indicator = current_time_indicator;
    m_subdivisions = subdivisions;

    m_height = 27;
    m_halfHeightFactor = 0.2f;
    m_drawnStep = 1;

    m_startTime = 1.0;
    m_endTime = 24.0;

    m_frameStep = 1.0;
    m_currentTime = 1.0;

    setMinimumSize(QSize(200, m_height));
    // setMaximumSize(QSize(100000, m_height));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fontHeight = 10;
    m_indent = 5;

    setFocusPolicy(Qt::StrongFocus);

    m_audio_decoder = new AudioDecoder(this);
    connect(m_audio_decoder, &AudioDecoder::finish_decoding, this, [this] {
        m_wave_cache = false;
        update();
    });
}

TimeBarWidget::~TimeBarWidget() {}

void TimeBarWidget::mousePressEvent(QMouseEvent *event)
{
    //     if (event->pos().x() >= 0 && event->pos().x() < width())
    //     {
    float timeValue = compute_time(event->pos().x());

    if (timeValue > m_endTime)
        timeValue = m_endTime;
    else if (timeValue < m_startTime)
        timeValue = m_startTime;

    // m_currentTime = timeValue;
    if (m_snap_time_mode)
        timeValue = int(timeValue);

    if (m_time_selection_left_drag || m_time_selection_center_drag || m_time_selection_right_drag)
    {
        double shift = compute_time(event->pos().x()) - compute_time(m_time_selection_drag_x);
        if (m_snap_time_mode)
        {
            shift = int(shift);
        }

        if (m_time_selection_left_drag)
        {
            const double temp_value = m_time_drag_start + shift;
            if (temp_value < m_time_selection_end)
            {
                m_time_selection_start = temp_value;
            }
        }
        else if (m_time_selection_center_drag)
        {
            m_time_selection_start = m_time_drag_start + shift;
            m_time_selection_end = m_time_drag_end + shift;
        }
        else if (m_time_selection_right_drag)
        {
            const double temp_value = m_time_drag_end + shift;
            if (temp_value > m_time_selection_start)
            {
                m_time_selection_end = temp_value;
            }
        }
        emit time_selection_move(m_time_selection_start, m_time_selection_end);
        repaint();
        return;
    }
    else if (m_time_selection && !m_time_selection_drag)
    {
        const int selection_start = time_to_x_pos(m_time_selection_start);
        const int selection_end = time_to_x_pos(m_time_selection_end);
        const int selection_center = (selection_end - selection_start) / 2 + selection_start;

        const int d = 20;

        auto setup_drag = [this, event]() {
            m_time_selection_drag_x = event->pos().x();
            m_time_drag_start = m_time_selection_start;
            m_time_drag_end = m_time_selection_end;
        };

        if (event->pos().x() <= (selection_center + d) && event->pos().x() >= (selection_center - d))
        {
            m_time_selection_center_drag = true;
            setup_drag();
            return;
        }
        else if (event->pos().x() <= (selection_start + d) && event->pos().x() >= (selection_start - d))
        {
            m_time_selection_left_drag = true;
            setup_drag();
            return;
        }
        else if (event->pos().x() <= (selection_end + d) && event->pos().x() >= (selection_end - d))
        {
            m_time_selection_right_drag = true;
            setup_drag();
            return;
        }
    }

    if (timeValue != m_currentTime)
    {
        m_currentTime = timeValue;
        emit current_time_changed(m_currentTime);
        emit time_drag(timeValue);
    }

    if ((event->modifiers() & Qt::ShiftModifier) || m_time_selection_drag)
    {
        if (!m_time_selection_drag)
        {
            m_time_drag_start = timeValue;
        }
        m_time_drag_end = timeValue;
        m_time_selection_drag = true;
        m_time_selection = true;
        if (m_time_drag_end >= m_time_drag_start)
        {
            m_time_selection_start = m_time_drag_start;
            m_time_selection_end = m_time_drag_end + 1.0;
        }
        else
        {
            m_time_selection_start = m_time_drag_end;
            m_time_selection_end = m_time_drag_start + 1.0;
        }

        emit time_selection_begin(m_time_selection_start, m_time_selection_end);
    }
    else
    {
        m_time_selection = false;
    }

    repaint();
    event->accept();
    //     }
    //     event->ignore();
}

void TimeBarWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mouse_hover && !QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) && event->buttons() != Qt::LeftButton &&
        !m_time_selection_drag)
    {
        double frame = compute_time(event->pos().x());
        if (m_snap_time_mode)
            frame = int(frame);
        QString time = get_time_string(frame, true);
        QToolTip::showText(event->globalPos(), time);
    }

    if (event->buttons() == Qt::LeftButton)
    {
        mousePressEvent(event);
    }
}

void TimeBarWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_time_selection_drag = false;
    if (m_time_selection_left_drag || m_time_selection_center_drag || m_time_selection_right_drag)
    {
        emit time_selection_end(m_time_selection_start, m_time_selection_end);
    }
    m_time_selection_left_drag = false;
    m_time_selection_center_drag = false;
    m_time_selection_right_drag = false;
}

void TimeBarWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        m_time_selection = true;
        m_time_selection_start = m_startTime;
        m_time_selection_end = m_endTime + 1.0;
        repaint();
    }
}

QString TimeBarWidget::get_time_string(double frame, bool add_prefix /*= false*/)
{
    QString result;
    switch (m_time_display)
    {
    case OPENDCC_NAMESPACE::TimeDisplay::Frames:
    {
        if (std::ceil(frame) == frame)
        {
            result = QString::asprintf("%d", int(frame));
        }
        else
        {
            result = QString::asprintf("%0.2f", frame);
        }

        if (add_prefix)
        {
            result = i18n("toolbars.timeline", "Frame: ") + result;
        }
    }
    break;
    case OPENDCC_NAMESPACE::TimeDisplay::Timecode:

        result += to_timecode(frame, m_fps);

        if (add_prefix)
        {
            result = "Timecode: " + result;
        }

        break;
    default:
        result = "Not implemented";
        break;
    }
    return result;
}

double TimeBarWidget::compute_time(int x)
{
    double factor = double(x - m_indent) / double(width() - 2 * m_indent);
    return m_startTime + factor * (m_endTime - m_startTime + 1);
}

int TimeBarWidget::time_to_x_pos(double time)
{
    double duration = zero_to_one(m_endTime - m_startTime) + 1;
    return ((width() - 2 * m_indent) * (time - m_startTime) / duration) + m_indent;
}

void TimeBarWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    const float margin = 0.0f;

    float duration = m_endTime - m_startTime;

    duration = zero_to_one(duration);

    float _width = width() - margin;
    float _top = 0;
    float _height = height();
    float _bottom = _height - 1;
    QColor backFont = palette().dark().color().darker(120);
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);

    auto color_theme = get_color_theme();

    if (color_theme == ColorTheme::DARK)
    {
        painter.fillRect(rect(), QColor::fromRgbF(0.16, 0.16, 0.16));
    }
    else
    {
        painter.fillRect(rect(), palette().light());
    }
    // sound
    if (m_audio_decoder->is_ready())
    {
        QPen sound_pen;
        QColor sound_color(100, 125, 102, 255);
        sound_pen.setColor(sound_color);
        painter.setPen(sound_pen);

        const int h = _height;

        if (!m_wave_cache)
        {
            m_audio_decoder->compute_wave(m_wave, width() - m_indent * 2, m_startTime, m_endTime, m_fps, m_sound_time);
            m_wave_cache = true;
        }

        for (int i = 0; i < width() - m_indent * 2; i++)
        {
            const int max_h = h * ((m_wave[i * 2] + 1.0) * 0.5);
            const int min_h = h * ((m_wave[i * 2 + 1] + 1.0) * 0.5);
            painter.drawLine(i + m_indent, min_h, i + m_indent, max_h);
        }
    }

    double time_pos_width = ((double)width() - 2 * m_indent) / (zero_to_one(m_endTime - m_startTime) + 1);
    time_pos_width = std::max(1.0, time_pos_width);

    // double dt = std::max(1.0, std::ceil(25 / time_pos_width));

    double time_width = (double)width() - 2 * m_indent;

    QFontMetrics fm(font());
    int left_width = fm.horizontalAdvance(get_time_string(m_startTime));
    int right_width = fm.horizontalAdvance(get_time_string(m_endTime));
    double font_width = (double)std::max(left_width, right_width) * 2.0 + 20.0;

    double num_sectors = time_width / font_width;
    double dt = (zero_to_one(m_endTime - m_startTime) + 1) / num_sectors;

    double width_power = std::max(1.0, pow(10, floor(log10(dt))));
    dt = dt / width_power;
    int subdivisions = 0;

    if (dt > 5)
    {
        dt = 5;
        if (m_subdivisions)
            subdivisions = 5;
    }
    else if (dt > 2)
    {
        dt = 2;
        if (m_subdivisions)
            subdivisions = 2;
    }
    else
        dt = 1;

    dt = dt * width_power;

    if (m_subdivisions && subdivisions == 0)
    {
        if (dt >= 10.0)
            subdivisions = 5;
    }

    double new_startTime = ceil(m_startTime / dt) * dt;

    num_sectors = (m_endTime - new_startTime) / dt;

    // background
    switch (m_background_type)
    {
    case TimebarBackgroundType::Stripped:
    {
        for (int i = 0; i <= num_sectors; i += 2)
        {
            float pos1 = time_to_x_pos(i * dt + new_startTime);
            float pos2 = time_to_x_pos((i + 1) * dt + new_startTime);

            QColor stripe_color = QColor::fromRgb(0, 0, 0, 40);

            painter.fillRect(pos1, 0, pos2 - pos1, _bottom + 1, stripe_color);
        }
    }
    break;
    default:
        break;
    }

    for (int i = 0; i <= num_sectors; i++)
    {
        double t = i * dt + new_startTime;
        float pos = time_to_x_pos(t);
        painter.setPen(backFont);
        painter.drawLine(pos, _height / 2, pos, _bottom);

        QString text = get_time_string(int(t));
        painter.setPen(backFont);
        painter.drawText(pos + 3, m_fontHeight, text);
    }

    if (m_subdivisions)
    {
        painter.setPen(backFont);
        for (int i = -1; i <= num_sectors; i++)
        {
            double t1 = i * dt + new_startTime;
            for (int j = 1; j < subdivisions; j++)
            {
                double t2 = (i + 1) * dt + new_startTime;
                double t = t1 + (double(j) / double(subdivisions)) * (t2 - t1);
                float pos = time_to_x_pos(t);
                if (pos >= margin)
                {
                    painter.drawLine(pos, _height - _height / 4, pos, _bottom);
                }
            }
        }
    }

    if (m_time_selection)
    {
        QPen time_selection_pen;
        time_selection_pen.setWidth(1);
        QColor time_selection_color = palette().highlight().color();
        time_selection_color.setAlphaF(0.4);
        time_selection_pen.setColor(time_selection_color);
        painter.setPen(time_selection_pen);
        painter.setBrush(time_selection_color);
        const int start = time_to_x_pos(m_time_selection_start);
        const int end = time_to_x_pos(m_time_selection_end);
        painter.fillRect(start, 0, end - start, _bottom + 1, time_selection_color);

        QPen arrows_pen;
        arrows_pen.setWidth(1);
        QColor arrow_color(255, 255, 255, 80);
        arrows_pen.setColor(arrow_color);
        painter.setPen(arrows_pen);
        painter.setBrush(arrow_color);

        auto draw_arrow = [this, &painter](double pos, float length) {
            const int h = height();
            const float bottom = h - 2;
            const float top = h / 2 + 4;
            const auto p1 = QPointF(0.0f + pos, bottom);
            const auto p2 = QPointF(length + pos, top + (bottom - top) / 2.0);
            const auto p3 = QPointF(0.0f + pos, top);

            const QPointF points[3] = { p1, p2, p3 };

            painter.drawPolygon(points, 3);
        };

        const int middle = start + (end - start) / 2.0;
        const float length = 5.0f;
        draw_arrow(middle + 4, length);
        draw_arrow(middle - 4, -length);
        draw_arrow(start - 4, -length);
        draw_arrow(end + 4, length);

        QString start_text = get_time_string(m_time_selection_start);
        QString end_text = get_time_string(m_time_selection_end);

        if (color_theme == ColorTheme::DARK)
        {
            painter.setPen(Qt::white);
        }
        else
        {
            painter.setPen(palette().windowText().color());
        }
        painter.drawText(time_to_x_pos(m_time_selection_start) + 3, _height - 4, start_text);
        painter.drawText(time_to_x_pos(m_time_selection_end) + 3, _height - 4, end_text);
    }

    if (m_keyframes)
    {
        QColor timesamples_color =
            m_keyframe_draw_mode == KeyframeDrawMode::AnimationCurves ? QColor::fromRgb(255, 20, 10) : QColor::fromRgb(10, 166, 233);
        QColor timesamples_color_selected = QColor::fromRgb(255, 255, 255);

        if (m_keyframe_type == KeyframeDisplayType::Rect)
        {
            timesamples_color.setAlphaF(0.7);
        }

        QColor timesamples_color_rect = timesamples_color;
        QColor timesamples_color_selected_rect = timesamples_color_selected;

        switch (m_keyframe_type)
        {
        case KeyframeDisplayType::Rect:
            timesamples_color_rect.setAlphaF(0.3);
            timesamples_color_selected_rect.setAlphaF(0.3);

            break;
        case KeyframeDisplayType::Arrow:

            timesamples_color_rect.setAlphaF(0.6);
            timesamples_color_selected_rect.setAlphaF(0.6);

            break;
        default:
            break;
        }

        for (float keyframe : *m_keyframes)
        {
            if (keyframe >= m_startTime && keyframe <= m_endTime)
            {
                painter.setPen(timesamples_color);
                painter.setBrush(timesamples_color_rect);

                bool keyframe_selected = false;
                if (m_keyframe_draw_mode == KeyframeDrawMode::AnimationCurves)
                {
                    if (m_time_selection)
                    {
                        if (keyframe >= m_time_selection_start && keyframe < m_time_selection_end)
                        {
                            painter.setPen(timesamples_color_selected);
                            painter.setBrush(timesamples_color_selected_rect);
                        }
                        else
                        {
                            painter.setPen(timesamples_color);
                            painter.setBrush(timesamples_color_rect);
                        }
                    }
                }

                switch (m_keyframe_type)
                {
                case KeyframeDisplayType::Line:
                {
                    float p = margin + time_to_x_pos(keyframe);
                    painter.drawLine(p, 0, p, _height);
                    break;
                }
                case KeyframeDisplayType::Rect:
                {
                    float pos1 = margin + time_to_x_pos(keyframe);
                    float pos2 = margin + time_to_x_pos(keyframe + 1);
                    painter.drawRect(pos1, 1, pos2 - pos1, _height - 2);
                    break;
                }
                case KeyframeDisplayType::Arrow:
                {
                    float arrow_width = 3;
                    float p_center = margin + time_to_x_pos(keyframe);
                    float p_right = p_center + arrow_width;
                    float p_left = p_center - arrow_width;

                    float arrow_dist = 4;

                    QPainterPath arrow_path;
                    arrow_path.moveTo(p_center, _height);
                    arrow_path.lineTo(p_right, _height - arrow_dist);
                    arrow_path.lineTo(p_right, 0);
                    arrow_path.lineTo(p_left, 0);
                    arrow_path.lineTo(p_left, _height - arrow_dist);
                    arrow_path.lineTo(p_center, _height);

                    painter.drawPath(arrow_path);

                    break;
                }
                }
            }
        }
    }

    /*
    const int32_t first_frame_indent = 6;

    const int32_t frame_count = 12;

    const float frame_seg_length = float(width()) / float((frame_count + 1));
    const float draw_height = _height - 2;
    */

    // pen.setWidth(2);

    // for (int32_t current_frame = int32_t(m_startTime + eps); current_frame < int32_t(m_endTime + eps); current_frame++)
    //{
    //    auto it = std::find_if(m_cached_frames.begin(), m_cached_frames.end(), [&](const TimelineFrame& frame_data)
    //    {
    //        return current_frame == frame_data.frame;
    //    });
    //    if (it != m_cached_frames.end())
    //    {
    //        if (it->status == TimelineFrameStatus::Cached)
    //        {
    //            pen.setColor(Qt::green);
    //        }
    //        else if (it->status == TimelineFrameStatus::InProgress)
    //        {
    //            pen.setColor(QColor(255, 201, 14));//orange
    //        }
    //        else if (it->status == TimelineFrameStatus::OutOfDate)
    //        {
    //            pen.setColor(Qt::red);
    //        }
    //        else
    //        {
    //            pen.setColor(QColor(255, 0, 255));
    //        }

    //        painter.setPen(pen);

    //        const int32_t frame_seg_start = time_to_x_pos(current_frame);

    //        const int32_t frame_seg_end = time_to_x_pos(current_frame + 1);
    //        painter.drawLine(QPointF(frame_seg_start, draw_height), QPointF(frame_seg_end, draw_height));

    //    }
    //}

    switch (m_current_time_indicator)
    {
    case CurrentTimeIndicator::Default:
    {
        QPen time_pen;
        time_pen.setWidth(1);
        QColor timeColor = palette().highlight().color();

        timeColor.setAlphaF(0.4);
        time_pen.setColor(timeColor);
        painter.setPen(time_pen);
        timeColor.setAlphaF(0.5);
        painter.setBrush(timeColor);
        float timePos = time_to_x_pos(m_currentTime);

        float indicator_width = std::max(2.0, time_pos_width);
        painter.fillRect(timePos, 0, indicator_width, _bottom + 1, timeColor);

        QPen pen;
        pen.setWidth(1);
        pen.setColor(Qt::white);
        painter.setPen(pen);

        if (!m_time_selection)
        {
            QString text = get_time_string(m_currentTime);

            QFontMetrics fm(font());
            const int font_width = fm.horizontalAdvance(text) + 6;

            if (font_width - indicator_width > 4)
            {
                painter.fillRect(timePos, 0, font_width, _bottom + 1, timeColor);
            }

            if (color_theme == ColorTheme::DARK)
            {
                painter.setPen(Qt::white);
            }
            else
            {
                painter.setPen(palette().windowText().color());
            }
            painter.drawText(time_to_x_pos(m_currentTime) + 3, _height - 4, text);
        }
    }
    break;
    case CurrentTimeIndicator::Arrow:
    {
        QString text = get_time_string(m_currentTime);
        auto bold_font = font();
        bold_font.setBold(true);
        QFontMetrics fm_bold(bold_font);

        const int font_width = fm_bold.horizontalAdvance(text);
        const double font_width_half = double(font_width) / 2.0;

        // const QColor arrow_color(245, 255, 82); // yellow
        const QColor arrow_color(76, 161, 255); // blue
        painter.setPen(arrow_color);

        float timePos = time_to_x_pos(m_currentTime);

        const double arrow_padding = 3.0;

        const double arrow_left = timePos - font_width_half - arrow_padding - 0.5;
        const double arrow_right = timePos + font_width_half + arrow_padding + 0.5;

        QPainterPath arrow_path;
        arrow_path.moveTo(timePos + 0.5, _height - _height / 4);
        arrow_path.lineTo(arrow_right, _height / 2);
        arrow_path.lineTo(arrow_right, 0);
        arrow_path.lineTo(arrow_left, 0);
        arrow_path.lineTo(arrow_left, _height / 2);
        arrow_path.lineTo(timePos + 0.5, _height - _height / 4);

        painter.fillPath(arrow_path, arrow_color);
        painter.fillRect(timePos, _height / 2 + 1, 1, _height / 2, arrow_color);
        // painter.fillRect(timePos - 7, _height - 2, 14, 2, arrow_color);

        painter.setPen(Qt::black);
        painter.setFont(bold_font);
        painter.drawText(timePos - font_width_half, _height / 2 - 3, text);
    }
    break;
    default:
        break;
    }

    painter.end();
}

QSize TimeBarWidget::sizeHint() const
{
    return QSize(100, m_height);
}

void TimeBarWidget::set_current_time(double time)
{
    if (time != m_currentTime)
    {
        m_currentTime = time;
        repaint();
        emit current_time_changed(time);
    }
}

void TimeBarWidget::set_start_time(const double start_time)
{
    m_startTime = start_time;
    m_wave_cache = false;
    repaint();
}

void TimeBarWidget::set_end_time(const double end_time)
{
    m_endTime = end_time;
    m_wave_cache = false;
    repaint();
}

void TimeBarWidget::set_keyframes(KeyFrameSet &keyframes)
{
    m_keyframes = std::make_shared<KeyFrameSet>(keyframes);
    repaint();
}

KeyFrameSetPtr TimeBarWidget::get_keyframes()
{
    return m_keyframes;
}

void TimeBarWidget::set_sound(const std::string &filepath, const double time)
{
    m_audio_decoder->set_source_filename(filepath.c_str());
    m_sound_time = time;
    m_wave_cache = false;
}

void TimeBarWidget::clear_sound()
{
    m_audio_decoder->clear();
    update();
}

void TimeBarWidget::set_time_display(TimeDisplay mode)
{
    m_time_display = mode;
}

void TimeBarWidget::set_current_time_indicator_type(CurrentTimeIndicator cursor)
{
    m_current_time_indicator = cursor;
    update();
}

void TimeBarWidget::set_keyframe_display_type(KeyframeDisplayType type)
{
    m_keyframe_type = type;
    update();
}

void TimeBarWidget::set_background_type(TimebarBackgroundType type)
{
    m_background_type = type;
    update();
}

void TimeBarWidget::set_subdivisions(bool subdivisions)
{
    m_subdivisions = subdivisions;
    update();
}

void TimeBarWidget::resizeEvent(QResizeEvent *event)
{
    m_wave_cache = false;
    return QWidget::resizeEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void TimeBarWidget::enterEvent(QEnterEvent *event)
#else
void TimeBarWidget::enterEvent(QEvent *event)
#endif
{
    m_mouse_hover = true;
    QWidget::enterEvent(event);
}

void TimeBarWidget::leaveEvent(QEvent *event)
{
    m_mouse_hover = false;
    QWidget::leaveEvent(event);
}

OPENDCC_NAMESPACE_CLOSE
