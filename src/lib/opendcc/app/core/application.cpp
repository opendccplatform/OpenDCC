// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/app/core/application.h"

#include "opendcc/base/defines.h"
#include "opendcc/base/utils/process.h"
#include "opendcc/base/app_version.h"
#include "opendcc/base/ipc_commands_api/server.h"
#include "opendcc/base/ipc_commands_api/server_registry.h"
#include "opendcc/base/ipc_commands_api/command_registry.h"
#include "opendcc/base/vendor/eventpp/utilities/counterremover.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logging_utils.h"
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"
#include "opendcc/base/packaging/filesystem_package_provider.h"
#include "opendcc/base/packaging/toml_parser.h"

#include "opendcc/ui/common_widgets/ramp.h"

#include "opendcc/app/core/session.h"
#include "opendcc/app/core/py_interp.h"
#include "opendcc/app/core/undo/stack.h"

#include "opendcc/app/ui/main_window.h"

#include "opendcc/app/viewport/usd_render.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/usd_render_control.h"

#include "opendcc/render_system/render_system.h"
#include "opendcc/render_system/render_factory.h"

#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/error.h>
#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/arch/systemInfo.h>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdUtils/stageCache.h>
#include <pxr/usd/usd/stageCacheContext.h>
#include <pxr/usd/sdr/registry.h>

#include <QDir>
#include <QLibraryInfo>

#include <string>
#include <algorithm>
#include <stdexcept>
#include "opendcc/app/core/sentry_logging_delegate.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("Application");

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    template <class T>
    void register_settings_pxr_vector()
    {
        static_assert(GfIsGfVec<T>::value, "Specified type is not GfVec");
        Settings::register_type<T>(
            [](const nonstd::any& val) {
                const auto vec_val = nonstd::any_cast<T>(val);
                Json::Value result(Json::ValueType::arrayValue);
                result.resize(T::dimension);
                for (int i = 0; i < T::dimension; i++)
                    result[i] = static_cast<double>(vec_val[i]);
                return result;
            },
            [](const Json::Value& val) {
                if (!val.isArray())
                    return nonstd::any();

                T result;
                for (int i = 0; i < T::dimension; i++)
                {
                    if (!val[i].isDouble())
                        return nonstd::any();
                    result[i] = val[i].asDouble();
                }
                return nonstd::make_any<T>(std::move(result));
            });
    }
    void register_extra_settings_types()
    {
        register_settings_pxr_vector<GfVec2i>();
        register_settings_pxr_vector<GfVec2h>();
        register_settings_pxr_vector<GfVec2f>();
        register_settings_pxr_vector<GfVec2d>();
        register_settings_pxr_vector<GfVec3i>();
        register_settings_pxr_vector<GfVec3h>();
        register_settings_pxr_vector<GfVec3f>();
        register_settings_pxr_vector<GfVec3d>();
        register_settings_pxr_vector<GfVec4i>();
        register_settings_pxr_vector<GfVec4h>();
        register_settings_pxr_vector<GfVec4f>();
        register_settings_pxr_vector<GfVec4d>();

        Settings::register_type<VtValue>(
            [](const nonstd::any& val) {
                auto vt_val = nonstd::any_cast<VtValue>(val);
                if (vt_val.IsHolding<bool>())
                {
                    return Json::Value(vt_val.UncheckedGet<bool>());
                }
                else if (vt_val.CanCast<double>())
                {
                    vt_val.Cast<double>();
                    return Json::Value(vt_val.UncheckedGet<double>());
                }
                else if (vt_val.CanCast<std::string>())
                {
                    vt_val.Cast<std::string>();
                    return Json::Value(vt_val.UncheckedGet<std::string>());
                }
                // TODO: arrays
                return Json::Value();
            },
            [](const Json::Value& val) {
                if (val.isBool())
                    return nonstd::make_any<VtValue>(VtValue(val.asBool()));
                else if (val.isInt())
                    return nonstd::make_any<VtValue>(VtValue(val.asInt()));
                else if (val.isDouble())
                    return nonstd::make_any<VtValue>(VtValue(val.asDouble()));
                else if (val.isString())
                    return nonstd::make_any<VtValue>(VtValue(val.asString()));
                // TODO: arrays
                return nonstd::any();
            });
    }
};
static SelectionList::SelectionMask convert_to_selection_mask(Application::SelectionMode mode)
{
    using SelectionMode = Application::SelectionMode;
    SelectionList::SelectionMask mask = SelectionList::PROPERTIES;
    switch (mode)
    {
    case SelectionMode::POINTS:
        mask |= SelectionList::POINTS;
        break;
    case SelectionMode::EDGES:
        mask |= SelectionList::EDGES;
        break;
    case SelectionMode::FACES:
        mask |= SelectionList::ELEMENTS;
        break;
    case SelectionMode::UV:
        mask = SelectionList::NONE;
        break;
    case SelectionMode::INSTANCES:
        mask |= SelectionList::INSTANCES;
        break;
    }
    return mask;
}

