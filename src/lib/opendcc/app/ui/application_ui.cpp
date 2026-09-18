// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

// workaround
#include "opendcc/base/qt_python.h"
#include <QRegularExpression>
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/core/py_interp.h"
#include "usd_fallback_proxy/core/source_registry.h"
#include "opendcc/app/viewport/prim_material_override.h"

#include <QTranslator>

#include <string>

#include <OpenColorIO/OpenColorIO.h>
#include <QFileInfo>
#include <QShortcut>
#include <QApplication>

#include "pxr/usd/sdf/fileFormat.h"
#include "QDir"

OPENDCC_NAMESPACE_OPEN

ApplicationUI* ApplicationUI::m_instance = nullptr;

ApplicationUI& ApplicationUI::instance()
{
    if (!m_instance)
    {
        m_instance = new ApplicationUI();
    }
    return *m_instance;
}

void ApplicationUI::delete_instance()
{
    if (m_instance)
    {
        delete m_instance;
        m_instance = nullptr;
    }
}

ApplicationUI::ApplicationUI()
{
    // USD Fallback Proxy used by Attribute Editor, and we need to preload fallback plugins somewhere
    SourceRegistry::get_instance().load_plugins();
    init_ocio();
}

ApplicationUI::~ApplicationUI()
{
    if (m_main_window)
    {
        delete m_main_window;
    }
}

void ApplicationUI::init_ocio()
{
    namespace OCIO = OCIO_NAMESPACE;

    auto ocio_var = getenv("OCIO");
    OCIO::ConstConfigRcPtr config;

    if (ocio_var && QFileInfo::exists(ocio_var))
    {
        config = OCIO::Config::CreateFromEnv();
    }
    else
    {
        QString config_path = Application::instance().get_application_root_path().c_str();
        config_path.append("/ocio/config.ocio");
        if (QFileInfo::exists(config_path))
            config = OCIO::Config::CreateFromFile(config_path.toStdString().c_str());
        else
            config = OCIO::Config::Create(); // create a fall-back config.
    }

    OCIO::SetCurrentConfig(config);
}

ViewportWidget* ApplicationUI::get_active_view()
{

    if (m_active_view == nullptr)
    {
        // Not sure if its good idea, but fixes case then we close all viewports, and create new viewport, without changing active_view focus
        auto viewports = ViewportWidget::get_live_widgets();
        if (viewports.size() > 0)
        {
            return *(viewports.begin());
        }
        else
        {
            return nullptr;
        }
    }
    else
    {
        return m_active_view;
    }
}

void ApplicationUI::set_active_view(ViewportWidget* view)
{
    m_active_view = view;
    Application::instance().dispatch_event(Application::EventType::ACTIVE_VIEW_CHANGED);

    PXR_NS::TfToken context("USD");
    if (view)
    {
        auto viewport_view = view->get_viewport_view();
        if (viewport_view)
        {
            context = viewport_view->get_scene_context_type();
        }
    }
    Application::instance().set_active_view_scene_context(context);
}

void ApplicationUI::set_current_viewport_tool(IViewportToolContext* tool_context)
{
    if (m_current_viewport_tool)
    {
        delete m_current_viewport_tool;
    }
    m_current_viewport_tool = tool_context;
    Application::instance().dispatch_event(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED);
    if (m_active_view != nullptr)
    {
        m_active_view->get_gl_widget()->update();
        m_active_view->get_gl_widget()->update_cursor();
    }
}

void ApplicationUI::init_ui()
{
    m_main_window = new MainWindow;
    m_main_window->show();
    Application::instance().m_ui_available = true;
    CrashHandler::set_tag("ui_available", "yes");
    py_interp::run_init_ui();
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::AFTER_UI_LOAD);
    get_main_window()->load_panel_layout();

    // We register global escape shortcut which can call multiple registered callback to escape multiple long computations
    // used e.g. to cancel playback and render (which can possible by active at same time)
    // this means that escape could cancel all active computations
    QShortcut* shortcut = new QShortcut(Qt::Key_Escape, m_main_window);
    m_main_window->connect(shortcut, &QShortcut::activated,
                           [&] { Application::instance().m_event_dispatcher.dispatch(Application::EventType::UI_ESCAPE_KEY_ACTION); });
    shortcut->setContext(Qt::ApplicationShortcut);
}

std::string ApplicationUI::get_file_extensions()
{
    auto all_file_format_extensions_set = PXR_NS::SdfFileFormat::FindAllFileFormatExtensions();
    all_file_format_extensions_set.erase("sdf");

    std::vector<std::string> all_file_extensions;

    for (const auto& file_format_extension : all_file_format_extensions_set)
    {
        auto file_format = PXR_NS::SdfFileFormat::FindByExtension(file_format_extension);
        if (!file_format)
        {
            OPENDCC_WARN("Failed to get file extensions for \"{}\" file format.", file_format_extension);
            continue;
        }
        auto file_extensions = file_format->GetFileExtensions();
        for (const auto& file_extension : file_extensions)
        {
            std::ostringstream file_extension_stream;
            file_extension_stream << "*." << file_extension;
            all_file_extensions.push_back(file_extension_stream.str());
        }
    }

    std::ostringstream result_stream;
    result_stream << "*.usd *.usda *.usdc *.usdz;;";
    for (int i = 0; i < all_file_extensions.size(); ++i)
    {
        if (i)
        {
            result_stream << ";;";
        }
        result_stream << all_file_extensions[i];
    }
    std::string result = result_stream.str();

    return result;
}

std::vector<std::string> ApplicationUI::get_supported_languages() const
{
    std::vector<std::string> result;

    auto i18n = QDir(Application::instance().get_application_root_path().c_str());
    i18n.cd("i18n");

    QRegularExpression reg("i18n\\.(.*)\\.qm");
    for (const auto& entry : i18n.entryInfoList(QDir::Filter::Files, QDir::SortFlag::Name))
    {
        const auto match = reg.match(entry.fileName());
        if (!match.hasMatch())
        {
            continue;
        }
        const auto lang = match.captured(1);
        result.push_back(lang.toStdString());
    }
    return result;
}

bool ApplicationUI::set_ui_language(const std::string& language_code)
{
    if (m_translator)
    {
        m_translator->deleteLater();
    }
    m_translator = new QTranslator(qApp);
    auto& app = Application::instance();

    auto i18n = QDir(app.get_application_root_path().c_str());
    i18n.cd("i18n");
    const auto locale = QLocale(language_code.c_str());
    auto res = m_translator->load(locale, "i18n", ".", i18n.path());
    if (!res)
    {
        // Qt6 resolves this overload through QLocale::uiLanguages(), which does not reliably yield
        // the bare language code, so "i18n.en.qm" is missed where Qt5 found it. Retry the exact name.
        res = m_translator->load(QStringLiteral("i18n.") + QString::fromStdString(language_code), i18n.path());
    }
    if (!res)
    {
        OPENDCC_ERROR("Failed to load internationalization file for '{}' language.", locale.languageToString(locale.language()).toStdString());
        return false;
    }

    if (!QApplication::installTranslator(m_translator))
    {
        OPENDCC_ERROR("Failed to install QTranslator.");
        return false;
    }
    return true;
}

OPENDCC_NAMESPACE_CLOSE
