// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

// workaround
#include "opendcc/base/qt_python.h"

#include "opendcc/app/ui/main_window.h"
#include "opendcc/opendcc.h"

#include <QtWidgets/QLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QSplitter>
#include <QtCore/QSignalBlocker>
#include <QUndoStack>
#include <QSettings>
#include <FloatingDockContainer.h>
#include <DockAreaWidget.h>
#include <DockAreaTitleBar.h>
#include <DockAreaTabBar.h>
#include <DockContainerWidget.h>
#include <DockAreaWidget.h>

#include <QScreen>
#include <QXmlStreamReader>

#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/base/tf/type.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/plug/plugin.h>

#include "opendcc/base/logging/logging_utils.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/panel_factory.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_scene_context.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"

#include "opendcc/ui/logger_panel/logger_view.h"
#include "opendcc/app/ui/logger/render_log.h"
#include "opendcc/app/viewport/viewport_refine_manager.h"
#include "opendcc/app/viewport/def_cam_settings.h"
#include "opendcc/app/viewport/tool_settings_view.h"
#include "opendcc/app/viewport/viewport_scene_delegate.h"
#include "opendcc/app/viewport/viewport_usd_camera_mapper.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/app/viewport/ui_camera_mapper.h"
#include "opendcc/ui/color_theme/color_theme.h"

#include <algorithm>
#include <locale>
#include <codecvt>
#include <string>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE
namespace
{
    class StatusBarLoggingDelegate : public LoggingDelegate
    {
    public:
        StatusBarLoggingDelegate(MainWindow* window)
            : m_window(window)
        {
            Logger::add_logging_delegate(this);
        }

        void log(const MessageContext& context, const std::string& message) override
        {
            if (m_window)
            {
                Q_EMIT m_window->send_message(QString::fromStdString(context.channel), context.level, QString::fromStdString(message));
            }
        }

    private:
        MainWindow* m_window = nullptr;
    };
}

