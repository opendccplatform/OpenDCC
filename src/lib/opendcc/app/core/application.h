/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

#include "opendcc/app/core/api.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/logging/logging_delegate.h"
#include "opendcc/app/core/settings.h"
#include "opendcc/app/core/stage_watcher.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/rich_selection.h"
#include "opendcc/app/core/usd_clipboard.h"
#include "opendcc/base/crash_reporting/sentry_crash_handler.h"

#include "opendcc/base/app_config/config.h"
#include "opendcc/base/packaging/package_registry.h"
#include "opendcc/base/vendor/eventpp/eventdispatcher.h"

#include "opendcc/app/ui/logger/usd_logging_delegate.h"

#include "opendcc/usd/layer_tree_watcher/layer_tree_watcher.h"
#include "opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

#include <thread>
#include <future>
#include <string>

OPENDCC_NAMESPACE_OPEN

class Session;
class Settings;
class UsdLoggingDelegate;

template <class T>
class Ramp;

namespace commands
{
    class UndoStack;
};

namespace ipc
{
    class CommandServer;
};

/**
 * @brief The Application class provides various features:
 *   - Defines and handles the application events.
 *   - Manages selection and the selection tools.
 *   - Sets global time.
 *   - Gets the global information about the application and it's configuration.
 *
 * Python example:
 * \code {.py}
 * Application.instance().get_prim_selection()
 * \endcode
 * C++ example:
 * \code {.cpp}
 * Application::instance().get_prim_selection();
 * \endcode
 */
class Application
{
public:
    /**
     * @brief Defines the active selection mode.
     *
     * Any changes of the selection mode trigger
     *``EventType::SELECTION_MODE_CHANGED`` and
     * ``EventType::SELECTION_CHANGED`` events.
     */
    enum class SelectionMode : uint8_t
    {
        /**
         * @brief Select points.
         */
        POINTS = 0,
        /**
         * @brief Select edges.
         */
        EDGES = 1,
        /**
         * @brief Select faces.
         */
        FACES = 2,
        /**
         * @brief Select uv.
         */
        UV = 3,
        /**
         * @brief Select instances.
         */
        INSTANCES = 4,
        /**
         * @brief Select prims.
         */
        PRIMS = 5,
        /**
         * @brief Selection mode count.
         */
        COUNT = 6,
    };

    /**
     * @brief Defines application event types.
     */
    enum class EventType
    {
        /**
         * @brief Triggered when selection is changed.
         */
        SELECTION_CHANGED,
        /**
         * @brief Triggered when selection mode is changed.
         */
        SELECTION_MODE_CHANGED,
        /**
         * @brief Triggered when another Viewport panel is selected.
         */
        ACTIVE_VIEW_CHANGED,
        /**
         * @brief Triggered when the scene view context is changed.
         * The scene view context signifies the source of data used
         * by the current focused Viewport to display the scene.
         *
         */
        ACTIVE_VIEW_SCENE_CONTEXT_CHANGED,
        /**
         * @brief Triggered when current stage is changed.
         */
        CURRENT_STAGE_CHANGED,
        /**
         * @brief Triggered when edit target is changed.
         */
        EDIT_TARGET_CHANGED,
        /**
         * @brief Triggered when edit target has unsaved changes.
         */
        EDIT_TARGET_DIRTINESS_CHANGED,
        /**
         * @brief Triggered before closing current stage.
         */
        BEFORE_CURRENT_STAGE_CLOSED,
        /**
         * @brief Triggered when session stage list is changed.
         */
        SESSION_STAGE_LIST_CHANGED,
        /**
         * @brief Triggered when current time is changed.
         */
        CURRENT_TIME_CHANGED,
        /**
         * @brief Triggered when viewport tool is changed.
         */
        CURRENT_VIEWPORT_TOOL_CHANGED,
        /**
         * @brief Triggered after loading the application UI.
         *
         * @note This EventType is triggered only once on launching the application.
         */
        AFTER_UI_LOAD,
        /**
         * @brief Triggered when the escape key is pressed and the binded action is done.
         */
        UI_ESCAPE_KEY_ACTION,
        /**
         * @brief Triggered when another layer is selected.
         * It is used for the classes which interact with the layers' content.
         *
         *      For example:
         *          - UsdDetailsView;
         *          - LayerView;
         *          - UsdLayerText.
         *
         * @note It is also used for synchronization of the state of several LayerView instances.
         */
        LAYER_SELECTION_CHANGED,

