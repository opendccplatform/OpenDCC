/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/app/core/api.h"
#include "opendcc/opendcc.h"
#include "opendcc/base/vendor/eventpp/eventdispatcher.h"
#include <QApplication>

class QTranslator;
OPENDCC_NAMESPACE_OPEN

class MainWindow;
class ViewportWidget;
class IViewportToolContext;

inline QString i18n(const char* context, const char* key, const char* disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate(context, key, disambiguation, n);
}

/**
 * @brief The ApplicationUI class allows to manage the QT-based user interface.
 */
class ApplicationUI
{
public:
    /**
     * @brief Build a new ApplicationUI instance.
     *
     * @return ApplicationUI instance.
     */
    OPENDCC_API static ApplicationUI& instance();
    /**
     * @brief Deletes the ApplicationUI instance.
     */
    static void delete_instance();
    /**
     * @brief Gets the pointer to the main window.
     *
     * @return The main window.
     */
    OPENDCC_API MainWindow* get_main_window() { return m_main_window; }

    /**
     * @brief Sets specified view as active.
     *
     * @param view The viewport widget to set active.
     */
    OPENDCC_API void set_active_view(ViewportWidget* view);
    /**
     * @brief Gets the currently active view widget.
     *
     * @return The active viewport widget.
     */
    OPENDCC_API ViewportWidget* get_active_view();
    /**
     * @brief Sets the active viewport tool.
     *
     * @param tool_context The context of the viewport tool.
     */
    OPENDCC_API void set_current_viewport_tool(IViewportToolContext* tool_context);

    /**
     * @brief Get the current viewport tool.
     *
     * @return The current viewport tool.
     */
    OPENDCC_API IViewportToolContext* get_current_viewport_tool() { return m_current_viewport_tool; }

    /**
     * @brief Initializes UI.
     */
    OPENDCC_API void init_ui();

    /**
     * @brief Gets all file format extensions for the Qt's file browser.
     *
     * @return std::string that is a list of file extensions. It uses "*.usd *.usda *.usdc *.usdz;;" as a header to make it more user-friendly.
     */
    OPENDCC_API std::string get_file_extensions();

    OPENDCC_API std::vector<std::string> get_supported_languages() const;

    OPENDCC_API bool set_ui_language(const std::string& language_code);

private:
    ApplicationUI(const ApplicationUI&) = delete;
    ApplicationUI& operator=(const ApplicationUI&) = delete;

    ApplicationUI();
    ~ApplicationUI();

    friend class ViewportWidget;
    void deactivate_view(ViewportWidget* view);
    void init_ocio();

    static ApplicationUI* m_instance;

    MainWindow* m_main_window = nullptr;
    ViewportWidget* m_active_view = nullptr;
    IViewportToolContext* m_current_viewport_tool = nullptr;

    QTranslator* m_translator = nullptr;
};
OPENDCC_NAMESPACE_CLOSE
