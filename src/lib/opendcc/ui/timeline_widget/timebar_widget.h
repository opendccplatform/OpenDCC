/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "api.h"
#include "opendcc/opendcc.h"
// workaround
#include "opendcc/base/qt_python.h"

// #include "opendcc/app/core/api.h"
#include <QtWidgets/QWidget>
#include <memory>
#include <set>
// #include "opendcc/app/core/settings.h"
#include "opendcc/ui/timeline_widget/time_display.h"

class AudioDecoder;

OPENDCC_NAMESPACE_OPEN

typedef std::set<float> KeyFrameSet;
typedef std::shared_ptr<KeyFrameSet> KeyFrameSetPtr;

enum class KeyframeDrawMode
{
    Timesamples,
    AnimationCurves
};

enum class KeyframeDisplayType
{
    Line,
    Rect,
    Arrow
};

/**
 * @brief This class represents a widget for displaying and manipulating time intervals.
 *        It provides functionality for displaying time marks, keyframes, and current time indicator.
 *
 */
class OPENDCC_UI_TIMELINE_WIDGET_API TimeBarWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a TimeBarWidget.
     *
     * @param current_time_indicator The current time indicator style.
     * @param subdivisions Whether to display subdivisions on the time bar.
     * @param parent The parent QWidget.
     *
     * @note It is important to specify opendcc namespace before CurrentTimeIndicator::Default because
     * Shiboken ignores it during generation and produces compilation error.
     */
    TimeBarWidget(CurrentTimeIndicator current_time_indicator = OpenDCC::CurrentTimeIndicator::Default, bool subdivisions = true,
                  QWidget *parent = nullptr);
    virtual ~TimeBarWidget();
    // Mouse event handlers
    void mousePressEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void mouseDoubleClickEvent(QMouseEvent *);
    // Paint event handler
    void paintEvent(QPaintEvent *);

    /**
     * @brief Returns the preferred size of the widget.
     *
     * @return The preferred size of the widget.
     */
    QSize sizeHint() const;

    /**
     * @brief Sets the current time.
     *
     * @param time The current time value.
     */
    void set_current_time(double time);
    /**
     * @brief Returns the current time.
     *
     * @return The current time value.
     */
    double current_time() { return m_currentTime; }
    /**
     * @brief Returns the start time of the time interval.
     *
     * @return The start time value.
     */
    double start_time() { return m_startTime; }
    /**
     * @brief Returns the end time of the time interval.
     *
     * @return The end time value.
     */
    double end_time() { return m_endTime; }

    /**
     * @brief Sets the start time of the time interval.
     *
     * @param start_time The start time value.
     */
    void set_start_time(const double start_time);

    /**
     * @brief Sets the end time of the time interval.
     *
     * @param end_time The end time value.
     */
    void set_end_time(const double end_time);

    /**
     * @brief Sets the frames per second (FPS) value for time calculations.
     *
     * @param fps The frames per second value.
     */
    void set_fps(const double fps)
    {
        m_fps = fps;
        m_wave_cache = false;
        update();
    }

    /**
     * @brief Sets the keyframes for the time interval.
     *
     * @param keyframes The set of keyframes.
     */
    void set_keyframes(KeyFrameSet &keyframes);

    /**
     * @brief Retrieves the keyframes for the time interval.
     *
     * @return The set of keyframes.
     */
    KeyFrameSetPtr get_keyframes();
    /**
     * @brief Checks if the snap time mode is enabled.
     *
     * @return True if snap time mode is enabled, false otherwise.
     */
    bool is_snap_time_mode() { return m_snap_time_mode; }
    /**
     * @brief Sets the snap time mode.
     *
     * @param state The state to set.
     */
    void set_snap_time_mode(bool state) { m_snap_time_mode = state; }

    /**
     * @brief Sets the keyframe draw mode.
     *
     * @param mode The keyframe draw mode to set.
     */
    void set_keyframe_draw_mode(KeyframeDrawMode mode) { m_keyframe_draw_mode = mode; }

    /**
     * @brief Retrieves the keyframe draw mode.
     *
     * @return The current keyframe draw mode.
     */
    KeyframeDrawMode get_keyframe_draw_mode() { return m_keyframe_draw_mode; }

    /**
     * @brief Resets the time selection.
     */
    void reset_selection()
    {
        if (m_time_selection)
        {
            m_time_selection = false;
            repaint();
        }
    }

    /**
     * @brief Sets the sound file and time position.
     *
     * @param filepath The path to the sound file.
     * @param time The time position of the sound file.
     */
    void set_sound(const std::string &filepath, const double time);
    /**
     * @brief Removes the currently set sound file from the widget.
     */
    void clear_sound();
    /**
     * @brief Sets the time display mode.
     *
     * @param mode The time display mode to set.
     */

    void set_time_display(TimeDisplay mode);

    /**
     * @brief Changes the appearance of the cursor.
     *
     * @param cursor The type of cursor.
     */
    void set_current_time_indicator_type(CurrentTimeIndicator cursor);

    /**
     * @brief Changes the appearance of keyframes.
     *
     * @param type The type of keyframe.
     */
    void set_keyframe_display_type(KeyframeDisplayType type);

    /**
     * @brief Changes the appearance of background.
     *
     * @param type The type of background.
     */
    void set_background_type(TimebarBackgroundType type);

    /**
     * @brief Changes the appearance of subdivisions.
     *
     * @param type Subdivisions.
     */
    void set_subdivisions(bool subdivisions);