ApplicationConfig Application::m_app_config;

Application* Application::m_instance = nullptr;

Application& Application::instance()
{
    if (!m_instance)
    {
        m_instance = new Application();
    }
    return *m_instance;
}

void Application::delete_instance()
{
    if (m_instance)
    {
        delete m_instance;
        m_instance = nullptr;
    }
}

const std::string Application::event_type_to_string(const EventType& event_type)
{
    std::string result;
    switch (event_type)
    {
    case EventType::SELECTION_MODE_CHANGED:
        result = "selection_mode_changed";
        break;
    case EventType::SELECTION_CHANGED:
        result = "selection_changed";
        break;
    case EventType::CURRENT_TIME_CHANGED:
        result = "current_time_changed";
        break;
    case EventType::CURRENT_STAGE_CHANGED:
        result = "current_stage_changed";
        break;
    case EventType::BEFORE_CURRENT_STAGE_CLOSED:
        result = "before_current_stage_closed";
        break;
    case EventType::EDIT_TARGET_CHANGED:
        result = "edit_target_changed";
        break;
    case EventType::EDIT_TARGET_DIRTINESS_CHANGED:
        result = "edit_target_dirtiness_changed";
        break;
    case EventType::SESSION_STAGE_LIST_CHANGED:
        result = "session_stage_list_changed";
        break;
    case EventType::CURRENT_VIEWPORT_TOOL_CHANGED:
        result = "current_viewport_tool_changed";
        break;
    case EventType::AFTER_UI_LOAD:
        result = "after_ui_load";
        break;
    case EventType::ACTIVE_VIEW_CHANGED:
        result = "active_view_changed";
        break;
    case EventType::ACTIVE_VIEW_SCENE_CONTEXT_CHANGED:
        result = "active_view_scene_context_changed";
        break;
    case EventType::UI_ESCAPE_KEY_ACTION:
        result = "ui_escape_key_action";
        break;
    case EventType::BEFORE_APP_QUIT:
        result = "before_app_quit";
        break;
    case EventType::LAYER_SELECTION_CHANGED:
        result = "layer_selection_changed";
        break;
    default:
        break;
    }
    return result;
}

OPENDCC_API Application::EventType Application::string_to_event_type(const std::string& event_type)
{
    static std::unordered_map<std::string, EventType> types_map;
    types_map["selection_mode_changed"] = EventType::SELECTION_MODE_CHANGED;
    types_map["selection_changed"] = EventType::SELECTION_CHANGED;
    types_map["current_time_changed"] = EventType::CURRENT_TIME_CHANGED;
    types_map["current_stage_changed"] = EventType::CURRENT_STAGE_CHANGED;
    types_map["before_current_stage_closed"] = EventType::BEFORE_CURRENT_STAGE_CLOSED;
    types_map["edit_target_changed"] = EventType::EDIT_TARGET_CHANGED;
    types_map["edit_target_dirtiness_changed"] = EventType::EDIT_TARGET_DIRTINESS_CHANGED;
    types_map["session_stage_list_changed"] = EventType::SESSION_STAGE_LIST_CHANGED;
    types_map["current_viewport_tool_changed"] = EventType::CURRENT_VIEWPORT_TOOL_CHANGED;
    types_map["before_app_quit"] = EventType::BEFORE_APP_QUIT;
    types_map["after_ui_load"] = EventType::AFTER_UI_LOAD;
    types_map["active_view_changed"] = EventType::ACTIVE_VIEW_CHANGED;
    types_map["active_view_scene_context_changed"] = EventType::ACTIVE_VIEW_SCENE_CONTEXT_CHANGED;
    types_map["ui_escape_key_action"] = EventType::UI_ESCAPE_KEY_ACTION;
    types_map["layer_selection_changed"] = EventType::LAYER_SELECTION_CHANGED;

    auto find_it = types_map.find(event_type);
    if (find_it != types_map.end())
    {
        return find_it->second;
    }

    throw std::invalid_argument("Unknown application event: " + event_type);
}