MainWindow::MainWindow()
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    const auto settings_path = Application::instance().get_settings_path();
    // TODO XXX unhandled code settings path could be empty, so we are just going to be optimistic here

    const auto app_config = Application::get_app_config();
    const auto app_type_str = QString(app_config.get<std::string>("settings.app.type", "usd_editor").c_str());
    const auto app_name_str = QString(app_config.get<std::string>("settings.app.name", "dcc_base").c_str());

    QString settings_file_dir = QDir(settings_path.c_str()).filePath(app_type_str);
    QDir().mkpath(settings_file_dir);
    QString settings_file_name = QDir(settings_file_dir).filePath(app_name_str + ".ini");

    m_settings = new QSettings(settings_file_name, QSettings::IniFormat);

    // try to restore window geometry or try to take a lot of space on the primary screen
    const auto main_window_geometry = m_settings->value("ui/main_window_geometry", QByteArray()).toByteArray();
    if (main_window_geometry.isEmpty())
    {
        const QScreen* primary_screen = qApp->primaryScreen();
        QRect available_geometry = primary_screen->availableGeometry();
        available_geometry.adjust(100, 100, -100, -100);
        setGeometry(available_geometry);
    }
    else
    {
        restoreGeometry(main_window_geometry);
    }

    qRegisterMetaType<LogLevel>("LogLevel");
    auto status_bar_label = new QLabel;
    // status_bar_label->setStyleSheet("QLabel{color: rgb(200, 200, 200);}");
    m_status_bar_logging = std::make_unique<StatusBarLoggingDelegate>(this);
    m_logger_panel_manager = new LoggerManager(this);

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [] { Application::instance().get_session()->process_events(); });

    m_live_share_changed_cid = Application::instance().get_session()->register_event_callback(Session::EventType::LIVE_SHARE_STATE_CHANGED, [timer] {
        if (Application::instance().get_session()->is_live_sharing_enabled())
            timer->start(0);
        else
            timer->stop();
    });

    statusBar()->addWidget(status_bar_label, 1);

    connect(this, &MainWindow::send_message, this, [this, status_bar_label](QString channel, LogLevel log_level, QString msg) {
        const auto metrics = status_bar_label->fontMetrics();
        const auto log_level_name = QString::fromStdString(log_level_to_str(log_level));
        const auto elided_msg =
            metrics.elidedText(msg, Qt::TextElideMode::ElideRight,
                               (status_bar_label->width() - metrics.horizontalAdvance(log_level_name + "  ")) * status_bar_label->devicePixelRatio());
        status_bar_label->setText(QString("<span style=\"color:%1\">%2</span>  %3")
                                      .arg(LoggerWidget::log_level_to_color(log_level).name())
                                      .arg(log_level_name)
                                      .arg(elided_msg.toHtmlEscaped()));
    });

    for (auto item : PXR_NS::PlugRegistry::GetInstance().GetAllPlugins())
    {
        if (item->GetName() == "opendcc_commands")
            item->Load();
    }

    const auto window_title = Application::get_app_config().get<std::string>("settings.app.window.title");
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    const auto unicode_title = converter.from_bytes(window_title);
    setWindowTitle(QString::fromStdWString(unicode_title));

    qApp->setProperty("window", QVariant::fromValue<QObject*>(this));
    setObjectName("mainWindow");

    m_main_container_widget = new ads::CDockManager(this);
    QString docking_stylesheet_path = ":/stylesheets/docking_stylesheet.qss";
    if (get_color_theme() == ColorTheme::LIGHT)
    {
        docking_stylesheet_path = ":/stylesheets/docking_stylesheet_light.qss";
    }
    QFile docking_stylesheet_file(docking_stylesheet_path);
    docking_stylesheet_file.open(QFile::ReadOnly);

    m_main_container_widget->setStyleSheet(QString(docking_stylesheet_file.readAll()));
    docking_stylesheet_file.close();
    connect(m_main_container_widget, &ads::CDockManager::dockAreaCreated,
            [this](ads::CDockAreaWidget* dock_area) { add_create_panel_btn(dock_area); });

    init_timeline_ui();
    m_before_stage_closed_callback_id = Application::instance().register_event_callback(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED, [] {
        const auto& stage_id = Application::instance().get_session()->get_current_stage_id();
        if (stage_id.IsValid())
        {
        }
    });

    ViewportCameraMapperFactory::register_camera_mapper(TfToken("USD"), [] { return std::make_shared<ViewportUsdCameraMapper>(); });

    ViewportCameraMapperFactory::register_camera_mapper(TfToken("UI"), [] { return std::make_shared<UICameraMapper>(); });

    PanelFactory::instance().register_panel(
        "viewport",
        []() -> QWidget* { return new ViewportWidget(ViewportSceneContextRegistry::get_instance().create_scene_context(TfToken("USD"))); },
        i18n("panels", "Viewport").toStdString(), false, ":icons/panel_viewport");

    PanelFactory::instance().register_panel(
        "logger", [this] { return new LoggerView(m_logger_panel_manager); }, i18n("panels", "Logger").toStdString(), false, ":icons/panel_logger");

    PanelFactory::instance().register_panel(
        "tool_settings", [] { return new ToolSettingsView; }, i18n("panels", "Tool Settings").toStdString(), true, ":icons/panel_tool_settings");

    PanelFactory::instance().register_panel(
        "render_log", [] { return new RenderLog(); }, i18n("panels", "Render Log").toStdString(), false, ":icons/panel_render_log");
}

MainWindow::~MainWindow()
{
    Application::instance().unregister_event_callback(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED, m_before_stage_closed_callback_id);
    Application::instance().get_session()->unregister_event_callback(Session::EventType::LIVE_SHARE_STATE_CHANGED, m_live_share_changed_cid);

    cleanup_timeline_ui();
}

