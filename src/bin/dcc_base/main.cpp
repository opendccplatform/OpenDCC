// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"

#include "opendcc/base/defines.h"
#include "opendcc/base/qt_python.h"
#include "opendcc/base/vendor/spdlog/spdlog.h"

#include "opendcc/app/core/session.h"
#include "opendcc/app/core/application.h"

#include "opendcc/app/ui/main_window.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/global_event_filter.h"

#include "opendcc/render_system/render_system.h"
#include "opendcc/ui/color_theme/color_theme.h"

#include <pxr/base/arch/env.h>
#include <pxr/base/arch/systemInfo.h>
#include <pxr/base/plug/registry.h>
#include "opendcc/base/packaging/toml_parser.h"

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

#include <QDir>
#include <QTimer>
#include <QScopeGuard>
#include <QApplication>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QCommandLineParser>
#include <QHostInfo>

#include <iostream>

OPENDCC_NAMESPACE_USING
PXR_NAMESPACE_USING_DIRECTIVE
static QApplication* s_qapp = nullptr;

QDir get_configs_path()
{
    const auto app_path = QString::fromLocal8Bit(ArchGetExecutablePath().c_str());

    QDir configs_path(app_path);
    configs_path.cdUp();
    configs_path.cdUp();
#ifdef OPENDCC_OS_MAC
    configs_path.cd("Resources");
#endif
    configs_path.cd("configs");

    return configs_path;
}

std::vector<std::string> get_vec_args(const QStringList& app_args)
{
    std::vector<std::string> args;
    args.push_back(app_args[0].toStdString());
    if (app_args.length() > 3)
    {
        for (int i = 3; i < app_args.length(); i++)
        {
            args.push_back(app_args[i].toStdString());
        }
    }
    return args;
}

void configure_crash_reporter(const ApplicationConfig& cfg)
{
    if (!CrashHandler::is_enabled())
    {
        return;
    }

    QString user_name = qgetenv("USER");
    if (user_name.isEmpty())
    {
        user_name = qgetenv("USERNAME");
    }
    if (!user_name.isEmpty())
    {
        user_name = user_name.toLower();
        CrashHandler::set_user(user_name.toStdString());
    }

    QString project_name = qgetenv("PROJECT_NAME");
    if (!project_name.isEmpty())
    {
        CrashHandler::set_tag("project_name", project_name.toLower().toStdString());
    }

    QString server_name = QHostInfo::localHostName();
    if (!server_name.isEmpty())
    {
        CrashHandler::set_tag("server_name", server_name.toLower().toStdString());
    }
}

void setup_ui(Application& app, QApplication& qt_app)
{
    // Set color theme
    const auto default_ui_theme = app.get_app_config().get<std::string>("settings.ui.color_theme", "dark");
    const auto ui_theme = app.get_settings()->get<std::string>("ui.color_theme", default_ui_theme);
    if (ui_theme == "dark")
        set_color_theme(ColorTheme::DARK);
    else if (ui_theme == "light")
        set_color_theme(ColorTheme::LIGHT);
    else
        set_color_theme(ColorTheme::DARK);

    auto style = QStyleFactory::create("fusion");
    QApplication::setStyle(style);

    if (get_color_theme() == ColorTheme::DARK)
    {
        QPalette palette = QApplication::palette();

        palette.setColor(QPalette::Window, QColor::fromRgb(68, 68, 68));
        palette.setColor(QPalette::Base, QColor::fromRgb(48, 48, 48));
        palette.setColor(QPalette::AlternateBase, QColor::fromRgb(55, 55, 55));

        palette.setColor(QPalette::Button, QColor::fromRgb(80, 80, 80));
        palette.setColor(QPalette::Text, QColor::fromRgb(200, 200, 200));
        palette.setColor(QPalette::ButtonText, QColor::fromRgb(200, 200, 200));
        palette.setColor(QPalette::WindowText, QColor::fromRgb(200, 200, 200));
        palette.setColor(QPalette::Highlight, QColor::fromRgb(103, 141, 178));
        palette.setColor(QPalette::Light, QColor::fromRgb(80, 80, 80));

        // Fusion draws grooves, frames and toolbar edges from Midlight/Dark/Mid/Shadow, inherited
        // from the platform palette. Qt 6.5 detects Windows dark mode and hands those back
        // near-black, so the timeline and shelf lose their contrast. Pin them to the Qt5 values.
        palette.setColor(QPalette::Midlight, QColor::fromRgb(227, 227, 227));
        palette.setColor(QPalette::Dark, QColor::fromRgb(160, 160, 160));
        palette.setColor(QPalette::Mid, QColor::fromRgb(160, 160, 160));
        palette.setColor(QPalette::Shadow, QColor::fromRgb(105, 105, 105));
        palette.setColor(QPalette::BrightText, QColor::fromRgb(255, 255, 255));
        palette.setColor(QPalette::HighlightedText, QColor::fromRgb(255, 255, 255));
        palette.setColor(QPalette::ToolTipBase, QColor::fromRgb(255, 255, 220));
        palette.setColor(QPalette::ToolTipText, QColor::fromRgb(0, 0, 0));
        // PlaceholderText is left alone, Qt6 resolves it better for a dark theme.

        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor::fromRgb(42, 42, 42));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor::fromRgb(100, 100, 100));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor::fromRgb(90, 90, 90));
        palette.setColor(QPalette::Disabled, QPalette::Light, QColor::fromRgb(30, 30, 30));

        QApplication::setPalette(palette);
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Removed in Qt6, where the context-help button is off by default anyway
    QApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
