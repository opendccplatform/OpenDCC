/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include <QRegularExpression>
#include "time_display.h"
#include <QDoubleSpinBox>

class QRegularExpression;

OPENDCC_NAMESPACE_OPEN

/**
 * @class TimeWidget
 * @brief This class represents a widget for entering and displaying time values.
 *        It derives from QDoubleSpinBox for numeric input functionality.
 */
class TimeWidget : public QDoubleSpinBox
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a TimeWidget.
     *
     * @param mode The time display mode.
     * @param parent The parent QWidget.
     */
    TimeWidget(TimeDisplay mode, QWidget* parent = nullptr);
    ~TimeWidget();

    /**
     * @brief Sets the time display mode of the TimeWidget.
     *
     * @param mode The time display mode to set.
     */
    void set_time_display(TimeDisplay mode);

    /**
     * @brief Sets the frames per second (FPS) value for time calculations.
     *
     * @param fps The frames per second value to set.
     */
    void set_fps(double fps);

    /**
     * @brief Converts a value to its corresponding text representation.
     *
     * @param value The value to convert.
     * @return The string representation of the value.
     */
    QString textFromValue(double value) const override;

    /**
     * @brief Validates the text input and updates the validation status.
     *
     * @param text The input text.
     * @param pos The cursor position.
     * @return The validation state of the input text.
     */
    QValidator::State validate(QString& text, int& pos) const override;

    /**
     * @brief Converts the text representation to its corresponding numeric value.
     *
     * @param text The input text.
     * @return The corresponding numeric value.
     */
    double valueFromText(const QString& text) const override;

private:
    TimeDisplay m_time_display = TimeDisplay::Frames;
    double m_fps = 24.0;
    QRegularExpression m_timecode_exp = QRegularExpression("(-?)(\\d\\d*):(\\d\\d?):(\\d\\d?):(\\d\\d?\\d?)");
};

OPENDCC_NAMESPACE_CLOSE