Session* Application::get_session()
{
    return m_session;
}

Application::Application()
{
#ifdef OPENDCC_OS_WINDOWS
    // Set maximum possible opened file descriptors
    const int min_open_file_limit = _getmaxstdio();
    constexpr int max_open_file_limit = 8192;
    constexpr int step_open_files_limit = 512;
    for (int i = max_open_file_limit; i > min_open_file_limit; i -= step_open_files_limit)
    {
        if (_setmaxstdio(i) != -1)
        {
            break;
        }
    }
#endif
    m_instance = this;

    auto app_path = QString(ArchGetExecutablePath().c_str());
    m_settings = new Settings(get_settings_path() + "settings.json");
    register_extra_settings_types();
    const auto undo_stack_size = m_settings->get("undo.finite", false) ? m_settings->get("undo.stack_size", 100) : 0;
    m_undo_stack = new commands::UndoStack(undo_stack_size);
    m_undo_stack->set_enabled(m_settings->get("undo.enabled", true));
#ifdef OPENDCC_OS_MAC
    auto base_dir = QDir(app_path);
    base_dir.cdUp();
    base_dir.cdUp();
    base_dir.cd("Resources");
#else
    QDir base_dir(app_path);
    base_dir.cdUp();
    base_dir.cdUp();
#endif

    m_application_root_path = base_dir.path().toStdString();
    qputenv("DCC_LOCATION", base_dir.path().toStdString().c_str());
    auto mtlx_path = base_dir;
    mtlx_path.cd("materialx/libraries");
    auto mtlx_stdlib_search = qEnvironmentVariable("PXR_MTLX_STDLIB_SEARCH_PATHS");
    mtlx_stdlib_search.append(ARCH_PATH_LIST_SEP + mtlx_path.path());
    qputenv("PXR_MTLX_STDLIB_SEARCH_PATHS", mtlx_stdlib_search.toStdString().c_str());
    m_package_registry = std::make_shared<PackageRegistry>();
    auto package_provider = std::make_shared<FileSystemPackageProvider>();
    package_provider->add_path((m_application_root_path + "/packages/*").c_str());
    package_provider->register_package_parser("toml", std::make_shared<TOMLParser>());
    m_package_registry->add_package_provider(package_provider);
#if defined(OPENDCC_OS_WINDOWS)
    m_package_registry->define_token("APP_LIB_DIR", m_application_root_path + "/bin");
#else
    m_package_registry->define_token("APP_LIB_DIR", m_application_root_path + "/lib");
#endif
    m_package_registry->define_token("APP_ROOT_DIR", m_application_root_path);

    if (!get_app_config().is_valid())
    {
        auto configs_dir = base_dir;
        configs_dir.cd("configs");
        const auto app_config = ApplicationConfig(configs_dir.filePath("default.toml").toLocal8Bit().toStdString());
        Application::set_app_config(app_config);
    }

    m_current_time = 1.0;
    m_session = new Session;
    register_event_callback(EventType::CURRENT_TIME_CHANGED, [this]() {
        get_session()->update_current_stage_bbox_cache_time();
        get_session()->update_current_stage_xform_cache_time();
    });

    register_event_callback(EventType::CURRENT_STAGE_CHANGED, [this]() { clear_prim_selection(); });

    register_event_callback(EventType::BEFORE_CURRENT_STAGE_CLOSED, [this]() { clear_prim_selection(); });

    m_usd_logging_delegate = std::make_unique<UsdLoggingDelegate>();
    PXR_NS::TfDiagnosticMgr::GetInstance().AddDelegate(m_usd_logging_delegate.get());
    if (CrashHandler::is_enabled())
    {
        m_sentry_logging_delegate = std::make_unique<SentryLoggingDelegate>();
    }

    auto update_rich_selection = [this](const std::string&, const Settings::Value&, Settings::ChangeType) {
        m_active_rich_selection.update();
        m_soft_selection_settings_changed = true;
        m_event_dispatcher.dispatch(Application::EventType::SELECTION_CHANGED);
    };
    m_setting_changed_cids["soft_selection.falloff_radius"] =
        m_settings->register_setting_changed("soft_selection.falloff_radius", update_rich_selection);
    m_setting_changed_cids["soft_selection.falloff_mode"] =
        m_settings->register_setting_changed("soft_selection.falloff_mode", update_rich_selection);
    m_setting_changed_cids["soft_selection.enable_color"] =
        m_settings->register_setting_changed("soft_selection.enable_color", update_rich_selection);
    m_setting_changed_cids["soft_selection.falloff_curve"] =
        m_settings->register_setting_changed("soft_selection.falloff_curve", update_rich_selection);
    m_setting_changed_cids["soft_selection.falloff_color"] =
        m_settings->register_setting_changed("soft_selection.falloff_color", update_rich_selection);
}