        /**
         * @brief Triggered when the application is about to be closed. This event type is useful for serialization or uninitialization
         * of disposable resources.
         */
        BEFORE_APP_QUIT
    };

    /**
     * @brief Event dispatcher for events.
     */
    using EventDispatcher = eventpp::EventDispatcher<EventType, void()>;
    /**
     * @brief Callback handle for events.
     */
    using CallbackHandle = eventpp::EventDispatcher<EventType, void()>::Handle;

    /**
     * @brief Returns an Application instance.
     *
     */
    OPENDCC_API static Application& instance();
    /**
     * @brief Deletes the Application instance.
     */
    static void delete_instance();
    /**
     * @brief Initializes the Python interpreter with the specified command line arguments.
     *
     * @param args Python command line arguments.
     */
    OPENDCC_API void init_python(std::vector<std::string>& args);
    /**
     * @brief Run startup.init function
     *
     */
    OPENDCC_API void run_startup_init();
    /**
     * @brief Initializes the Python shell with the specified arguments.
     *
     */
    OPENDCC_API void init_python_shell();

    /**
     * @brief Checks whether the UI is available.
     *
     * @return true if the UI is available, false otherwise.
     */
    bool is_ui_available() { return m_ui_available; }

    /**
     * @brief Returns the clipboard
     *
     */
    OPENDCC_API static UsdClipboard& get_usd_clipboard();

    /**
     * @brief Returns the version of the OpenDCC as a string using the following format:
     *      'major.minor.patch.tweak'
     *
     */
    OPENDCC_API const std::string get_opendcc_version_string();
    /**
     * @brief Returns the version of the OpenDCC as a tuple of int values using the following format:
     *      <major, minor, patch, tweak>
     *
     */
    OPENDCC_API std::tuple<int, int, int, int> get_opendcc_version();
    /**
     * @brief Returns the application build date as a string using the following format:
     *      'mmm dd yyyy'
     *
     */
    OPENDCC_API const std::string get_build_date();
    /**
     * @brief Returns the commit hash of the current build as a string.
     *
     */
    OPENDCC_API const std::string get_commit_hash();
    /**
     * @brief Runs a Python script at the specified path.
     *
     * @param filepath The path to the Python script to run.
     * @return The script completion code.
     */
    OPENDCC_API int run_python_script(const std::string& filepath);

    /**
     * @brief Runs inline Python in the main module.
     * @return The execution completion code.
     */
    OPENDCC_API int run_python_command(const std::string& code);

    /**
     * @brief Returns the application root path.
     *
     */
    OPENDCC_API std::string get_application_root_path() { return m_application_root_path; }

    /**
     * @brief Returns the path of the selected prims.
     *
     */
    OPENDCC_API PXR_NS::SdfPathVector get_prim_selection();
    /**
     * @brief Clears the current selection and selects the prims
     * specified in the new_selection.
     *
     * @param new_selection The paths of the prims to select.
     *
     * @note Sends the``EventType::SELECTION_CHANGED`` if the selection mode
     * is set to ``selection_mode == SelectionMode::PRIMS``.
     *
     * Otherwise, initially sets the selection mode to ``selection_mode == SelectionMode::PRIMS``
     * and then sends the ``EventType::SELECTION_MODE_CHANGED`` and ``EventType::SELECTION_CHANGED`` events.
     */
    OPENDCC_API void set_prim_selection(const PXR_NS::SdfPathVector& new_selection);
    /**
     * @brief Clears the selection state and then triggers the ``EventType::SELECTION_CHANGED`` event.
     *
     */
    OPENDCC_API void clear_prim_selection();