#endif
    QString stylesheet_path = ":/stylesheets/application_stylesheet.qss";
    if (get_color_theme() == ColorTheme::LIGHT)
    {
        stylesheet_path = ":/stylesheets/application_stylesheet_light.qss";
    }
    QFile app_stylesheet_file(stylesheet_path);
    app_stylesheet_file.open(QFile::ReadOnly);
    qt_app.setStyleSheet(QString(app_stylesheet_file.readAll()));
    app_stylesheet_file.close();
    ApplicationUI::instance().init_ui();
}

void setup_attributes()
{
    // Enable High DPI scaling
    // Remove this code after migrating to Qt6
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);

    QSurfaceFormat fmt;
    fmt.setSamples(4);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
#ifdef OPENDCC_OS_MAC
    fmt.setVersion(4, 1);
#endif
    QSurfaceFormat::setDefaultFormat(fmt);
}

void release_resources()
{
    render_system::RenderSystem::instance().stop_render();
    Application::instance().uninitialize_extensions();
    Application::destroy_command_server();
    delete s_qapp;
}

int main(int argc, char* argv[])
{
    auto raii = qScopeGuard(&release_resources);

    QCommandLineParser parser;
    parser.addPositionalArgument("stage", "stage path to open on start");
    QCommandLineOption shell_option("shell", "init python shell");
    parser.addOption(shell_option);
    QCommandLineOption config_option("config", "application config .toml file", "<config-file>", get_configs_path().filePath("default.toml"));
    parser.addOption(config_option);
    QCommandLineOption script_option("script", "run python script", "filepath");
    parser.addOption(script_option);
    QCommandLineOption test_option("with-tests", "run registered tests");
    parser.addOption(test_option);

    QStringList app_args;
    // pass any non option arguments as python interpreter args
    // that can be used in --script mode
    // we pass any more than 3 args (including app name)
    // auto app_args = qt_app.arguments();
    for (int i = 0; i < argc; i++)
    {
        app_args << QString::fromLocal8Bit(argv[i]);
    }

    parser.parse(app_args);

    const auto app_config = ApplicationConfig(parser.value(config_option).toLocal8Bit().toStdString());
    CrashHandlerSession crash_handler_session(app_config, "dcc_base");
    configure_crash_reporter(app_config);

    Application::set_app_config(app_config);
    Application::create_command_server();

    auto args = get_vec_args(app_args);
    Application& app = Application::instance();
    if (parser.isSet(test_option))
    {
        app.init_python(args);
        app.initialize_extensions();
        doctest::Context context(argc, argv);
        int res = context.run();
        if (context.shouldExit())
        {
            return res;
        }
    }
    Logger::set_log_level(LogLevel::Info);

    // if we dont pass script flag, ignore all positional arguments assuming its script args
    // TODO in future I think we should allow mixing stage list and script flag(i.e open stage list and modify it via script), assuming that script
    // arguments should be anything after --script flag, but that's hard to do with current CommandLineParser
    if (!parser.isSet(script_option) && !parser.isSet(shell_option))
    {
        setup_attributes();
        s_qapp = new QApplication(argc, argv);

        auto& app = Application::instance();
        const auto default_ui_language = app.get_app_config().get<std::string>("settings.ui.language", "en");
        const auto ui_lang = app.get_settings()->get<std::string>("settings.ui.language", default_ui_language);
        PlugRegistry::GetInstance().RegisterPlugins(Application::instance().get_application_root_path() + "/plugin/usd");
        PlugRegistry::GetInstance().RegisterPlugins(Application::instance().get_application_root_path() + "/plugin/opendcc");
        ApplicationUI::instance().set_ui_language(ui_lang);

        app.init_python(args);
        if (!parser.isSet(test_option))
        {
            app.initialize_extensions();
        }
        Application::instance().run_startup_init();
        app.update_render_control();

        setup_ui(app, *s_qapp);

        auto global_event_filter = new GlobalEventFilter;
        s_qapp->installEventFilter(global_event_filter);

        auto app_session = Application::instance().get_session();
        auto stage_list = parser.positionalArguments();
        for (int i = 0; i < stage_list.size(); i++)
        {
            app_session->open_stage(stage_list[i].toStdString());
        }

        return s_qapp->exec();
    }
    else if (parser.isSet(shell_option))
    {
        app.init_python(args);
        app.initialize_extensions();
        Application::instance().run_startup_init();
        app.update_render_control();

        app.init_python_shell();
        return 0;
    }
    else if (parser.isSet(script_option))
    {
        app.init_python(args);
        app.initialize_extensions();
        Application::instance().run_startup_init();
        app.update_render_control();

        return app.run_python_script(parser.value(script_option).toStdString());
    }
    return -1;
}
