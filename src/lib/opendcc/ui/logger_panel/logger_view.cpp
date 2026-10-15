// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/logger_panel/logger_view.h"

#include "opendcc/app/ui/application_ui.h"
#include "opendcc/ui/color_theme/color_theme.h"
#include "opendcc/ui/logger_panel/logger_output_widget.h"
#include "opendcc/ui/logger_panel/logger_message_list_widget.h"

#include <QMenu>
#include <QDebug>
#include <QAction>
#include <QMenuBar>
#include <QDateTime>
#include <QBoxLayout>
#include <QTabWidget>

#include <array>

OPENDCC_NAMESPACE_OPEN

using base::logging::LogLevel;
LoggerView::LoggerView(LoggerManager* manager, QWidget* parent)
    : QWidget(parent)
    , m_logger_manager(manager)
{
    if (!manager)
    {
        OPENDCC_ERROR("LoggerManager is null: logger panel cannot be created.");
        return;
    }

    QString style = R"V0G0N(
QTabWidget::pane {
    background: palette(light);
    border-top-color: palette(light);
}

QTabBar::tab:selected, QTabBar::tab:hover {
    background: palette(light);
    color: palette(foreground);
}

QTabBar::tab:!selected {
    background: TAB_BACKGROUND;
}

QTabBar::tab {
    color: TAB_COLOR;
    background: TAB_BACKGROUND;

    padding-left: 12px;
    padding-right: 12px;
    padding-top: 4px;
    padding-bottom: 5px;

    border-radius: 0px;
    border-left: 0px;

    border: 1px solid;
    border-width: 0px 1px 0px 0px;
    border-color: palette(base) palette(light) palette(light) palette(base);
}

QTabBar::tab:left {
    padding-left: 4px;
    padding-right: 5px;
    padding-top: 12px;
    padding-bottom: 12px;
    border-width: 0px 0px 1px 0px;
    border-color: palette(light) palette(base) palette(light) palette(light);
}

QTabBar::tab:last {
    border: 0px;
}
)V0G0N";

    if (get_color_theme() == ColorTheme::LIGHT)
    {
        style = style.replace("TAB_COLOR", "#3b3b3b");
        style = style.replace("TAB_BACKGROUND", "#d6d6d6");
    }
    else
    {
        style = style.replace("TAB_COLOR", "palette(dark)");
        style = style.replace("TAB_BACKGROUND", "rgb(55, 55, 55)");
    }

    setStyleSheet(style);

    const auto& messages = m_logger_manager->model()->messages();

    auto tab_widget = new QTabWidget;
    std::array<LoggerWidget*, 2> logger_widgets = { new LoggerMessageListWidget(m_logger_manager->model()),
                                                    new LoggerOutputWidget(m_logger_manager->model()) };
    for (const auto& widget : logger_widgets)
    {
        connect(this, &LoggerView::selected_channels_changed, widget, &LoggerWidget::on_selected_channels_changed);
        connect(this, &LoggerView::wrap_mode_changed, widget, &LoggerWidget::on_wrap_mode_changed);
        connect(m_logger_manager, &LoggerManager::message_added, widget, &LoggerWidget::on_message_added);
        connect(m_logger_manager, &LoggerManager::model_updated, widget, &LoggerWidget::on_model_updated);
        connect(m_logger_manager, &LoggerManager::selected_channels_cleared, widget, &LoggerWidget::on_selected_channels_cleared);

        widget->on_model_updated(messages);
    }

    tab_widget->addTab(logger_widgets[0], i18n("logger.tab", "Message List"));
    tab_widget->addTab(logger_widgets[1], i18n("logger.tab", "Output"));

    auto menu_bar = new QMenuBar;
    m_channel_menu = new QMenu(i18n("logger.message_list", "Channels"));
    m_channel_action_group = new QActionGroup(m_channel_menu);
    m_channel_action_group->setExclusive(false);

    auto all_channels_action = new QAction(i18n("logger.message_list.channel", "All"));
    m_channel_menu->addAction(all_channels_action);
    all_channels_action->setCheckable(true);
    all_channels_action->setChecked(true);
    connect(all_channels_action, &QAction::triggered, [this](bool checked) {
        if (checked)
        {
            for (auto& action : m_channel_action_group->actions())
            {
                m_selected_channels.insert(action->text());
                action->setChecked(true);
            }
        }
        else
        {
            for (auto& action : m_channel_action_group->actions())
            {
                action->setChecked(false);
            }
            m_selected_channels.clear();
        }

        selected_channels_changed(m_selected_channels);
    });

    connect(m_channel_action_group, &QActionGroup::triggered, [all_channels_action, this](QAction* action) {
        const QString& channel_name = action->text();
        if (action->isChecked())
        {
            m_selected_channels.insert(channel_name);
        }
        else
        {
            m_selected_channels.remove(channel_name);
        }

        all_channels_action->setChecked(m_selected_channels.size() == m_channel_action_group->actions().size());
        selected_channels_changed(m_selected_channels);
    });

    auto edit_menu = new QMenu(i18n("logger.menu_bar", "Edit"));
    menu_bar->addMenu(m_channel_menu);
    menu_bar->addMenu(edit_menu);

    auto wrap_text_action = new QAction(i18n("logger.menu_bar.edit", "Wrap Text"));
    wrap_text_action->setCheckable(true);
    connect(wrap_text_action, &QAction::triggered, [this](bool is_checked) { wrap_mode_changed(is_checked); });
    wrap_text_action->trigger();
    edit_menu->addAction(wrap_text_action);

    auto clear_all_action = new QAction(i18n("logger.menu_bar.edit", "Clear All"));
    connect(clear_all_action, &QAction::triggered, [this]() { m_logger_manager->on_clear_messages(m_selected_channels); });
    edit_menu->addAction(clear_all_action);

    connect(m_logger_manager, &LoggerManager::send_message, this, &LoggerView::on_add_message);
    connect(m_logger_manager, &LoggerManager::clear_messages, m_logger_manager, &LoggerManager::on_clear_messages);

    auto layout = new QVBoxLayout;

    layout->setContentsMargins(1, 1, 1, 1);
    layout->addWidget(menu_bar);
    layout->addWidget(tab_widget);
    layout->setSpacing(3);
    setLayout(layout);

    for (const auto& message : messages)
    {
        try_add_channel(message.channel);
    }
}