    /**
     * @brief Returns current selection list.
     *
     */
    OPENDCC_API SelectionList get_selection() const;
    /**
     * @brief Defines the selection state and sends the ``EventType::SELECTION_CHANGED`` event.
     *
     * @param selection_state Reference to the selection list to set.
     */
    OPENDCC_API void set_selection(const SelectionList& selection_state);

    /**
     * @brief Subscribes to the specified``event_type``.
     *
     * @param event_type Type of the event to which the callback is to be set.
     * @param callback The function to set as a callback.
     * @return The callback handle for the event which is required for unsubscription from the event.
     */
    OPENDCC_API CallbackHandle register_event_callback(const EventType& event_type, const std::function<void()> callback);
    /**
     * @brief Removes a callback from the specified event.
     *
     * @param event_type Type of the event from which the callback is to be removed.
     * @param handle The callback handle reference.
     */
    OPENDCC_API void unregister_event_callback(const EventType& event_type, CallbackHandle& handle);
    /**
     * @brief Sends the events of the specified ``EventType`` explicitly.
     *
     * @param event_type The type of the event to dispatch.
     */
    OPENDCC_API void dispatch_event(const EventType& event_type);
    /**
     * @brief Converts an event type to a string value.
     *
     *      Example:
     *          EventType::SELECTION_MODE_CHANGED => "selection_mode_changed".
     *
     * @param event_type The event type to return as a string.
     */
    OPENDCC_API const std::string event_type_to_string(const EventType& event_type);
    /**
     * @brief Converts a string event name to the corresponding EventType `enum`.
     *
     *      Example:
     *          "selection_mode_changed" => EventType::SELECTION_MODE_CHANGED.
     *
     * @param event_type string value which is to be converted to the event type.
     * @return The converted event type.
     */
    OPENDCC_API EventType string_to_event_type(const std::string& event_type);
    /**
     * @brief Returns a pointer to the current session.
     *
     */
    OPENDCC_API Session* get_session();

    /**
     * @brief Sets the current global time to the specified value.
     *
     * @param time The double value to set as the global time.
     */
    OPENDCC_API void set_current_time(double time);

    /**
     * @brief Returns the current global time.
     *
     */
    double get_current_time() const { return m_current_time; }

    /**
     * @brief Returns a pointer to the application settings.
     *
     *
     */
    Settings* get_settings() { return m_settings; }

    /**
     * @brief Returns a path to the application settings directory.
     *
     * For Windows: %USERPROFILE%/.dcc
     * For Linux and MacOS: $HOME/.dcc
     *
     */
    OPENDCC_API std::string get_settings_path();

    /**
     * @brief Returns the undo stack.
     *
     */
    commands::UndoStack* get_undo_stack() { return m_undo_stack; }
    /**
     * @brief Returns the current rich selection (an active selection with the soft selection enabled).
     *
     */
    OPENDCC_API const RichSelection& get_rich_selection() const;
    /**
     * @brief Sets the rich selection, then triggers ``EventType::SELECTION_CHANGED``.
     *
     * @param rich_selection The const RichSelection object reference which is to be set.
     */
    OPENDCC_API void set_rich_selection(const RichSelection& rich_selection);
    /**
     * @brief Sets the specified prims highlighted.
     *
     * @param prims Reference to the vector of prims to set highlighted.
     */
    OPENDCC_API void set_highlighted_prims(const PXR_NS::SdfPathVector& prims);
    /**
     * @brief Returns a list of paths of the prims which have any subcomponents (points, faces, edges etc.) available for the selection.
     *
     */
    OPENDCC_API const PXR_NS::SdfPathVector& get_highlighted_prims() const;
    /**
     * @brief Toggles soft selection mode.
     *
     * @param enable Whether to enable soft selection mode.
     */
    OPENDCC_API void enable_soft_selection(bool enable);
    /**
     * @brief Checks whether soft selection mode is enabled.
     *
     * @return true if soft selection mode is enabled, false otherwise.
     */
    OPENDCC_API bool is_soft_selection_enabled() const;
    /**
     * @brief Returns current selection mode.
     *
     */
    OPENDCC_API SelectionMode get_selection_mode() const;
    /**
     * @brief Sets the selection mode and triggers the ``EventType::SELECTION_MODE_CHANGED``
     * and ``EventType::SELECTION_CHANGED`` events.
     *
     * @param mask The desired selection mode:
     *      SelectionList::POINTS
     *      SelectionList::EDGES;
     *      SelectionList::ELEMENTS;
     *      SelectionList::INSTANCES.
     */
    OPENDCC_API void set_selection_mode(SelectionMode mask);
    /**
     * @brief Sets the active view scene context.
     *
     * @param token The token reference.
     */
    OPENDCC_API void set_active_view_scene_context(const PXR_NS::TfToken& token);
    /**
     * @brief Returns the active context token.
     *
     * @return TfToken of the current context.
     */
    OPENDCC_API PXR_NS::TfToken get_active_view_scene_context() const;

