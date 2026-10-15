/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include <QtWidgets/QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <DockManager.h>
#include <DockWidget.h>
#include "opendcc/app/core/api.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/ui/timeline_widget/timeline_slider.h"
#include "opendcc/ui/logger_panel/logger_view.h"
#include "opendcc/app/core/settings.h"

class QCloseEvent;

OPENDCC_NAMESPACE_OPEN

class TimelineWidget;

/**
 * @brief The MainWindow class provides methods for
 * arranging the panel layouts and the main window widgets.
 *
 */
class OPENDCC_API MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow();

    /**
     * @brief Creates a new panel.
     *
     * @param panel_type The panel type to create.
     * @param floating Whether to create the panel floating or docked. (Default value = true)
     * @param parent_panel The widget to set for the newly created panel as parent. Nullptr by default.
     * @param dock_area The docking area of the panel to be created. By default, it is docked to the center.
     * @param site_index #unused parameter.
     * @return The CDockWidget or nullptr if failed.
     */
    ads::CDockWidget* create_panel(const std::string panel_type, bool floating = true, ads::CDockAreaWidget* parent_panel = nullptr,
                                   ads::DockWidgetArea dock_area = ads::CenterDockWidgetArea, int site_index = 0);

    /**
     * @brief Loads a layout from the specified QSettings.
     *
     * @param settings The QSettings which store information about
     * the widgets' layout as byte arrays.
     */
    void load_panel_layout(QSettings* settings = nullptr);
    void closeEvent(QCloseEvent* event) override;
    /**
     * @brief Returns the main window settings.
     *
     */
    QSettings* get_settings() { return m_settings; }
    /**
     * @brief Returns a pointer to the timeline widget.
     *
     */
    TimelineWidget* timeline_widget() { return m_timeline_widget; }

    /**
     * @brief Closes all widgets.
     *
     */
    void close_panels();
    /**
     * @brief Saves current layout using the specified path.
     *
     * @param path The path to the file where to store the current layout data.
     */
    void save_layout(const std::string& path);
    /**
     * @brief Loads a layout from the specified path.
     *
     * @param path The path to the file which stores layout widgets.
     */
    void load_layout(const std::string& path);
    /**
     * @brief Arranges widgets using the specified vector of
     * double values to use as proportions.
     *
     * @param dock_widget The CDockWidget the contents of which is to be arranged.
     * @param proportion The widget proportions. The size of this vector
     * must be equal to the splitter count in the dock area widget
     * of the specified ``dock_widget``.
     */
    void arrange_splitters(ads::CDockWidget* dock_widget, const std::vector<double>& proportion);

    // void init_timeline_ui(TimelineWidget* timeline_widget);

    /**
     * @brief Initializes the MainWindow timeline widget.
     */
    void init_timeline_ui();

Q_SIGNALS:
    void send_message(QString channel, base::logging::LogLevel log_level, QString msg);

private:
    void add_create_panel_btn(ads::CDockAreaWidget* dock_widget);
    void restore_widgets(const QByteArray& content);
    void update_timeline_samples();

    void cleanup_timeline_ui();

    LoggerManager* m_logger_panel_manager = nullptr;
    std::unique_ptr<base::logging::LoggingDelegate> m_status_bar_logging = nullptr;

    ads::CDockManager* m_main_container_widget;
    TimelineWidget* m_timeline_widget;
    TimelineSlider* m_timeline_slider;
    Settings::SettingChangedHandle m_timeline_playback_by_callback_id;
    Settings::SettingChangedHandle m_timeline_playback_mode_callback_id;
    Settings::SettingChangedHandle m_timeline_snap_callback_id;
    Settings::SettingChangedHandle m_timeline_keyframe_current_time_indicator_type_callback_id;
    Settings::SettingChangedHandle m_timeline_keyframe_display_type_callback_id;
    Settings::SettingChangedHandle m_timeline_background_type_callback_id;
    Settings::SettingChangedHandle m_timeline_subdivisions_callback_id;

    Session::StageChangedCallbackHandle m_timeline_stage_callback_id;
    Application::CallbackHandle m_timeline_current_stage_callback_id;
    Application::CallbackHandle m_timeline_selection_changed_callback_id;
    Application::CallbackHandle m_timeline_current_time_changed_callback_id;
    Session::CallbackHandle m_live_share_changed_cid;
    Application::CallbackHandle m_before_stage_closed_callback_id;
    Application::CallbackHandle m_escape_action_callback_id;
    QSettings* m_settings = nullptr;
};
OPENDCC_NAMESPACE_CLOSE
