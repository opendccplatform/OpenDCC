// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/wrap_application.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/app/core/settings.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/app/viewport/viewport_scene_context.h"
#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include <pxr/base/tf/pyUtils.h>
#include <OpenColorIO/OpenColorIO.h>
#include "opendcc/ui/timeline_widget/timeline_widget.h"
#include <pxr/base/tf/pyResultConversions.h>

OPENDCC_NAMESPACE_OPEN
using namespace pybind11;

QMainWindow* get_main_window(Application* self)
{
    return ApplicationUI::instance().get_main_window();
}

void set_sound_display(Application* self, const std::string& filepath, const double frame_offset)
{
    MainWindow* main_window = ApplicationUI::instance().get_main_window();
    TimelineWidget* timeline = main_window->timeline_widget();
    timeline->set_sound_display(filepath, frame_offset);
}

void clear_sound_display(Application* self)
{
    MainWindow* main_window = ApplicationUI::instance().get_main_window();
    TimelineWidget* timeline = main_window->timeline_widget();
    timeline->clear_sound_display();
}

QSettings* get_ui_settings(Application* self)
{
    return ApplicationUI::instance().get_main_window()->get_settings();
}

void app_set_current_viewport_tool(Application* self, IViewportToolContext* tool)
{
    ApplicationUI::instance().set_current_viewport_tool(tool);
}

IViewportToolContext* app_get_current_viewport_tool(Application* self)
{
    return ApplicationUI::instance().get_current_viewport_tool();
}

static std::shared_ptr<ViewportView> get_active_view(Application* self)
{
    if (auto widget = ApplicationUI::instance().get_active_view())
    {
        return widget->get_viewport_view();
    }
    return nullptr;
}

static std::string get_file_extensions()
{
    return ApplicationUI::instance().get_file_extensions();
}

static std::vector<std::string> get_supported_languages(Application* self)
{
    (void)self;
    return ApplicationUI::instance().get_supported_languages();
}

static bool set_ui_language(Application* self, const std::string& lang_code)
{
    (void)self;
    return ApplicationUI::instance().set_ui_language(lang_code);
}

static tuple get_ocio_config(Application* self)
{
    namespace OCIO = OCIO_NAMESPACE;
    auto config = OCIO::GetCurrentConfig();
    if (!config)
    {
        return tuple();
    }

#if OCIO_VERSION_MAJOR < 2
    std::vector<std::string> rendering_spaces(config->getNumColorSpaces());
    for (int i = 0; i < rendering_spaces.size(); i++)
        rendering_spaces[i] = config->getColorSpaceNameByIndex(i);
#else
    const auto spaces = config->getColorSpaces(nullptr);
    std::vector<std::string> rendering_spaces(spaces->getNumColorSpaces());
    for (int i = 0; i < spaces->getNumColorSpaces(); i++)
        rendering_spaces[i] = spaces->getColorSpaceNameByIndex(i);
#endif

    const auto default_display = config->getDefaultDisplay();
    std::vector<std::string> view_transforms(config->getNumViews(default_display));
    for (int i = 0; i < view_transforms.size(); i++)
        view_transforms[i] = config->getView(default_display, i);

    std::string default_rendering_space;
    if (auto cs = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR))
        default_rendering_space = cs->getName();
    const std::string default_view = default_display ? config->getDefaultView(default_display) : std::string();

    return pybind11::make_tuple(rendering_spaces, view_transforms, default_rendering_space, default_view);
}

class PythonUsdEditsUndoBlock
{
public:
    explicit PythonUsdEditsUndoBlock()
        : m_block(nullptr)
    {
    }

    void Open()
    {
        PXR_NAMESPACE_USING_DIRECTIVE
        if (!TF_VERIFY(m_block == 0))
        {
            return;
        }
        m_block = new commands::UsdEditsUndoBlock();
    }

    void Close(object, object, object) { Close(); }

    void Close()
    {
        PXR_NAMESPACE_USING_DIRECTIVE
        if (!TF_VERIFY(m_block != nullptr))
        {
            return;
        }
        delete m_block;
        m_block = nullptr;
    }

    ~PythonUsdEditsUndoBlock() { delete m_block; }

private:
    commands::UsdEditsUndoBlock* m_block = nullptr;
};