void Application::initialize_extensions()
{
    PlugRegistry::GetInstance().RegisterPlugins(Application::instance().get_application_root_path() + "/plugin/usd");

    auto dcc_plugins = PlugRegistry::GetInstance().RegisterPlugins(Application::instance().get_application_root_path() + "/plugin/opendcc");
    for (auto& plugin : dcc_plugins)
    {
        // make sure opendcc_core is loaded
        if (plugin->GetName() == "opendcc_core")
            plugin->Load();
    }

    m_package_registry->fetch_packages(true);
}

void Application::uninitialize_extensions()
{
    m_event_dispatcher.dispatch(EventType::BEFORE_APP_QUIT);
}

Application::~Application()
{
    uninitialize_extensions();

    PXR_NS::TfDiagnosticMgr::GetInstance().RemoveDelegate(m_usd_logging_delegate.get());
    if (m_session)
    {
        delete m_session;
    }
    delete m_settings;
    delete m_undo_stack;
}

void Application::init_python(std::vector<std::string>& args)
{
    py_interp::init_py_interp(args);
    PythonCommandInterface::instance().register_conversion<SelectionList>("dcc_core.SelectionList");
    PythonCommandInterface::instance().register_conversion<UsdStageWeakPtr>("Usd.Stage");
    PythonCommandInterface::instance().register_conversion<UsdPrim>("Usd.Prim");
    PythonCommandInterface::instance().register_conversion<std::vector<UsdPrim>>("Usd.PrimVector");
    PythonCommandInterface::instance().register_conversion<TfToken>("Tf.Token");
    PythonCommandInterface::instance().register_conversion<SdfPath>("Sdf.Path");
    PythonCommandInterface::instance().register_conversion<std::vector<SdfPath>>("Sdf.PathVector");
    PythonCommandInterface::instance().register_conversion<GfVec2f>("Gf.Vec2f");
    PythonCommandInterface::instance().register_conversion<GfVec3f>("Gf.Vec3f");
    PythonCommandInterface::instance().register_conversion<GfVec4f>("Gf.Vec4f");
    PythonCommandInterface::instance().register_conversion<GfVec2d>("Gf.Vec2d");
    PythonCommandInterface::instance().register_conversion<GfVec3d>("Gf.Vec3d");
    PythonCommandInterface::instance().register_conversion<GfVec4d>("Gf.Vec4d");
    PythonCommandInterface::instance().register_conversion<GfRotation>("Gf.Rotation");
    PythonCommandInterface::instance().register_conversion<GfMatrix3f>("Gf.Matrix3f");
    PythonCommandInterface::instance().register_conversion<GfMatrix4f>("Gf.Matrix4f");
    PythonCommandInterface::instance().register_conversion<GfMatrix3d>("Gf.Matrix3d");
    PythonCommandInterface::instance().register_conversion<GfMatrix4d>("Gf.Matrix4d");
    CommandRegistry::register_command_interface(PythonCommandInterface::instance());
}

void Application::run_startup_init()
{
    py_interp::run_init();
}

SdfPathVector Application::get_prim_selection()
{
    return m_active_selection_list.get_selected_paths();
}

void Application::set_prim_selection(const SdfPathVector& new_selection)
{
    if (m_selection_mode == SelectionMode::PRIMS)
    {
        m_active_selection_list.set_selected_paths(new_selection);
        m_active_rich_selection = RichSelection();
        m_event_dispatcher.dispatch(EventType::SELECTION_CHANGED);
    }
    else
    {
        m_active_selection_list.add_prims(new_selection);
        set_selection_mode(SelectionMode::PRIMS);
    }
}

