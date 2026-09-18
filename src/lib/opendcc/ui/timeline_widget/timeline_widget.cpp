// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "timeline_widget.h"
#include "timebar_widget.h"
#include "frames_per_second_widget.h"
#include "time_widget.h"
#include <QtCore/QTimer>
#include <QtCore/QTimeLine>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QMenu>
#include <QShortcut>
#include <iostream>
#include <cmath>
// #include "opendcc/app/core/application.h"
#include <QMediaPlayer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#endif
#include "opendcc/app/ui/application_ui.h"

#ifdef USE_ANIM_ENGINE
#include "opendcc/anim/engine/engine.cpp"
#include "anim_engene_session.cpp"
#endif

OPENDCC_NAMESPACE_OPEN

namespace
{
    const double eps = 1e-5;
}

QPushButton* add_button(QHBoxLayout* layout, const QString& name, const QString& tooltip)
{
    QPushButton* btn = new QPushButton(name);
    btn->setFixedSize(24, 24);
    btn->setIconSize(QSize(20, 20));
    layout->addWidget(btn);
    btn->setFlat(true);
    QFont font = btn->font();
    font.setBold(true);
    btn->setFont(font);
    btn->setToolTip(tooltip);
    return btn;
}

TimelineWidget::TimelineWidget(TimelineLayout timeline_layout, CurrentTimeIndicator current_time_indicator, bool subdivisions, QWidget* parent)
{
    m_timeline_layout = timeline_layout;
    QLayout* layout;

    switch (m_timeline_layout)
    {
    case OPENDCC_NAMESPACE::TimelineLayout::Default:
        layout = new QHBoxLayout;
        break;
    case OPENDCC_NAMESPACE::TimelineLayout::Player:
        layout = new QVBoxLayout;
        break;
    }
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* first_row;
    QHBoxLayout* second_row;

    switch (m_timeline_layout)
    {
    case OPENDCC_NAMESPACE::TimelineLayout::Default:
        first_row = qobject_cast<QHBoxLayout*>(layout);
        second_row = qobject_cast<QHBoxLayout*>(layout);
        break;
    case OPENDCC_NAMESPACE::TimelineLayout::Player:
        first_row = new QHBoxLayout;
        second_row = new QHBoxLayout;
        first_row->setContentsMargins(0, 0, 0, 0);
        second_row->setContentsMargins(0, 0, 0, 0);
        QVBoxLayout* main_layout = qobject_cast<QVBoxLayout*>(layout);
        main_layout->addLayout(first_row);
        main_layout->addLayout(second_row);
        break;
    }

    setLayout(layout);

    m_timebar = new TimeBarWidget(current_time_indicator, subdivisions, this);

    m_timer = new QTimer(this);
    m_timeline = new QTimeLine(1000, this);
    // setCurveShape() was removed in Qt6; setEasingCurve below is the equivalent and works in Qt5 too
    m_timeline->setEasingCurve(QEasingCurve::Linear);

    auto init_time_widget = [&](double init_time, std::function<void(double)> value_setter_fn) -> TimeWidget* {
        auto widget = new TimeWidget(m_time_display);
        widget->setValue(init_time);

        QObject::connect(widget, &QDoubleSpinBox::editingFinished, [this, widget, value_setter_fn] {
            double time = widget->value();
            // if (m_timebar->is_snap_time_mode())
            widget->setValue(time);
            value_setter_fn(time);
        });
        return widget;
    };

    first_row->addWidget(m_timebar);

    auto add_buttons = [this](QHBoxLayout* layout) -> void {
        QPushButton* goto_start_btn = add_button(layout, "", i18n("toolbars.timeline", "Go to start of playback range"));
        connect(goto_start_btn, SIGNAL(clicked()), this, SLOT(go_to_start()));
        goto_start_btn->setIcon(QIcon(":icons/goto_start.png"));

        QPushButton* step_back_btn = add_button(layout, "", i18n("toolbars.timeline", "Step back one frame"));
        step_back_btn->setIcon(QIcon(":icons/step_back.png"));
        connect(step_back_btn, SIGNAL(clicked()), this, SLOT(step_back_one_frame()));

        QPushButton* step_back_key_btn = add_button(layout, "", i18n("toolbars.timeline", "Step back one key"));
        step_back_key_btn->setIcon(QIcon(":icons/step_back_key.png"));
        connect(step_back_key_btn, SIGNAL(clicked()), this, SLOT(step_back_one_key()));

        m_play_backward_btn = add_button(layout, "", i18n("toolbars.timeline", "Play backwards"));
        m_play_backward_btn->setIcon(QIcon(":icons/play_backward.png"));
        connect(m_play_backward_btn, SIGNAL(clicked()), this, SLOT(play_backwards()));

        m_play_forward_btn = add_button(layout, "", i18n("toolbars.timeline", "Play forwards"));
        m_play_forward_btn->setIcon(QIcon(":icons/play_forward.png"));
        connect(m_play_forward_btn, SIGNAL(clicked()), this, SLOT(play_forwards()));

        QPushButton* step_forward_key_btn = add_button(layout, "", i18n("toolbars.timeline", "Step forward one key"));
        step_forward_key_btn->setIcon(QIcon(":icons/step_forward_key.png"));
        connect(step_forward_key_btn, SIGNAL(clicked()), this, SLOT(step_forward_one_key()));

        QPushButton* step_forward_btn = add_button(layout, "", i18n("toolbars.timeline", "Step forward one frame"));
        step_forward_btn->setIcon(QIcon(":icons/step_forward.png"));
        connect(step_forward_btn, SIGNAL(clicked()), this, SLOT(step_forward_one_frame()));

        QPushButton* goto_end_btn = add_button(layout, "", i18n("toolbars.timeline", "Go to end of playback range"));
        goto_end_btn->setIcon(QIcon(":icons/goto_end.png"));
        connect(goto_end_btn, SIGNAL(clicked()), this, SLOT(go_to_end()));
    };

    m_time_display_label = new QLabel(i18n("toolbars.timeline", "Frame:"));

    if (m_timeline_layout == TimelineLayout::Default)
    {
        second_row->addSpacing(12);
    }
    else
    {
        int label_width = m_time_display_label->sizeHint().width();
        second_row->addSpacing(label_width);
        second_row->addStretch();
        add_buttons(second_row);
        second_row->addStretch();
    }

    second_row->addWidget(m_time_display_label);

    m_current_time_edit = init_time_widget(m_timebar->current_time(), [&](double time) { m_timebar->set_current_time(time); });
    second_row->addWidget(m_current_time_edit);

    if (m_timeline_layout == TimelineLayout::Default)
    {
        add_buttons(second_row);
    }

    m_step_forward_one_key_act = new QAction(i18n("toolbars.timeline", "Step Forward One Key"), this);
    connect(m_step_forward_one_key_act, SIGNAL(triggered()), this, SLOT(step_forward_one_key()));
    m_step_forward_one_key_act->setShortcut(QKeySequence("."));
    addAction(m_step_forward_one_key_act);

    m_step_back_one_key_act = new QAction(i18n("toolbars.timeline", "Step Back One Key"), this);
    connect(m_step_back_one_key_act, SIGNAL(triggered()), this, SLOT(step_back_one_key()));
    m_step_back_one_key_act->setShortcut(QKeySequence(","));
    addAction(m_step_back_one_key_act);

    connect(m_timebar, SIGNAL(current_time_changed(double)), this, SLOT(update_time_edit(double)));
    connect(m_timebar, SIGNAL(current_time_changed(double)), this, SIGNAL(current_time_changed(double)));

    m_context_menu = new QMenu(this);

    auto* show_timesamples_mode = new QAction(i18n("toolbars.timeline", "Show Timesamples"));
    show_timesamples_mode->setCheckable(true);
    show_timesamples_mode->setChecked(m_timebar->get_keyframe_draw_mode() == KeyframeDrawMode::Timesamples);
    connect(show_timesamples_mode, &QAction::triggered, [&]() {
        m_timebar->set_keyframe_draw_mode(KeyframeDrawMode::Timesamples);
        emit keyframe_draw_mode_changed();
    });

    auto* show_animation_keys_mode = new QAction(i18n("toolbars.timeline", "Show Animation Keyframes"));
    show_animation_keys_mode->setCheckable(true);
    show_animation_keys_mode->setChecked(m_timebar->get_keyframe_draw_mode() == KeyframeDrawMode::AnimationCurves);
    connect(show_animation_keys_mode, &QAction::triggered, [&]() {
        m_timebar->set_keyframe_draw_mode(KeyframeDrawMode::AnimationCurves);
        emit keyframe_draw_mode_changed();
    });

    auto* mode_group = new QActionGroup(this);
    mode_group->addAction(show_timesamples_mode);
    mode_group->addAction(show_animation_keys_mode);

    m_context_menu->addAction(show_timesamples_mode);
    m_context_menu->addAction(show_animation_keys_mode);

    auto time_display_menu = new QMenu(i18n("toolbars.timeline", "Time Display"), this);

    auto* time_display_frames = new QAction(i18n("toolbars.timeline", "Frames"));
    time_display_frames->setCheckable(true);
    time_display_frames->setChecked(true);
    connect(time_display_frames, &QAction::triggered, [this]() { set_time_display(TimeDisplay::Frames); });

    auto* time_display_timecode = new QAction(i18n("toolbars.timeline", "Timecode"));
    time_display_timecode->setCheckable(true);
    connect(time_display_timecode, &QAction::triggered, [this]() { set_time_display(TimeDisplay::Timecode); });

    auto* time_display_group = new QActionGroup(this);
    time_display_group->addAction(time_display_frames);
    time_display_group->addAction(time_display_timecode);

    time_display_menu->addAction(time_display_frames);
    time_display_menu->addAction(time_display_timecode);

    m_context_menu->addMenu(time_display_menu);

    m_player = new QMediaPlayer(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6 split playback from output: a QMediaPlayer without an attached QAudioOutput
    // decodes but stays silent. Qt5 routed to the default device implicitly.
    m_player->setAudioOutput(new QAudioOutput(m_player));
#endif

    connect(m_timebar, &TimeBarWidget::time_drag, this, &TimelineWidget::time_drag);
    m_drag_timer = new QTimer(this);
    m_drag_timer->setSingleShot(true);
    connect(m_drag_timer, &QTimer::timeout, m_player, &QMediaPlayer::stop);
}

TimelineWidget::~TimelineWidget() {}

void TimelineWidget::update_time_edit(double value)
{
    if (value != m_current_time_edit->value())
        m_current_time_edit->setValue(value);
}

void TimelineWidget::step_forward_one_frame()
{
    double shift = m_playback_mode == PlaybackMode::EveryFrame ? m_playback_by : 1.0;
    double new_time = m_timebar->current_time() + shift;
    // if (m_timebar->is_snap_time_mode())
    //     new_time = std::floor(new_time);
    if (new_time > m_timebar->end_time() + eps)
        new_time = m_timebar->start_time();
    m_timebar->set_current_time(new_time);
}

void TimelineWidget::step_back_one_frame()
{
    double shift = m_playback_mode == PlaybackMode::EveryFrame ? m_playback_by : 1.0;
    double new_time = m_timebar->current_time() - shift;
    // if (m_timebar->is_snap_time_mode())
    //     new_time = std::ceil(new_time);
    if (new_time < m_timebar->start_time() - eps)
        new_time = m_timebar->end_time();
    m_timebar->set_current_time(new_time);
}

void TimelineWidget::go_to_start()
{
    m_timebar->set_current_time(m_timebar->start_time());
}

void TimelineWidget::go_to_end()
{
    m_timebar->set_current_time(m_timebar->end_time());
}

inline double linstep(double mn, double mx, double x)
{
    double result = (x - mn) / (mx - mn);
    if (result > 1.0)
        result = 1.0;
    else if (result < 0.0)
        result = 0.0;
    return result;
}

void TimelineWidget::play_forwards()
{
    switch (m_playback_mode)
    {
    case PlaybackMode::EveryFrame:
        if (!m_timer->isActive() || (m_timer->isActive() && !m_is_play_forward))
        {
            stop_play();
            m_play_forward_btn->setIcon(QIcon(":icons/stop_play.png"));
            connect(m_timer, SIGNAL(timeout()), this, SLOT(step_forward_one_frame()));
            m_timer->start((int)(1000 / (m_frames_per_second / m_playback_by)));
            m_is_play_forward = true;
            m_is_playing = true;
        }
        else
        {
            stop_play();
        }
        break;
    case PlaybackMode::Realtime:
        if (m_timeline->state() != QTimeLine::Running || (m_timeline->state() != QTimeLine::Running && !m_is_play_forward))
        {
            stop_play();
            m_play_forward_btn->setIcon(QIcon(":icons/stop_play.png"));
            connect(m_timeline, &QTimeLine::frameChanged, [this](int frame) {
                if (m_sound)
                {
                    const double sound_frame = frame - m_sound_start;
                    const double sound_duration = m_player->duration() / 1000.0 * m_frames_per_second;
                    if (!m_sound_playing)
                    {
                        if (sound_frame >= 0 && sound_frame <= sound_duration)
                        {
                            m_sound_playing = true;
                            m_player->setPosition(sound_frame / m_frames_per_second * 1000.0);
                            m_player->play();
                            m_sound_frame = sound_frame;
                        }
                    }
                    else
                    {
                        if (sound_frame < 0.0 || sound_frame > sound_duration)
                        {
                            m_sound_playing = false;
                            m_player->stop();
                        }
                        else
                        {
                            if (sound_frame < m_sound_frame)
                            {
                                m_player->setPosition(sound_frame / m_frames_per_second * 1000.0);
                            }
                            m_sound_frame = sound_frame;
                        }
                    }
                }
                m_timebar->set_current_time((double)frame);
            });
            const int current_frame = std::ceil(m_timebar->current_time());
            const int interval = int(1000.0 / m_frames_per_second);
            const int start_frame = std::ceil(m_timebar->start_time());
            const int end_frame = std::floor(m_timebar->end_time());
            const int duration = std::abs(end_frame - start_frame) * interval;

            m_timeline->setDuration(duration);
            m_timeline->setDirection(QTimeLine::Forward);
            m_timeline->setFrameRange(start_frame, end_frame);
            m_timeline->setLoopCount(INT_MAX);
            m_timeline->setUpdateInterval(interval);
            m_timeline->setCurrentTime(std::ceil(duration * linstep((double)start_frame, (double)end_frame, (double)current_frame)));
            m_timeline->resume();

            m_is_play_forward = true;
            m_is_playing = true;
        }
        else
        {
            stop_play();
        }
        break;
    }
}

void TimelineWidget::play_backwards()
{
    switch (m_playback_mode)
    {
    case PlaybackMode::EveryFrame:
        if (!m_timer->isActive() || (m_timer->isActive() && m_is_play_forward))
        {
            stop_play();
            m_play_backward_btn->setIcon(QIcon(":icons/stop_play.png"));
            connect(m_timer, SIGNAL(timeout()), this, SLOT(step_back_one_frame()));
            m_timer->start((int)(1000 / (m_frames_per_second / m_playback_by)));
            m_is_play_forward = false;
            m_is_playing = true;
        }
        else
        {
            stop_play();
        }
        break;
    case PlaybackMode::Realtime:
        if (m_timeline->state() != QTimeLine::Running || (m_timeline->state() != QTimeLine::Running && m_is_play_forward))
        {
            stop_play();
            m_play_backward_btn->setIcon(QIcon(":icons/stop_play.png"));
            connect(m_timeline, &QTimeLine::frameChanged, [this](int frame) { m_timebar->set_current_time((double)frame); });
            const int current_frame = std::ceil(m_timebar->current_time());
            const int interval = int(1000.0 / m_frames_per_second);
            const int start_frame = std::ceil(m_timebar->start_time());
            const int end_frame = std::floor(m_timebar->end_time());
            const int duration = std::abs(end_frame - start_frame) * interval;

            m_timeline->setDuration(duration);
            m_timeline->setDirection(QTimeLine::Backward);
            m_timeline->setFrameRange(start_frame, end_frame);
            m_timeline->setLoopCount(INT_MAX);
            m_timeline->setUpdateInterval(interval);
            m_timeline->setCurrentTime(std::ceil(duration * linstep((double)start_frame, (double)end_frame, (double)current_frame)));
            m_timeline->resume();

            m_is_play_forward = true;
            m_is_playing = true;
        }
        else
        {
            stop_play();
        }
        break;
    }
}

void TimelineWidget::stop_play()
{
    if (m_sound)
    {
        m_player->stop();
        m_sound_playing = false;
    }

    m_is_playing = false;

    switch (m_playback_mode)
    {
    case PlaybackMode::EveryFrame:
        m_timer->stop();
        m_timer->disconnect(SIGNAL(timeout()));
        break;
    case PlaybackMode::Realtime:
        m_timeline->stop();
        m_timeline->disconnect(SIGNAL(frameChanged(int)));
        break;
    }

    m_play_backward_btn->setIcon(QIcon(":icons/play_backward.png"));
    m_play_forward_btn->setIcon(QIcon(":icons/play_forward.png"));
}

void TimelineWidget::step_forward_one_key()
{
    if (!m_timebar->get_keyframes())
        return;
    bool first_key_found_in_timeline = false;
    double forward_key;

    auto keyframes = m_timebar->get_keyframes();
    for (float key : *keyframes)
    {
        if (key > (m_timebar->start_time() - eps) && key < (m_timebar->end_time() + eps))
        {
            if (!first_key_found_in_timeline)
            {
                first_key_found_in_timeline = true;
                forward_key = key;
            }
            if (key > m_timebar->current_time() + eps)
            {
                forward_key = key;
                break;
            }
        }
    }

    if (first_key_found_in_timeline)
        m_timebar->set_current_time(forward_key);
}

void TimelineWidget::step_back_one_key()
{
    if (!m_timebar->get_keyframes())
        return;
    bool last_key_found_in_timeline = false;
    double forward_key;

    auto keyframes = m_timebar->get_keyframes();
    for (auto rit = keyframes->rbegin(); rit != keyframes->rend(); ++rit)
    {
        float key = *rit;
        if (key > (m_timebar->start_time() - eps) && key < (m_timebar->end_time() + eps))
        {
            if (!last_key_found_in_timeline)
            {
                last_key_found_in_timeline = true;
                forward_key = key;
            }
            if (key < m_timebar->current_time() - eps)
            {
                forward_key = key;
                break;
            }
        }
    }

    if (last_key_found_in_timeline)
        m_timebar->set_current_time(forward_key);
}

double TimelineWidget::current_time()
{
    return m_timebar->current_time();
}

void TimelineWidget::set_start_time(double value)
{
    QSignalBlocker lk(this);
    if (m_timebar->is_snap_time_mode())
        value = std::floor(value);
    m_timebar->set_start_time(value);
}

void TimelineWidget::set_end_time(double value)
{
    QSignalBlocker lk(this);
    if (m_timebar->is_snap_time_mode())
        value = std::floor(value);
    m_timebar->set_end_time(value);
}

void TimelineWidget::set_sound_display(const std::string& filepath, const double frame_offset)
{
    m_timebar->set_sound(filepath, frame_offset);
    m_sound = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // QMediaContent is gone in Qt6; the player takes the QUrl directly.
    m_player->setSource(QUrl::fromLocalFile(filepath.c_str()));
#else
    m_player->setMedia(QMediaContent(QUrl::fromLocalFile(filepath.c_str())));
#endif
    m_sound_start = frame_offset;
}

void TimelineWidget::clear_sound_display()
{
    m_timebar->clear_sound();
    m_sound = false;
}

void TimelineWidget::set_time_display(TimeDisplay mode)
{
    m_time_display = mode;
    m_timebar->set_time_display(mode);
    m_current_time_edit->set_time_display(mode);
    switch (mode)
    {
    case TimeDisplay::Frames:
        m_time_display_label->setText(i18n("toolbars.timeline", "Frame:"));
        break;
    case TimeDisplay::Timecode:
        m_time_display_label->setText(i18n("toolbars.timeline", "Timecode:"));
        break;
    default:
        m_time_display_label->setText("Dunno:");
        break;
    }

    emit time_display_changed(mode);
}

TimeDisplay TimelineWidget::get_time_display() const
{
    return m_time_display;
}

double TimelineWidget::get_playback_by() const
{
    return m_playback_by;
}

void TimelineWidget::set_playback_by(double val)
{
    m_playback_by = val;
}

TimelineWidget::PlaybackMode TimelineWidget::get_playback_mode() const
{
    return m_playback_mode;
}

void TimelineWidget::set_playback_mode(PlaybackMode mode)
{
    m_playback_mode = mode;
}

void TimelineWidget::set_frames_per_second(double value)
{
    {
        QSignalBlocker lk(this);
        m_frames_per_second = value;
        m_timebar->set_fps(value);
        m_current_time_edit->set_fps(value);
    }
}

void TimelineWidget::time_spinbox_editing_finished()
{
    double time = m_current_time_edit->value();
    if (m_timebar->is_snap_time_mode())
        time = std::floor(time);
    m_current_time_edit->setValue(time);
    m_timebar->set_current_time(time);
}

void TimelineWidget::time_drag(double time)
{
    if (m_sound)
    {
        m_player->setPosition(time / m_frames_per_second * 1000.0);
        m_player->play();
        m_drag_timer->start(200);
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    m_context_menu->exec(event->globalPos());
}
OPENDCC_NAMESPACE_CLOSE