void py_interp::bind::wrap_application(pybind11::module& m)
{
    using namespace pybind11;
    auto app_scope = class_<Application, std::unique_ptr<Application, nodelete>>(m, "Application")
                         .def_static("instance", &Application::instance, return_value_policy::reference)
                         .def("get_opendcc_version_string", &Application::get_opendcc_version_string)
                         .def("get_build_date", &Application::get_build_date)
                         .def("get_commit_hash", &Application::get_commit_hash)
                         .def("get_opendcc_version", &Application::get_opendcc_version)
                         .def("is_ui_available", &Application::is_ui_available)
                         .def("get_ui_settings", &get_ui_settings)
                         .def("get_settings", &Application::get_settings, return_value_policy::reference)
                         .def("get_ocio_config", &get_ocio_config)
                         .def("get_settings_path", &Application::get_settings_path)
                         .def("get_main_window", &get_main_window)
                         .def("get_undo_stack", &Application::get_undo_stack, return_value_policy::reference)
                         .def("set_current_time", &Application::set_current_time)
                         .def("set_current_viewport_tool", app_set_current_viewport_tool)
                         .def("get_current_viewport_tool", app_get_current_viewport_tool, return_value_policy::reference)
                         .def_static("get_usd_clipboard", &Application::get_usd_clipboard, return_value_policy::reference)
                         .def("get_current_time", &Application::get_current_time)
                         .def("get_prim_selection", &Application::get_prim_selection)
                         .def("set_prim_selection", &Application::set_prim_selection)
                         .def("get_selection", &Application::get_selection)
                         .def("set_selection", &Application::set_selection)
                         .def("get_selection_mode", &Application::get_selection_mode)
                         .def("set_selection_mode", &Application::set_selection_mode)
                         .def("clear_prim_selection", &Application::clear_prim_selection)
                         .def("register_event_callback",
                              [](Application* self, const Application::EventType& event_type, std::function<void()> callback) {
                                  self->register_event_callback(event_type, pybind_safe_callback(callback));
                              })
                         .def("register_event_callback",
                              [](Application* self, const std::string& event_type, std::function<void()> callback) {
                                  return self->register_event_callback(self->string_to_event_type(event_type), pybind_safe_callback(callback));
                              })
                         .def("unregister_event_callback", &Application::unregister_event_callback)
                         .def("unregister_event_callback",
                              [](Application* self, const std::string& event_type, Application::CallbackHandle handle) {
                                  return self->unregister_event_callback(self->string_to_event_type(event_type), handle);
                              })
                         .def("get_session", &Application::get_session, return_value_policy::reference)
                         .def("get_application_root_path", &Application::get_application_root_path)
                         .def("get_active_view", &get_active_view)
                         .def("set_active_view_scene_context", &Application::set_active_view_scene_context)
                         .def("get_active_view_scene_context", &Application::get_active_view_scene_context)
                         .def("set_sound_display", &set_sound_display)
                         .def("clear_sound_display", &clear_sound_display)
                         .def("set_layer_selection", &Application::set_layer_selection)
                         .def("get_layer_selection", &Application::get_layer_selection)
                         .def_static("set_app_config", &Application::set_app_config)
                         .def_static("get_app_config", &Application::get_app_config, return_value_policy::reference)
                         .def_static("get_file_extensions", &get_file_extensions)
                         .def("get_supported_languages", &get_supported_languages)
                         .def("set_ui_language", &set_ui_language)
                         .def("get_package_registry", &Application::get_package_registry);

    enum_<Application::SelectionMode>(app_scope, "SelectionMode")
        .value("POINTS", Application::SelectionMode::POINTS)
        .value("EDGES", Application::SelectionMode::EDGES)
        .value("FACES", Application::SelectionMode::FACES)
        .value("UV", Application::SelectionMode::UV)
        .value("INSTANCES", Application::SelectionMode::INSTANCES)
        .value("PRIMS", Application::SelectionMode::PRIMS)
        .value("COUNT", Application::SelectionMode::COUNT)
        .export_values();

    enum_<Application::EventType>(app_scope, "EventType")
        .value("SELECTION_CHANGED", Application::EventType::SELECTION_CHANGED)
        .value("SELECTION_MODE_CHANGED", Application::EventType::SELECTION_MODE_CHANGED)
        .value("ACTIVE_VIEW_SCENE_CONTEXT_CHANGED", Application::EventType::ACTIVE_VIEW_SCENE_CONTEXT_CHANGED)
        .value("CURRENT_STAGE_CHANGED", Application::EventType::CURRENT_STAGE_CHANGED)
        .value("SESSION_STAGE_LIST_CHANGED", Application::EventType::SESSION_STAGE_LIST_CHANGED)
        .value("BEFORE_CURRENT_STAGE_CLOSED", Application::EventType::BEFORE_CURRENT_STAGE_CLOSED)
        .value("EDIT_TARGET_CHANGED", Application::EventType::EDIT_TARGET_CHANGED)
        .value("EDIT_TARGET_DIRTINESS_CHANGED", Application::EventType::EDIT_TARGET_DIRTINESS_CHANGED)
        .value("CURRENT_TIME_CHANGED", Application::EventType::CURRENT_TIME_CHANGED)
        .value("CURRENT_VIEWPORT_TOOL_CHANGED", Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED)
        .value("AFTER_UI_LOAD", Application::EventType::AFTER_UI_LOAD)
        .value("UI_ESCAPE_KEY_ACTION", Application::EventType::UI_ESCAPE_KEY_ACTION)
        .value("LAYER_SELECTION_CHANGED", Application::EventType::LAYER_SELECTION_CHANGED)
        .value("BEFORE_APP_QUIT", Application::EventType::BEFORE_APP_QUIT)
        .export_values();

    class_<Application::CallbackHandle>(app_scope, "CallbackHandle");

    class_<PythonUsdEditsUndoBlock>(m, "UsdEditsUndoBlock")
        .def(init<>())
        .def("__enter__", &PythonUsdEditsUndoBlock::Open)
        .def("__exit__", overload_cast<object, object, object>(&PythonUsdEditsUndoBlock::Close))
        .def("enter", &PythonUsdEditsUndoBlock::Open)
        .def("exit", overload_cast<>(&PythonUsdEditsUndoBlock::Close))
        .def("exit", overload_cast<object, object, object>(&PythonUsdEditsUndoBlock::Close));
}
OPENDCC_NAMESPACE_CLOSE