void Application::set_selection(const SelectionList& selection_state)
{
    m_active_selection_list = selection_state;
    m_event_dispatcher.dispatch(EventType::SELECTION_CHANGED);
}

Application::CallbackHandle Application::register_event_callback(const EventType& event_type, const std::function<void()> callback)
{
    // this event occurs only once during app load or closes, so we need to remove callbacks
    // in case someone call dispatch second time
    if (event_type == EventType::AFTER_UI_LOAD || event_type == EventType::BEFORE_APP_QUIT)
    {
        return eventpp::counterRemover(m_event_dispatcher).appendListener(event_type, callback, 1);
    }
    return m_event_dispatcher.appendListener(event_type, callback);
}

void Application::unregister_event_callback(const EventType& event_type, CallbackHandle& handle)
{
    m_event_dispatcher.removeListener(event_type, handle);
}

void Application::dispatch_event(const EventType& event_type)
{
    m_event_dispatcher.dispatch(event_type);
}

void Application::clear_prim_selection()
{
    m_active_selection_list.clear();
    m_active_rich_selection.clear();
    m_global_selection_list.clear();
    m_per_mode_rich_selection.clear();
    m_event_dispatcher.dispatch(EventType::SELECTION_CHANGED);
}

SelectionList Application::get_selection() const
{
    return m_active_selection_list;
}

void Application::set_current_time(double time)
{
    m_current_time = time;
    m_event_dispatcher.dispatch(EventType::CURRENT_TIME_CHANGED);
}

std::string Application::get_settings_path()
{
    if (!m_settings_path.empty())
    {
        return m_settings_path;
    }
    else
    {
#if defined(OPENDCC_OS_WINDOWS)
        const std::string env_name = "USERPROFILE";
#elif defined(OPENDCC_OS_LINUX)
        const std::string env_name = "HOME";
#elif defined(OPENDCC_OS_MAC)
        const std::string env_name = "HOME";
#else
#error Settings serialization is not supported on the current OS
#endif
        auto home_dir = TfGetenv(env_name);

        if (home_dir.empty())
        {
            TF_RUNTIME_ERROR("Failed to find home directory (%s).", env_name.c_str());
            return m_settings_path;
        }

#ifdef OPENDCC_OS_WINDOWS
        std::replace(home_dir.begin(), home_dir.end(), '\\', '/');
#endif

        m_settings_path = home_dir + "/.opendcc/";

        // TODO: remove this code; it's for automatic migration and needs to be removed when it's okay
        std::function<void(const QString&, const QString&)> copy_path = [&copy_path](const QString& src, const QString& dst) {
            QDir dir(src);
            if (!dir.exists())
                return;

            foreach (QString d, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                QString dst_path = dst + QDir::separator() + d;
                dir.mkpath(dst_path);
                copy_path(src + QDir::separator() + d, dst_path);
            }

            foreach (QString f, dir.entryList(QDir::Files))
            {
                QFile::copy(src + QDir::separator() + f, dst + QDir::separator() + f);
            }
        };
        QString settings_path = m_settings_path.c_str();
        QString legacy_settings_path = QString(home_dir.c_str()) + "/.dcc/";
        if (QFileInfo::exists(legacy_settings_path) && !QFileInfo::exists(settings_path))
        {
            copy_path(legacy_settings_path, settings_path);
        }

        TfMakeDir(m_settings_path);

        return m_settings_path;
    }
}

const RichSelection& Application::get_rich_selection() const
{
    return m_active_rich_selection;
}

void Application::set_rich_selection(const RichSelection& rich_selection)
{
    m_active_rich_selection = rich_selection;
    m_event_dispatcher.dispatch(EventType::SELECTION_CHANGED);
}

void Application::set_highlighted_prims(const PXR_NS::SdfPathVector& prims)
{
    m_highlight_selection_prims = prims;
}

const SdfPathVector& Application::get_highlighted_prims() const
{
    return m_highlight_selection_prims;
}

void Application::enable_soft_selection(bool enable)
{
    if (m_enable_soft_selection == enable)
        return;

    m_enable_soft_selection = enable;
    m_soft_selection_settings_changed = true;
    enable ? m_active_rich_selection.set_soft_selection(m_active_selection_list) : m_active_rich_selection.clear();
    m_event_dispatcher.dispatch(EventType::SELECTION_CHANGED);
}