ads::CDockWidget* MainWindow::create_panel(const std::string panel_type, bool floating /*= true*/, ads::CDockAreaWidget* parent_panel /*= nullptr*/,
                                           ads::DockWidgetArea dock_area /*= ads::CenterDockWidgetArea*/, int site_index /*= 0*/)
{
    auto registry = PanelFactory::instance().get_registry();
    auto find_it = registry.find(panel_type);
    if (find_it != registry.end())
    {
        ads::CDockWidget* dock_widget = nullptr;
        QString dock_widget_name = panel_type.c_str();
        const bool is_singleton = find_it->second.singleton;
        if (is_singleton)
        {
            if (dock_widget = m_main_container_widget->findDockWidget(dock_widget_name))
            {
                if (dock_widget->isClosed())
                    m_main_container_widget->removeDockWidget(dock_widget);
                else
                {
                    // If the dock widget is docked, make it the current widget so the user can see it.
                    // This behavior aligns with how Maya operates.
                    auto doc_area_widget = dock_widget->dockAreaWidget();
                    if (doc_area_widget != nullptr)
                    {
                        doc_area_widget->setCurrentDockWidget(dock_widget);
                    }

                    dock_widget->activateWindow();

                    return dock_widget;
                }
            }
        }
        QWidget* new_content_widget = PanelFactory::instance().create_panel_widget(find_it->second);
        if (new_content_widget == nullptr)
            return nullptr;

        QString dock_name = dock_widget_name;
        int i = 0;
        while (m_main_container_widget->findDockWidget(dock_name))
            dock_name = dock_widget_name + "#" + QString::number(i++);

        dock_widget = new ads::CDockWidget(dock_name);
        dock_widget->setWindowTitle(find_it->second.label.c_str());
        if (!find_it->second.icon.empty())
        {
            dock_widget->setIcon(QIcon(find_it->second.icon.c_str()));
        }
        dock_widget->setWidget(new_content_widget);
        dock_widget->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
        connect(dock_widget, &ads::CDockWidget::closeRequested, new_content_widget, [new_content_widget] { new_content_widget->close(); });
        m_main_container_widget->addDockWidget(dock_area, dock_widget, parent_panel);
        if (floating)
        {
            auto flex_widget = new ads::CFloatingDockContainer(dock_widget);
            flex_widget->show();
            flex_widget->setGeometry(0, 0, 600, 400);
            flex_widget->move(geometry().center() - flex_widget->rect().center());
        }

        return dock_widget;
    }

    return nullptr;
}

void MainWindow::load_panel_layout(QSettings* settings)
{
    if (!settings)
        settings = m_settings;
    QByteArray content = settings->value("ui/panel_layout", QByteArray()).toByteArray();
    if (content.isEmpty())
    {
        create_panel("viewport", false);
    }
    else
    {
        restore_widgets(content);
        m_main_container_widget->restoreState(content);
        for (auto item : m_main_container_widget->dockWidgetsMap())
            item->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    }

    // restoreGeometry(m_settings->value("ui/main_window_geometry", saveGeometry()).toByteArray());
    const auto main_window_state = settings->value("ui/main_window_state", QByteArray()).toByteArray();
    if (!main_window_state.isEmpty())
    {
        restoreState(main_window_state);
    }
}

void MainWindow::closeEvent(QCloseEvent* evt)
{
    QByteArray content = m_main_container_widget->saveState();
    auto settings = ApplicationUI::instance().get_main_window()->get_settings();
    settings->setValue("ui/panel_layout", content);
    settings->setValue("ui/main_window_geometry", saveGeometry());
    settings->setValue("ui/main_window_state", saveState());
    DefCamSettings::instance().save_settings();
    // auto decoded_content = content.startsWith("<?xml") ? content : qUncompress(content);
    if (m_main_container_widget)
    {
        // manually close all floating widgets
        // destruction of CFloatingDockContainer produces memory leaks if its done via simple 'delete'
        // like in CDockManager destructor (ads internal issue)
        // manually send close event to each of these widgets in order to properly free resources

        // this might be a mistake but currently "closeOtherAreas" doesn't always close all dock areas
        // for some mysterious reason
        // m_main_container_widget->closeOtherAreas(nullptr);
        for (auto area : m_main_container_widget->openedDockAreas())
        {
            area->closeArea();
        }

        for (auto widget : m_main_container_widget->floatingWidgets())
            widget->close();
        m_main_container_widget->deleteLater();
        m_main_container_widget = nullptr;
    }
}

