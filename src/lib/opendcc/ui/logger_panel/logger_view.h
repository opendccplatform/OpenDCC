/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/logging/logging_delegate.h"
#include "opendcc/ui/logger_panel/logger_widget.h"

#include <QSet>
#include <QMenu>
#include <QTimer>
#include <QWidget>
#include <QVector>
#include <QActionGroup>

OPENDCC_NAMESPACE_OPEN

class OPENDCC_LOGGER_PANEL_API LoggerManager
    : public QObject
    , public base::logging::LoggingDelegate
{
    Q_OBJECT;

    using Message = LoggerWidget::Message;

public:
    LoggerManager(QObject* parent = nullptr);
    ~LoggerManager() = default;

    MessageModel* model() { return m_model; }

    LoggerManager(const LoggerManager&) = delete;
    LoggerManager(LoggerManager&&) = delete;

    LoggerManager& operator=(const LoggerManager&) = delete;
    LoggerManager& operator=(LoggerManager&&) = delete;

public Q_SLOTS:
    void on_clear_messages(QSet<QString> channels);
    void on_add_message(QString channel, base::logging::LogLevel log_level, QString msg);

private Q_SLOTS:
    void flush_messages_to_model();

Q_SIGNALS:
    void send_message(QString channel, base::logging::LogLevel log_level, QString msg);
    void clear_messages(QSet<QString> channels);
    void selected_channels_cleared();
    void message_added(Message message);
    void model_updated(QVector<Message> channels);

private:
    void log(const base::logging::MessageContext& context, const std::string& message) override;

    MessageModel* m_model = nullptr;
    QVector<Message> m_message_buffer;
    QTimer* m_messages_flush_timer = nullptr;
};

class OPENDCC_LOGGER_PANEL_API LoggerView : public QWidget
{
    Q_OBJECT;

    using Message = LoggerWidget::Message;

public:
    LoggerView(LoggerManager* logger_manager, QWidget* parent = nullptr);
    ~LoggerView();

private Q_SLOTS:
    void on_add_message(QString channel, base::logging::LogLevel log_level, QString msg);

Q_SIGNALS:
    void selected_channels_changed(QSet<QString> selected_channels);
    void wrap_mode_changed(bool is_wrap);

private:
    bool try_add_channel(const QString& channel);

    QMenu* m_channel_menu = nullptr;
    LoggerManager* m_logger_manager = nullptr;
    QSet<QString> m_selected_channels;
    QActionGroup* m_channel_action_group = nullptr;
};

OPENDCC_NAMESPACE_CLOSE
