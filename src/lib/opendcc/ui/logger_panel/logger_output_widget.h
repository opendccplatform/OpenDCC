/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/logger_panel/logger_widget.h"

#include <QPlainTextEdit>
#include <QVector>

OPENDCC_NAMESPACE_OPEN
class LoggerOutputWidget : public LoggerWidget
{
    Q_OBJECT;

public:
    LoggerOutputWidget(MessageModel* model, QWidget* parent = 0);

public Q_SLOTS:
    void on_selected_channels_changed(const QSet<QString>& selected_channels) override;
    void on_wrap_mode_changed(bool is_wrap) override;
    void on_model_updated(const QVector<Message>& messages) override;
    void on_message_added(const Message& message) override;
    void on_selected_channels_cleared() override;

private:
    void append_html_messages(const QVector<Message>& messages);
    QString to_formatted_string(const Message& message) const;
    QString to_formatted_log_level(base::logging::LogLevel log_level) const;
    void append_html_message(const QString& message);

    QVector<Message> m_cached_messages;
    QPlainTextEdit* m_output_text_edit = nullptr;
};
OPENDCC_NAMESPACE_CLOSE