bool Application::is_soft_selection_enabled() const
{
    return m_enable_soft_selection;
}

Application::SelectionMode Application::get_selection_mode() const
{
    return m_selection_mode;
}

void Application::set_selection_mode(SelectionMode mode)
{
    SelectionList::SelectionMask update_mask = convert_to_selection_mask(m_selection_mode);
    m_global_selection_list.update(m_active_selection_list, update_mask);

    m_per_mode_rich_selection[m_selection_mode] = m_active_rich_selection;
    if (m_soft_selection_settings_changed)
    {
        for (auto& cache : m_per_mode_rich_selection)
        {
            cache.second.update();
        }
        m_soft_selection_settings_changed = false;
    }

    m_selection_mode = mode;
    const auto new_selection_mask = convert_to_selection_mask(mode);
    auto extract_highlighted_prims = [this]() -> SdfPathVector {
        auto cur_fully_selected_paths = m_active_selection_list.get_fully_selected_paths();
        std::unordered_set<PXR_NS::SdfPath, PXR_NS::SdfPath::Hash> set(cur_fully_selected_paths.begin(), cur_fully_selected_paths.end());
        set.insert(m_highlight_selection_prims.begin(), m_highlight_selection_prims.end());
        SdfPathVector query_prim(set.begin(), set.end());
        return query_prim;
    };
    if (mode == SelectionMode::POINTS || mode == SelectionMode::EDGES || mode == SelectionMode::FACES || mode == SelectionMode::INSTANCES ||
        mode == SelectionMode::UV)
    {
        m_highlight_selection_prims = extract_highlighted_prims();
        m_active_selection_list = m_global_selection_list.extract(m_highlight_selection_prims, new_selection_mask);
    }
    else
    {
        m_active_selection_list = SelectionList(extract_highlighted_prims());
        m_highlight_selection_prims.clear();
    }

    m_active_rich_selection = m_per_mode_rich_selection[mode];
    m_event_dispatcher.dispatch(EventType::SELECTION_MODE_CHANGED);
    m_event_dispatcher.dispatch(EventType::SELECTION_CHANGED);
}

void Application::set_active_view_scene_context(const PXR_NS::TfToken& context_type)
{
    if (context_type == m_active_view_context_type)
        return;
    m_active_view_context_type = context_type;
    m_event_dispatcher.dispatch(EventType::ACTIVE_VIEW_SCENE_CONTEXT_CHANGED);
}

TfToken Application::get_active_view_scene_context() const
{
    return m_active_view_context_type;
}

void Application::set_layer_selection(const PXR_NS::SdfLayerHandleSet& set)
{
    if (m_layer_selection == set)
    {
        return;
    }
    m_layer_selection = set;
    m_event_dispatcher.dispatch(Application::EventType::LAYER_SELECTION_CHANGED);
}

PXR_NS::SdfLayerHandleSet Application::get_layer_selection()
{
    return m_layer_selection;
}

const ApplicationConfig& Application::get_app_config()
{
    return m_app_config;
}

std::shared_ptr<ipc::CommandServer> Application::s_server = nullptr;