    /**
     * @brief Sets the layer selection.
     *
     * @param set The reference to the layer handle set.
     *
     * @note If the current selection is different from the passed selection, also triggers ``EventType::LAYER_SELECTION_CHANGED``.
     */
    OPENDCC_API void set_layer_selection(const PXR_NS::SdfLayerHandleSet& set);
    /**
     * @brief Returns the current layer selection.
     *
     */
    OPENDCC_API PXR_NS::SdfLayerHandleSet get_layer_selection();

    /**
     * @brief Sets a configuration for the current application.
     *
     * @param app_config The application configuration path.
     */
    OPENDCC_API static void set_app_config(const ApplicationConfig& app_config);
    /**
     * @brief Returns the current configuration of the application.
     *
     */
    OPENDCC_API static const ApplicationConfig& get_app_config();

    /**
     * @brief initialize extensions.
     */
    OPENDCC_API void initialize_extensions();

    /**
     * @brief forcefully uninitialize extensions. Also called in destructor.
     */
    OPENDCC_API void uninitialize_extensions();

    OPENDCC_API static void create_command_server();
    OPENDCC_API static void destroy_command_server();

    OPENDCC_API void update_render_control();
    OPENDCC_API std::shared_ptr<PackageRegistry> get_package_registry() const;

private:
    friend class ApplicationUI;
    friend class Session;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Application();
    ~Application();
    static Application* m_instance;

    std::unique_ptr<UsdLoggingDelegate> m_usd_logging_delegate = nullptr;
    std::unique_ptr<LoggingDelegate> m_sentry_logging_delegate = nullptr;

    Session* m_session;
    std::string m_application_root_path;
    EventDispatcher m_event_dispatcher;
    Settings* m_settings = nullptr;
    commands::UndoStack* m_undo_stack = nullptr;
    RichSelection m_active_rich_selection;
    SelectionMode m_selection_mode = SelectionMode::PRIMS;
    bool m_enable_soft_selection = false;
    bool m_soft_selection_settings_changed = false;

    std::unordered_map<SelectionMode, RichSelection> m_per_mode_rich_selection;
    SelectionList m_global_selection_list;
    SelectionList m_active_selection_list;
    PXR_NS::SdfPathVector m_highlight_selection_prims;

    std::unordered_map<std::string, Settings::SettingChangedHandle> m_setting_changed_cids;
    double m_current_time;

    bool m_ui_available = false;
    std::string m_settings_path;
    PXR_NS::TfToken m_active_view_context_type = PXR_NS::TfToken("USD");

    PXR_NS::SdfLayerHandleSet m_layer_selection;
    static ApplicationConfig m_app_config;

    static std::shared_ptr<ipc::CommandServer> s_server;
    std::shared_ptr<PackageRegistry> m_package_registry;
};

OPENDCC_NAMESPACE_CLOSE
