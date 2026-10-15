// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/logger_panel/logger_output_widget.h"
#include <QBoxLayout>
#include <QScrollBar>
#include <QStringBuilder>
#include <QDateTime>
#include <array>
#include <cmath>
#include <unordered_map>
#include <opendcc/base/logging/logging_utils.h>

OPENDCC_NAMESPACE_OPEN

using base::logging::LogLevel;
namespace logging = base::logging;

LoggerOutputWidget::LoggerOutputWidget(MessageModel* model, QWidget* parent)
    : LoggerWidget(model, parent)
{
    m_output_text_edit = new QPlainTextEdit;
    m_output_text_edit->setReadOnly(true);
    m_output_text_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto output_layout = new QVBoxLayout;
    output_layout->setContentsMargins(1, 1, 1, 1);
    output_layout->addWidget(m_output_text_edit);

    setLayout(output_layout);
}

void LoggerOutputWidget::on_selected_channels_cleared()
{
    m_output_text_edit->clear();
    append_html_messages(m_model->messages());
    append_html_messages(m_cached_messages);
}

QString LoggerOutputWidget::to_formatted_string(const Message& message) const
{
    const QColor color = log_level_to_color(message.log_level);

    QString formatted = QDateTime::currentDateTime().toString("HH:mm:ss") + "  ";

    if (message.log_level >= LogLevel::Warning)
    {
        formatted = formatted % to_formatted_log_level(message.log_level) % "  | ";
    }
    else
    {
        formatted += "         | ";
    }

    formatted = QString("<span style=\"white-space:pre-wrap;color:%1\">%2%3</span><br>")
                    .arg(color.name())
                    .arg(formatted)
                    .arg(message.message.toHtmlEscaped());
    return formatted;
}

void LoggerOutputWidget::on_selected_channels_changed(const QSet<QString>& selected_channels)
{
    LoggerWidget::on_selected_channels_changed(selected_channels);
    m_output_text_edit->clear();
    append_html_messages(m_model->messages());
    append_html_messages(m_cached_messages);
}

void LoggerOutputWidget::on_wrap_mode_changed(bool is_wrap)
{
    LoggerWidget::on_wrap_mode_changed(is_wrap);
    if (is_wrap)
    {
        m_output_text_edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    }
    else
    {
        m_output_text_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
}

void LoggerOutputWidget::on_model_updated(const QVector<Message>& messages)
{
    m_cached_messages.clear();
}

void LoggerOutputWidget::on_message_added(const Message& message)
{
    m_cached_messages.push_back(message);
    if (m_selected_channels.contains(message.channel))
    {
        append_html_message(to_formatted_string(message));
    }
}

void LoggerOutputWidget::append_html_message(const QString& message)
{
    m_output_text_edit->appendHtml(message.left(message.length() - 4)); // remove last <br>
    m_output_text_edit->verticalScrollBar()->setValue(m_output_text_edit->verticalScrollBar()->maximum());
    m_output_text_edit->horizontalScrollBar()->setValue(m_output_text_edit->horizontalScrollBar()->minimum());
}

void LoggerOutputWidget::append_html_messages(const QVector<Message>& messages)
{
    QString result;
    for (const auto& message : messages)
    {
        if (m_selected_channels.contains(message.channel))
        {
            result += to_formatted_string(message);
        }
    }

    if (!result.isEmpty())
    {
        append_html_message(result);
    }
}

QString LoggerOutputWidget::to_formatted_log_level(LogLevel log_level) const
{
    static std::unordered_map<LogLevel, QString> upper_log_level_strings = {
        { LogLevel::Info, QString(logging::log_level_to_str(LogLevel::Info).c_str()).toUpper() },
        { LogLevel::Debug, QString(logging::log_level_to_str(LogLevel::Debug).c_str()).toUpper() },
        { LogLevel::Warning, QString(logging::log_level_to_str(LogLevel::Warning).c_str()).toUpper() },
        { LogLevel::Error, QString(logging::log_level_to_str(LogLevel::Error).c_str()).toUpper() },
        { LogLevel::Fatal, QString(logging::log_level_to_str(LogLevel::Fatal).c_str()).toUpper() }
    };

    QString result(7, ' ');
    result.replace(0, upper_log_level_strings[log_level].length(), upper_log_level_strings[log_level]);
    return result;
}
OPENDCC_NAMESPACE_CLOSE
