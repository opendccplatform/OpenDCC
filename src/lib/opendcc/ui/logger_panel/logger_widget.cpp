// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/logger_panel/logger_widget.h"

#include "opendcc/app/ui/application_ui.h"
#include <opendcc/base/logging/logging_utils.h>

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.ui.logger_panel");

using base::logging::LogLevel;
namespace logging = base::logging;

LoggerWidget::LoggerWidget(MessageModel* model, QWidget* parent /* = nullptr */)
    : QWidget(parent)
    , m_model(model)
{
}

LoggerWidget::~LoggerWidget() {}

QColor LoggerWidget::log_level_to_color(LogLevel log_level)
{
    // clang-format off
    static std::unordered_map<LogLevel, QColor> log_level_colors = {
        { LogLevel::Info, QColor(Qt::white) },
        { LogLevel::Debug, QColor(Qt::green) },
        { LogLevel::Warning, QColor(Qt::yellow) },
        { LogLevel::Error, QColor(Qt::red) },
        { LogLevel::Fatal, QColor(Qt::magenta) },
    };
    // clang-format on

    return log_level_colors[log_level];
}

LoggerWidget::LogLevelFlags LoggerWidget::log_level_to_flag(LogLevel log_level)
{
    static const std::unordered_map<LogLevel, LogLevelFlags> mapping = {
        { LogLevel::Unknown, LogLevelFlags::Error },   { LogLevel::Info, LogLevelFlags::Info },   { LogLevel::Debug, LogLevelFlags::Debug },
        { LogLevel::Warning, LogLevelFlags::Warning }, { LogLevel::Error, LogLevelFlags::Error }, { LogLevel::Fatal, LogLevelFlags::Fatal }
    };
    return mapping.at(log_level);
}

QString LoggerWidget::log_level_to_qstring(LogLevelFlags log_level)
{
    static std::unordered_map<LogLevelFlags, QString> log_level_strings = {
        { LogLevelFlags::All, i18n("logger.message_list.log_level", "All") },
        { LogLevelFlags::None, i18n("logger.message_list.log_level", "None") },
        { LogLevelFlags::Info, i18n("logger.message_list.log_level", "Info") },
        { LogLevelFlags::Debug, i18n("logger.message_list.log_level", "Debug") },
        { LogLevelFlags::Warning, i18n("logger.message_list.log_level", "Warning") },
        { LogLevelFlags::Error, i18n("logger.message_list.log_level", "Error") },
        { LogLevelFlags::Fatal, i18n("logger.message_list.log_level", "Fatal") },
    };

    return log_level_strings[log_level];
}

void LoggerWidget::on_selected_channels_changed(const QSet<QString>& selected_channels)
{
    m_selected_channels = selected_channels;
}

void LoggerWidget::on_wrap_mode_changed(bool is_wrap)
{
    m_is_wrap = is_wrap;
}

int MessageModel::rowCount(const QModelIndex& parent /*= QModelIndex()*/) const
{
    return m_messages.size();
}

int MessageModel::columnCount(const QModelIndex& parent /*= QModelIndex()*/) const
{
    return 3;
}

QVariant MessageModel::data(const QModelIndex& index, int role /*= Qt::DisplayRole*/) const
{
    const int row = index.row();
    const int column = index.column();
    if (!index.isValid())
    {
        return QVariant();
    }

    if (row >= m_messages.size() || row < 0)
    {
        return QVariant();
    }

    switch (role)
    {
    case Qt::DisplayRole:
        switch (column)
        {
        case 0:
            return m_messages[row].channel;
        case 1:
            return QString::fromStdString(logging::log_level_to_str(m_messages[row].log_level));
        case 2:
            return m_messages[row].message.simplified();
        }
    case Qt::ForegroundRole:
        if (column == 1)
        {
            return LoggerWidget::log_level_to_color(m_messages[row].log_level);
        }
        break;
    case Qt::EditRole:
        switch (column)
        {
        case 0:
            return m_messages[row].channel;
        case 1:
            return QString::fromStdString(logging::log_level_to_str(m_messages[row].log_level));
        case 2:
            return m_messages[row].message.simplified();
        }
    }
    return QVariant();
}

MessageModel::MessageModel(QObject* parent /*= nullptr*/)
    : QAbstractTableModel(parent)
{
}

QVariant MessageModel::headerData(int section, Qt::Orientation orientation, int role /*= Qt::DisplayRole*/) const
{
    if (role != Qt::DisplayRole)
    {
        return QVariant();
    }

    if (orientation == Qt::Horizontal)
    {
        switch (section)
        {
        case 0:
            return i18n("logger.message_list.header", "Channel");
        case 1:
            return i18n("logger.message_list.header", "Log Level");
        case 2:
            return i18n("logger.message_list.header", "Message");
        }
    }
    return QVariant();
}

void MessageModel::append_rows(const QVector<Message>& rows)
{
    const int check_overflow = std::numeric_limits<int>::max() - rows.size() - m_messages.size();
    if (check_overflow < 0)
    {
        removeRows(0, -check_overflow);
    }

    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size() + rows.size() - 1);
    m_messages.append(rows);
    endInsertRows();
}

void MessageModel::remove_if(const std::function<bool(const Message&)>& predicate)
{
    auto new_end = std::remove_if(m_messages.begin(), m_messages.end(), predicate);
    int new_size = std::distance(m_messages.begin(), new_end);

    if (new_size == m_messages.size())
    {
        return;
    }

    beginRemoveRows(QModelIndex(), new_size, m_messages.size() - 1);
    m_messages.erase(new_end, m_messages.end());
    endRemoveRows();
}

const MessageModel::Message& MessageModel::message_at(int row) const
{
    return m_messages[row];
}

Qt::ItemFlags MessageModel::flags(const QModelIndex& index) const
{
    auto flags = QAbstractTableModel::flags(index);
    flags |= Qt::ItemIsEditable;
    return flags;
}

QVector<MessageModel::Message> MessageModel::messages() const
{
    return m_messages;
}

bool MessageModel::removeRows(int row, int count, const QModelIndex& parent /*= QModelIndex()*/)
{
    if (row < 0 || row > m_messages.size() - 1 || count < 0)
    {
        return false;
    }

    if (row + count > m_messages.size())
    {
        return false;
    }

    if (row + count < row) // check for overflow
    {
        return false;
    }

    beginRemoveRows(parent, row, count);
    m_messages.remove(row, count);
    endRemoveRows();

    return true;
}

OPENDCC_NAMESPACE_CLOSE