void MainWindow::arrange_splitters(ads::CDockWidget* dock_widget, const std::vector<double>& proportion)
{
    qApp->processEvents(); // forces the ui to update, not gonna work without it

    auto dock_area = dock_widget->dockAreaWidget();
    QList<int> sizes = m_main_container_widget->splitterSizes(dock_area);
    if (sizes.length() != proportion.size())
    {
        qDebug() << "Error: wrong number of splitters";
        return;
    }
    int total_size = 0;
    for (int size : sizes)
    {
        total_size += size;
    }
    double portion = total_size / double(proportion.size());
    for (int i = 0; i < proportion.size(); i++)
    {
        sizes[i] = portion - portion * (1.0 - proportion[i]);
    }

    // normalize
    int new_total_size = 0;
    for (int size : sizes)
    {
        new_total_size += size;
    }
    double coef = total_size / double(new_total_size);

    for (int i = 0; i < proportion.size(); i++)
    {
        sizes[i] = sizes[i] * coef;
    }

    m_main_container_widget->setSplitterSizes(dock_area, sizes);
}

void MainWindow::close_panels()
{
    m_main_container_widget->closeOtherAreas(nullptr);
    for (auto area : m_main_container_widget->openedDockAreas())
    {
        area->closeArea();
    }
    for (auto widget : m_main_container_widget->floatingWidgets())
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // ADS 3.x clears AutoHideChildren before hiding, so the dock widgets still inside are not
        // toggled closed. ADS 4.x (Qt6) dropped the method; skip the hide() there rather than fire
        // those toggles, since the container is being destroyed anyway.
        widget->hideAndDeleteLater();
#else
        widget->deleteLater();
#endif
    }
}

void MainWindow::save_layout(const std::string& path)
{
    QSettings settings(path.c_str(), QSettings::IniFormat);

    QByteArray content = m_main_container_widget->saveState();
    settings.setValue("ui/panel_layout", content);
    settings.setValue("ui/main_window_geometry", saveGeometry());
    settings.setValue("ui/main_window_state", saveState());
}

void MainWindow::load_layout(const std::string& path)
{
    // close panels
    m_main_container_widget->closeOtherAreas(nullptr);
    for (auto area : m_main_container_widget->openedDockAreas())
    {
        area->closeArea();
    }
    for (auto widget : m_main_container_widget->floatingWidgets())
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // ADS 3.x clears AutoHideChildren before hiding, so the dock widgets still inside are not
        // toggled closed. ADS 4.x (Qt6) dropped the method; skip the hide() there rather than fire
        // those toggles, since the container is being destroyed anyway.
        widget->hideAndDeleteLater();
#else
        widget->deleteLater();
#endif
    }

    qApp->processEvents(); // forces the ui to update, because reasons

    QSettings* settings = new QSettings(path.c_str(), QSettings::IniFormat);

    const auto main_window_geometry = settings->value("ui/main_window_geometry", QByteArray()).toByteArray();
    restoreGeometry(main_window_geometry);
    load_panel_layout(settings);
    delete settings;
}