/* static */
void Application::create_command_server()
{
    auto& registry = ipc::CommandRegistry::instance();

    registry.add_handler("ServerCreated", [](const ipc::Command& command) {
        const auto end = command.args.end();
        auto find = command.args.find("pid");
        if (find == end)
        {
            return;
        }
        std::string pid = find->second;

        ipc::ServerInfo info;

        find = command.args.find("hostname");
        if (find == end)
        {
            return;
        }
        info.hostname = find->second;

        find = command.args.find("input_port");
        if (find == end)
        {
            return;
        }
        info.input_port = std::atoi(find->second.c_str());

        auto& server_registry = ipc::ServerRegistry::instance();
        server_registry.add_server(pid, info);

        // OPENDCC_DEBUG("new Server created: {} - {}", pid, info.get_tcp_address());
    });

    registry.add_handler("CropRender", [](const ipc::Command& crop) {
        if (!s_server)
        {
            return;
        }

        const auto render_control = render_system::RenderSystem::instance().render_control();
        if (!render_control)
        {
            return;
        }

        const auto type = render_control->control_type();

        // Temporary solution!
        // In the case, the render type is set to usd, the ``crop`` command is sent to the
        // usd render process. Otherwise, the ``crop`` command is sent to ipc server
        // inside the SceneLibSession of the current process, then
        // ipc server sets ``crop`` attribute to scene_lib.
        if (type == "usd")
        {
            const auto end = crop.args.end();
            const auto find = crop.args.find("destination_pid");
            if (find == end)
            {
                return;
            }

            auto usd_crop = crop;
            usd_crop.name = "CropUsdRender";
            s_server->send_command(find->second, usd_crop);
        }
        else // scene_lib
        {
            auto scene_lib_crop = crop;
            scene_lib_crop.name = "CropSceneLibRender";
            s_server->send_command(get_pid_string(), scene_lib_crop);
        }
    });

    registry.add_handler("CancelRender", [](const ipc::Command& command) {
        auto& render_system = render_system::RenderSystem::instance();
        const auto status = render_system.get_render_status();
        if (status == render_system::RenderStatus::IN_PROGRESS || status == render_system::RenderStatus::RENDERING)
        {
            render_system.stop_render();
        }
    });

    registry.add_handler("RenderAgain", [](const ipc::Command& command) {
        auto& render_system = render_system::RenderSystem::instance();
        const auto status = render_system.get_render_status();
        if (status == render_system::RenderStatus::IN_PROGRESS || status == render_system::RenderStatus::RENDERING)
        {
            render_system.stop_render();
        }
        render_system.wait_render();
        render_system.start_render();
    });

    const auto& config = get_app_config();

    ipc::CommandServer::set_server_timeout(config.get<int>("ipc.command_server.server_timeout", 1000));
    ipc::ServerInfo info;
    info.hostname = "127.0.0.1";
    info.input_port = config.get<uint32_t>("ipc.command_server.port", 8000);

    s_server = std::make_unique<ipc::CommandServer>(info);
    info = s_server->get_info();
    if (!s_server->valid())
    {
        OPENDCC_ERROR("Unable to create CommandServer on port {}", info.input_port);
    }
}

void Application::destroy_command_server()
{
    s_server.reset();
}

void Application::update_render_control()
{
#if PXR_VERSION > 1911
    auto hydra_render_control = std::make_shared<UsdRenderControl>(
        "USD", std::make_shared<UsdRender>([] { return "\"" + Application::instance().get_application_root_path() + "/bin/usd_render\""; }));
    render_system::RenderControlHub::instance().add_render_control(hydra_render_control);
#endif

    const std::string active_control = m_settings->get("render.active_control", get_app_config().get<std::string>("render.active_control", "usd"));
    const auto render_control =
        TfMapLookupByValue(render_system::RenderControlHub::instance().get_controls(), active_control, render_system::IRenderControlPtr());
    render_system::RenderSystem::instance().set_render_control(render_control);
}

std::shared_ptr<PackageRegistry> Application::get_package_registry() const
{
    return m_package_registry;
}

void Application::set_app_config(const ApplicationConfig& app_config)
{
    if (m_app_config.is_valid())
    {
        OPENDCC_ERROR("Application config was already assigned.");
        return;
    }
    m_app_config = app_config;
}

void Application::init_python_shell()
{
    py_interp::init_shell();
}

OPENDCC_API UsdClipboard& Application::get_usd_clipboard()
{
    static UsdClipboard clipboard;
    return clipboard;
}

const std::string Application::get_opendcc_version_string()
{
    const static std::string version_string(OPENDCC_VERSION_STRING);
    return version_string;
}

OPENDCC_API std::tuple<int, int, int, int> Application::get_opendcc_version()
{
    static std::tuple<int, int, int, int> version_tuple(OPENDCC_VERSION_MAJOR, OPENDCC_VERSION_MINOR, OPENDCC_VERSION_PATCH, OPENDCC_VERSION_TWEAK);
    return version_tuple;
}

OPENDCC_API const std::string Application::get_build_date()
{
    return std::string(platform::get_build_date_str());
}

OPENDCC_API const std::string Application::get_commit_hash()
{
    return std::string(platform::get_git_commit_hash_str());
}

int Application::run_python_script(const std::string& filepath)
{
    return py_interp::run_script(filepath);
}

int Application::run_python_command(const std::string& code)
{
    return py_interp::run_command(code);
}

OPENDCC_NAMESPACE_CLOSE
