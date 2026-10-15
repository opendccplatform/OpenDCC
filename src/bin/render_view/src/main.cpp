// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"
#include "qtsingleapplication.h"
#include "opendcc/render_view/image_view/app.h"
#include "opendcc/render_view/image_view/translator.h"
#include "opendcc/render_view/image_view/stylesheet.h"
#include "opendcc/ui/color_theme/color_theme.h"
#include "opendcc/base/crash_reporting/sentry_crash_handler.h"

#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QHostInfo>

#ifdef USE_SENTRY
#include <sentry.h>

#include <QScopeGuard>
#endif

int main(int argc, char *argv[])
{
    // Enable High DPI scaling
    // Remove this code after migrating to Qt6
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    OPENDCC_NAMESPACE_USING
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtSingleApplication app(argc, argv);

    auto configs_dir = QDir(app.applicationDirPath());
#ifdef OPENDCC_OS_MAC
    configs_dir.cdUp();
    configs_dir.cd("Resources");
#else
    configs_dir.cdUp();
#endif
    configs_dir.cd("configs");

    const auto app_config = OPENDCC_NAMESPACE::base::app_config::ApplicationConfig(configs_dir.filePath("default.toml").toLocal8Bit().toStdString());
    // Disable it for now until we will be ready to use multiple databases
#if 0
    sentry_options_t *options = sentry_options_new();

    // different path for render_view db, cause not sure if its safe for sentry-native
    QString temp_dir_path = QDir::temp().filePath("opendcc_render_view_sentry_db_directory");

    sentry_options_set_database_path(options, temp_dir_path.toStdString().c_str());
    sentry_options_set_dsn(options, app_config.get<std::string>("sentry.dsn").c_str());

    sentry_options_set_auto_session_tracking(options, true);
    sentry_options_set_release(options, "opendcc-v" OPENDCC_VERSION_STRING);

    sentry_init(options);

    sentry_set_tag("program", "render_view");

    QString user_name = qgetenv("USER");
    if (user_name.isEmpty())
    {
        user_name = qgetenv("USERNAME");
    }
    if (!user_name.isEmpty())
    {
        user_name = user_name.toLower();
        sentry_value_t user = sentry_value_new_object();
        sentry_value_set_by_key(user, "username", sentry_value_new_string(user_name.toStdString().c_str()));
        sentry_set_user(user);
    }

    QString project_name = qgetenv("PROJECT_NAME");
    if (!project_name.isEmpty())
    {
        sentry_set_tag("project_name", project_name.toLower().toStdString().c_str());
    }
    sentry_set_tag("ui_available", "yes");

    QString server_name = QHostInfo::localHostName();
    if (!server_name.isEmpty())
    {
        sentry_set_tag("server_name", server_name.toLower().toStdString().c_str());
    }
    auto sentryClose = qScopeGuard([] { sentry_close(); });
#endif

    if (app.isRunning())
        return !app.sendMessage("activate!");

    auto style = QStyleFactory::create("fusion");

    QApplication::setStyle(style);
    QPalette palette = QApplication::palette();
    std::string ui_theme = "dark";

    if (app_config.is_valid())
        ui_theme = app_config.get<std::string>("settings.ui.color_theme", "dark");
    if (ui_theme == "dark")
        set_color_theme(ColorTheme::DARK);
    else if (ui_theme == "light")
        set_color_theme(ColorTheme::LIGHT);
    else
        set_color_theme(ColorTheme::DARK);

    if (get_color_theme() == ColorTheme::DARK)
    {
        palette.setColor(QPalette::Window, QColor::fromRgb(68, 68, 68));
        palette.setColor(QPalette::Base, QColor::fromRgb(48, 48, 48));
        palette.setColor(QPalette::AlternateBase, QColor::fromRgb(55, 55, 55));

        palette.setColor(QPalette::Button, QColor::fromRgb(80, 80, 80));
        palette.setColor(QPalette::Text, QColor::fromRgb(200, 200, 200));
        palette.setColor(QPalette::ButtonText, QColor::fromRgb(200, 200, 200));
        palette.setColor(QPalette::WindowText, QColor::fromRgb(200, 200, 200));
        palette.setColor(QPalette::Highlight, QColor::fromRgb(103, 141, 178));
        palette.setColor(QPalette::Light, QColor::fromRgb(80, 80, 80));

        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor::fromRgb(42, 42, 42));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor::fromRgb(100, 100, 100));
        palette.setColor(QPalette::Disabled, QPalette::Light, QColor::fromRgb(30, 30, 30));

        QApplication::setPalette(palette);
        app.setStyleSheet(render_view_stylesheet);
    }
    else
    {
        app.setStyleSheet(render_view_stylesheet_light);
    }

    QString settings_path = QDir::homePath() + "/.opendcc/" + qApp->applicationName() + ".ini";

    auto settings = std::make_unique<QSettings>(settings_path, QSettings::IniFormat);

    auto preferences = RenderViewPreferences::read(std::move(settings));

    auto &instance = Translator::instance();
    instance.set_language(preferences.language);

    RenderViewMainWindow mainWin(std::move(preferences), app_config);
    app.setActivationWindow(&mainWin);
    app.setActiveWindow(&mainWin);
    mainWin.init_python();
    mainWin.init_python_ui();
    mainWin.show();
    return app.exec();
}