void MainWindow::add_create_panel_btn(ads::CDockAreaWidget* dock_area)
{
    QMenu* panels_menu = new QMenu("Panels");
    auto CustomButton = new QToolButton(dock_area);
    CustomButton->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    CustomButton->setToolTip("Add Panel");
    CustomButton->setIcon(QIcon(":icons/new_tab"));
    CustomButton->setAutoRaise(true);
    CustomButton->setMenu(panels_menu);
    CustomButton->setPopupMode(QToolButton::InstantPopup);

    connect(panels_menu, &QMenu::aboutToShow, [this, panels_menu, dock_area]() {
        panels_menu->clear();

        auto registry = PanelFactory::instance().get_registry(); // unordered maps can't be sorted

        std::vector<std::pair<std::string, PanelFactory::Entry>> registry_vector(registry.begin(), registry.end());

        std::sort(registry_vector.begin(), registry_vector.end(), [](const auto& a, const auto& b) -> bool {
            if (a.second.group == b.second.group)
                return a.second.label < b.second.label;
            return a.second.group < b.second.group;
        });

        std::string previous_group = "";

        for (auto entry : registry_vector)
        {
            if (previous_group != entry.second.group)
            {
                previous_group = entry.second.group;
                panels_menu->addSeparator();
            }
            auto action = panels_menu->addAction(entry.second.label.c_str());
            if (entry.second.icon != "")
            {
                action->setIcon(QIcon(entry.second.icon.c_str()));
            }
            connect(action, &QAction::triggered, action, [this, entry, dock_area](bool) mutable { create_panel(entry.first, false, dock_area); });
        }
        panels_menu->show();
    });

    auto TitleBar = dock_area->titleBar();
    int tabBarIndex = TitleBar->indexOf(TitleBar->tabBar());
    TitleBar->insertWidget(tabBarIndex + 1, CustomButton);
}

void MainWindow::restore_widgets(const QByteArray& xml_content)
{
    auto decoded_content = xml_content.startsWith("<?xml") ? xml_content : qUncompress(xml_content);
    QXmlStreamReader reader(decoded_content);
    reader.readNextStartElement();
    if (reader.name() != QLatin1String("QtAdvancedDockingSystem"))
        return;

    auto registry = PanelFactory::instance().get_registry();

    std::function<void()> find_widgets = [this, &reader, &find_widgets, &registry] {
        while (reader.readNextStartElement())
        {
            if (reader.name() != QLatin1String("Widget"))
            {
                find_widgets();
                continue;
            }

            // In certain cases ADS hides widgets instead of closing them
            // we don't want to create widgets that were hidden during the last session
            bool ok = false;
            const auto closed = reader.attributes().value("Closed").toInt(&ok);
            if (ok && closed == 1)
            {
                reader.skipCurrentElement();
                continue;
            }

            const auto name = reader.attributes().value("Name");
            if (name.isEmpty())
            {
                reader.skipCurrentElement();
                continue;
            }

            if (m_main_container_widget->findDockWidget(name.toString()) != nullptr)
            {
                reader.skipCurrentElement();
                continue;
            }

            const auto name_str = name.toString();
            const auto type = name_str.left(name_str.lastIndexOf('#'));
            const auto std_type = type.toLocal8Bit().toStdString();
            if (auto widget = PanelFactory::instance().create_panel_widget(std_type))
            {
                const auto title = PanelFactory::instance().get_panel_title(std_type);
                auto dock_widget = new ads::CDockWidget(name_str, m_main_container_widget);

                auto find_it = registry.find(std_type);
                if (find_it != registry.end())
                {
                    auto icon = find_it->second.icon;
                    if (!icon.empty())
                    {
                        dock_widget->setIcon(QIcon(icon.c_str()));
                    }
                }

                m_main_container_widget->addDockWidget(ads::DockWidgetArea::CenterDockWidgetArea, dock_widget);
                dock_widget->setWidget(widget);
                dock_widget->setProperty("dirty", true);
                dock_widget->setWindowTitle(title.c_str());
                connect(dock_widget, &ads::CDockWidget::closeRequested, widget, [widget] { widget->close(); });
            }
            reader.skipCurrentElement();
        }
    };
    find_widgets();
}

OPENDCC_NAMESPACE_CLOSE