protected:
    virtual void resizeEvent(QResizeEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    virtual void enterEvent(QEnterEvent *event) override;
#else
    virtual void enterEvent(QEvent *event) override;
#endif
    virtual void leaveEvent(QEvent *event) override;

Q_SIGNALS:
    /**
     * @brief Signal emitted when the current time changes.
     *
     * @param value The new current time value.
     */
    void current_time_changed(double value);

    /**
     * @brief Signal emitted when the time selection begins.
     *
     * @param start The start time of the selection.
     * @param end The end time of the selection.
     */
    void time_selection_begin(double start, double end);
    /**
     * @brief Signal emitted when the time selection moves.
     *
     * @param start The start time of the selection.
     * @param end The end time of the selection.
     */
    void time_selection_move(double start, double end);
    /**
     * @brief Signal emitted when the time selection ends.
     *
     * @param start The start time of the selection.
     * @param end The end time of the selection.
     */
    void time_selection_end(double start, double end);

    /**
     * @brief Signal emitted when the time is dragged.
     *
     * @param value The new time value after dragging.
     */
    void time_drag(double value);

private:
    QString get_time_string(double frame, bool add_prefix = false);
    double compute_time(int x);

    bool m_mouse_hover = false;

    CurrentTimeIndicator m_current_time_indicator = CurrentTimeIndicator::Default;
    KeyframeDisplayType m_keyframe_type = KeyframeDisplayType::Line;
    TimebarBackgroundType m_background_type = TimebarBackgroundType::Stripped;

    bool m_subdivisions = true;

    TimeDisplay m_time_display = TimeDisplay::Frames;
    AudioDecoder *m_audio_decoder;
    QVector<qreal> m_wave;
    bool m_wave_cache = false;
    double m_fps = 24.0;
    double m_sound_time = 0.0;

    int time_to_x_pos(double);
    int m_height;
    float m_halfHeightFactor;

    int m_drawnStep;
    int m_fontHeight;

    double m_startTime;
    double m_endTime;
    double m_frameStep;
    double m_currentTime;
    int m_indent;
    bool m_snap_time_mode = true;
    KeyFrameSetPtr m_keyframes;
    KeyframeDrawMode m_keyframe_draw_mode = KeyframeDrawMode::Timesamples;

    bool m_time_selection_drag = false;
    bool m_time_selection = false;
    double m_time_selection_start;
    double m_time_selection_end;
    double m_time_drag_start;
    double m_time_drag_end;
    bool m_time_selection_left_drag = false;
    bool m_time_selection_center_drag = false;
    bool m_time_selection_right_drag = false;
    int m_time_selection_drag_x;
};

OPENDCC_NAMESPACE_CLOSE
