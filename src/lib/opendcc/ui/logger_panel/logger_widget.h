/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/logger_panel/api.h"
#include "opendcc/base/logging/logger.h"

#include <QSet>
#include <QWidget>
#include <QAbstractTableModel>

OPENDCC_NAMESPACE_OPEN

class MessageModel;

class OPENDCC_LOGGER_PANEL_API LoggerWidget : public QWidget
{
    Q_OBJECT

public:
    enum LogLevelFlags
    {
        None = 0,
        Info = 1 << 0,
        Debug = 1 << 1,
        Warning = 1 << 2,
        Error = 1 << 3,
        Fatal = 1 << 4,
        All = Info | Debug | Warning | Error | Fatal
    };

    struct Message
    {
        QString channel;
        base::logging::LogLevel log_level;
        QString message;
    };

    LoggerWidget(MessageModel* model, QWidget* parent = nullptr);
    virtual ~LoggerWidget();

    static LogLevelFlags log_level_to_flag(base::logging::LogLevel log_level);
    static QString log_level_to_qstring(LogLevelFlags log_level);
    static QColor log_level_to_color(base::logging::LogLevel log_level);

public Q_SLOTS:
    virtual void on_selected_channels_changed(const QSet<QString>& selected_channels);
    virtual void on_wrap_mode_changed(bool is_wrap);
    virtual void on_model_updated(const QVector<Message>& messages) = 0;
    virtual void on_message_added(const Message& message) = 0;
    virtual void on_selected_channels_cleared() = 0;

protected:
    QSet<QString> m_selected_channels;
    bool m_is_wrap;
    MessageModel* m_model = nullptr;
};

class MessageModel : public QAbstractTableModel
{
    Q_OBJECT;

    using Message = LoggerWidget::Message;

public:
    MessageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    const Message& message_at(int row) const;
    QVector<Message> messages() const;
    void append_rows(const QVector<Message>& rows);
    void remove_if(const std::function<bool(const Message&)>& predicate);
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;

private:
    QVector<Message> m_messages;
};

OPENDCC_NAMESPACE_CLOSE

Q_DECLARE_METATYPE(OPENDCC_NAMESPACE::base::logging::LogLevel);