void LoggerView::on_add_message(QString channel, LogLevel log_level, QString msg)
{
    try_add_channel(channel);
}

LoggerView::~LoggerView() {}

bool LoggerView::try_add_channel(const QString& channel)
{
    for (const auto& action : m_channel_action_group->actions())
    {
        if (action->text() == channel)
        {
            return false;
        }
    }

    auto action = new QAction(channel);
    action->setCheckable(true);
    action->setChecked(true);
    m_channel_action_group->addAction(action);
    m_channel_menu->addAction(action);
    m_channel_action_group->triggered(action);
    // m_selected_channels.insert(channel);

    selected_channels_changed(m_selected_channels);
    return true;
}

void LoggerManager::on_clear_messages(QSet<QString> channels)
{
    m_model->remove_if([&channels](const Message& message) { return channels.contains(message.channel); });

    selected_channels_cleared();
}

void LoggerManager::flush_messages_to_model()
{
    m_model->append_rows(m_message_buffer);
    model_updated(m_message_buffer);

    m_message_buffer.clear();
}

void LoggerManager::on_add_message(QString channel, LogLevel log_level, QString msg)
{
    Message message = { channel, log_level, msg };
    m_message_buffer.push_back(message);

    message_added(message);
    if (!m_messages_flush_timer->isActive())
    {
        m_messages_flush_timer->start();
    }
}

LoggerManager::LoggerManager(QObject* parent /*= nullptr*/)
    : QObject(parent)
{
    m_model = new MessageModel;

    m_messages_flush_timer = new QTimer;
    m_messages_flush_timer->setSingleShot(true);
    m_messages_flush_timer->setInterval(1000);
    connect(m_messages_flush_timer, &QTimer::timeout, this, &LoggerManager::flush_messages_to_model);
    connect(this, &LoggerManager::send_message, this, &LoggerManager::on_add_message);

    base::logging::Logger::add_logging_delegate(this);
}

void LoggerManager::log(const base::logging::MessageContext& context, const std::string& message)
{
    Q_EMIT send_message(QString::fromStdString(context.channel), context.level, QString::fromStdString(message));
}

OPENDCC_NAMESPACE_CLOSE
