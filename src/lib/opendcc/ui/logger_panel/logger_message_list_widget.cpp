// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/logger_panel/logger_message_list_widget.h"

#include "opendcc/app/ui/application_ui.h"

#include <QMenu>
#include <QTableView>
#include <QVBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QHeaderView>
#include <opendcc/base/logging/logging_utils.h>

OPENDCC_NAMESPACE_OPEN

using base::logging::LogLevel;

LoggerMessageListWidget::LoggerMessageListWidget(MessageModel* model, QWidget* parent)
    : LoggerWidget(model, parent)
{
    auto message_list_layout = new QVBoxLayout;
    message_list_layout->setContentsMargins(1, 1, 1, 1);
    m_messages_table_proxy = new MessageTableProxy;
    m_search_line = new SearchWidget;
    m_search_line->setPlaceholderText(i18n("logger.message_list", "Search Messages"));

    connect(m_search_line, &QLineEdit::textChanged, this, &LoggerMessageListWidget::set_search_query);

    auto table_item_delegate = new TableItemDelegate();
    m_messages_table = new TableView;
    m_messages_table->horizontalHeader()->setStretchLastSection(true);
    m_messages_table->verticalHeader()->setVisible(false);
    m_messages_table->setSelectionBehavior(QAbstractItemView::SelectionBehavior::SelectRows);
    m_messages_table->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
    m_messages_table->setItemDelegate(table_item_delegate);
    m_messages_table->setShowGrid(false);
    m_messages_table->setWordWrap(true);

    m_messages_table_proxy->setSourceModel(m_model);
    m_messages_table->setModel(m_messages_table_proxy);

    auto log_level_button = new QToolButton();
    log_level_button->setAutoRaise(true);
    log_level_button->setIconSize(QSize(20, 20));
    log_level_button->setIcon(QIcon(":icons/level_log"));
    log_level_button->setToolTip(i18n("logger.message_list.tooltip", "Log Level"));
    log_level_button->setPopupMode(QToolButton::InstantPopup);
    auto log_level_menu = new QMenu;
    auto log_level_action_group = new QActionGroup(log_level_menu);
    log_level_action_group->setExclusive(false);

    struct Entry
    {
        LogLevel level;
        bool enable;
    };
    static const std::vector<Entry> log_level_defaults = {
        { LogLevel::Info, false }, { LogLevel::Debug, false }, { LogLevel::Warning, true }, { LogLevel::Error, true }, { LogLevel::Fatal, true }
    };
    for (const auto& entry : log_level_defaults)
    {
        auto action = new QAction(base::logging::log_level_to_str(entry.level).c_str());
        action->setCheckable(true);
        action->setChecked(entry.enable);
        const auto log_level_flag = log_level_to_flag(entry.level);
        if (action->isChecked())
        {
            m_log_level_mask ^= log_level_flag;
        }
        action->setData(static_cast<int32_t>(log_level_flag));

        log_level_action_group->addAction(action);
    }
    m_messages_table_proxy->set_log_level_mask(m_log_level_mask);

    auto log_level_all_action = new QAction(i18n("logger.message_list.log_level", "All"));
    log_level_all_action->setCheckable(true);
    log_level_all_action->setChecked(m_log_level_mask == LogLevelFlags::All);
    connect(log_level_all_action, &QAction::triggered, [log_level_action_group, this](bool checked) {
        for (auto& action : log_level_action_group->actions())
        {
            action->setChecked(checked);
        }
        m_log_level_mask = checked ? LogLevelFlags::All : LogLevelFlags::None;
        update_log_level();
    });
    connect(log_level_action_group, &QActionGroup::triggered, [log_level_all_action, this](QAction* action) {
        m_log_level_mask ^= static_cast<LogLevelFlags>(action->data().value<int32_t>());
        log_level_all_action->setChecked(m_log_level_mask == LogLevelFlags::All);
        update_log_level();
    });

    log_level_menu->addAction(log_level_all_action);
    log_level_menu->addActions(log_level_action_group->actions());
    log_level_button->setMenu(log_level_menu);

    auto control_panel = new QHBoxLayout;
    control_panel->addWidget(log_level_button);
    control_panel->addWidget(m_search_line);
    message_list_layout->addLayout(control_panel);
    message_list_layout->addWidget(m_messages_table);

    setLayout(message_list_layout);
}

void LoggerMessageListWidget::on_selected_channels_cleared()
{
    m_messages_table_proxy->invalidate();
}

void LoggerMessageListWidget::set_search_query(QString query)
{
    m_messages_table_proxy->set_search_query(query);
    m_messages_table->resizeRowsToContents();
    m_messages_table->scrollToBottom();
}

void LoggerMessageListWidget::on_selected_channels_changed(const QSet<QString>& selected_channels)
{
    LoggerWidget::on_selected_channels_changed(selected_channels);
    m_messages_table_proxy->set_channels(selected_channels);
    m_messages_table->resizeRowsToContents();
    m_messages_table->scrollToBottom();
}

void LoggerMessageListWidget::on_wrap_mode_changed(bool is_wrap)
{
    LoggerWidget::on_wrap_mode_changed(is_wrap);
    m_messages_table->setWordWrap(is_wrap);
    m_messages_table->resizeRowsToContents();
}

void LoggerMessageListWidget::on_model_updated(const QVector<Message>& messages)
{
    if (!messages.empty())
    {
        for (int i = m_model->rowCount() - messages.size(); i < m_model->rowCount(); i++)
        {
            resize_row(i);
        }
        m_messages_table->scrollToBottom();
    }
}

void LoggerMessageListWidget::on_message_added(const Message& message) {}

void LoggerMessageListWidget::resizeEvent(QResizeEvent* event)
{
    LoggerWidget::resizeEvent(event);
    m_messages_table->resizeRowsToContents();
}

void LoggerMessageListWidget::update_log_level()
{
    m_messages_table_proxy->set_log_level_mask(m_log_level_mask);
    m_messages_table->resizeRowsToContents();
    m_messages_table->scrollToBottom();
}

void LoggerMessageListWidget::resize_row(int row)
{
    auto index = m_model->index(row, 0);
    const int row_to_resize = m_messages_table_proxy->mapFromSource(index).row();
    m_messages_table->resizeRowToContents(row_to_resize);
}

TableItemDelegate::TableItemDelegate(QObject* parent /*= nullptr*/)
    : QStyledItemDelegate(parent)
{
}

QWidget* TableItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QLineEdit* line_edit = new QLineEdit(parent);
    line_edit->setReadOnly(true);
    return line_edit;
}

TableView::TableView(QWidget* parent /*= nullptr*/)
    : QTableView(parent)
{
}

void TableView::mousePressEvent(QMouseEvent* event)
{
    clearSelection();
    QTableView::mousePressEvent(event);
}

OPENDCC_NAMESPACE_CLOSE
