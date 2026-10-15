// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/render/ipc_render.h"
#include "opendcc/base/logging/logger.h"

#include "opendcc/app/viewport/viewport_render_aovs.h"
#include "opendcc/app/viewport/offscreen_render.h"
#include "opendcc/app/viewport/hydra_render_settings.h"

#include "opendcc/usd/usd_live_share/live_share_session.h"

#include "opendcc/render_view/display_driver_api/display_driver_api.h"

#include "opendcc/base/utils/process.h"
#include "opendcc/base/ipc_commands_api/server.h"
#include "opendcc/base/ipc_commands_api/command_registry.h"

#include <pxr/base/gf/frustum.h>
#if PXR_VERSION >= 2108
#include <pxr/imaging/hdx/types.h>
#else
#include <pxr/imaging/hdx/hgiConversions.h>
#include <pxr/imaging/hdSt/glConversions.h>
#endif
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hf/pluginDesc.h>

#include <filesystem>

#include <QOffscreenSurface>
#include <QOpenGLContext>

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.usd.render");

PXR_NAMESPACE_USING_DIRECTIVE

namespace ipc = base::ipc;

#if PXR_VERSION >= 2108
static CameraUtilFraming compute_camera_framing(const GfRect2i& render_rect, const int w, const int h)
{
    auto displayWindow = GfRange2f(GfVec2f(0, 0), GfVec2f(w, h));

    // Intersect the display window with render buffer rect for data window.
    auto dataWindow = render_rect.GetIntersection(GfRect2i(GfVec2i(0, 0), w, h));

    return CameraUtilFraming(displayWindow, dataWindow);
}
#endif

//////////////////////////////////////////////////////////////////////////
// GlContext
//////////////////////////////////////////////////////////////////////////

class GlContext
{
public:
    GlContext()
    {
        QSurfaceFormat fmt;
        fmt.setSamples(4);
        fmt.setProfile(QSurfaceFormat::CoreProfile);

        surf.setFormat(fmt);
        surf.create();

        ctx.setFormat(fmt);
        ctx.setShareContext(QOpenGLContext::globalShareContext());
        ctx.create();
        ctx.makeCurrent(&surf);
    }

    ~GlContext() { ctx.doneCurrent(); }

private:
    QOffscreenSurface surf;
    QOpenGLContext ctx;
};

//////////////////////////////////////////////////////////////////////////
// RenderViewIpc
//////////////////////////////////////////////////////////////////////////

struct RenderViewIpc
{
    int beauty_id = -1;
    std::unordered_map<std::string, int> image_handles;
    display_driver_api::RenderViewConnection connection;
};

//////////////////////////////////////////////////////////////////////////
// IpcRender
//////////////////////////////////////////////////////////////////////////

IpcRender::IpcRender(std::shared_ptr<ViewportSceneContext> scene_context)
    : m_scene_context(scene_context)
{
    m_crop_update.store(false);

    m_ctx = std::make_unique<GlContext>();
    m_render_view_ipc = std::make_unique<RenderViewIpc>();
    m_render = std::make_unique<ViewportOffscreenRender>(scene_context);

    m_render_settings = scene_context->get_render_settings();
    m_dirty_render_settings_cid = scene_context->register_event_handler(ViewportSceneContext::EventType::DirtyRenderSettings, [this] {
        m_render_settings = m_scene_context->get_render_settings();
        update_render_settings();
    });
    std::string render_delegate;
    if (m_render_settings)
    {
        render_delegate = m_render_settings->get_render_delegate();
    }

    if (render_delegate == "Storm")
    {
        render_delegate = "GL";
    }

    HfPluginDescVector plugins;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&plugins);
    auto it =
        std::find_if(plugins.begin(), plugins.end(), [&render_delegate](const HfPluginDesc& desc) { return desc.displayName == render_delegate; });
    m_render->set_renderer_plugin(it == plugins.end() ? TfToken("") : it->id);
    update_render_settings();
    m_processor = std::make_shared<ViewportRenderAOVs>();
    m_processor->flip(true);

    m_params.enable_scene_materials = true;
    m_params.use_camera_light = false;
    m_params.show_locators = false;
    m_params.invised_paths_dirty = false;
    m_params.visibility_mask.mark_clean();
}

IpcRender::~IpcRender()
{
    m_scene_context->unregister_event_handler(ViewportSceneContext::EventType::DirtyRenderSettings, m_dirty_render_settings_cid);
    m_render_settings.reset();
    m_render_view_ipc.reset();
    m_render.reset();
    m_processor.reset();
    m_ctx.reset();
    m_scene_context.reset();
}

bool IpcRender::valid() const
{
    return m_ctx && m_render_settings && m_render_view_ipc && m_render && m_processor;
}

bool IpcRender::converged() const
{
    return m_render->is_converged();
}

HydraRenderSettings& IpcRender::get_settings() const
{
    return *m_render_settings.get();
}

void IpcRender::update_render_settings()
{
    m_render->set_render_settings(m_render_settings);
    m_render->get_engine()->update_render_settings();
}

render_system::RenderStatus IpcRender::exec_render()
{
    std::lock_guard<std::mutex> lock(m_params_mtx);

    const auto dims = m_render_settings->get_resolution();
    const auto camera = m_render_settings->get_camera();
    auto frustum = camera.GetFrustum();
    CameraUtilConformWindow(&frustum, CameraUtilConformWindowPolicy::CameraUtilFit, dims[1] != 0 ? (double)dims[0] / dims[1] : 1.0);
    m_render->set_camera_state(frustum.ComputeViewMatrix(), frustum.ComputeProjectionMatrix(), frustum.GetPosition());
    m_params.render_resolution = dims;
    m_render->set_render_params(m_params);

    const auto time = Application::instance().get_current_time();
    return m_render->render(time, time, m_processor);
}

void IpcRender::send_aovs()
{
    const auto& params = m_render->get_render_params();
    for (const auto& aov : m_processor->get_aovs())
    {
#if PXR_VERSION < 2108
        const auto data_size = HgiDataSizeOfFormat(aov.desc.format);
#else
        const auto data_size = HgiGetDataSizeOfFormat(aov.desc.format, nullptr, nullptr);
#endif

        auto image_handle = TfMapLookupByValue(m_render_view_ipc->image_handles, aov.name, -1);
        if (image_handle == -1)
        {
            display_driver_api::ImageType component_type;
            switch (data_size / HgiGetComponentCount(aov.desc.format))
            {
            case 1:
            {
                component_type = display_driver_api::ImageType::Byte;
                break;
            }
            case 2:
            {
                component_type = display_driver_api::ImageType::HalfFloat;
                break;
            }
            case 4:
            {
                component_type = display_driver_api::ImageType::Float;
                break;
            }
            default:
            {
                component_type = display_driver_api::ImageType::Unknown;
            }
            }

            display_driver_api::ImageDescription image_desc;
            image_desc.parent_image_id = m_render_view_ipc->beauty_id;
            image_desc.image_name = aov.name;
            image_desc.image_data_type = component_type;
            image_desc.num_channels = HgiGetComponentCount(aov.desc.format);
            image_desc.width = params.render_resolution[0];
            image_desc.height = params.render_resolution[1];
            image_desc.extra_attributes["opendcc/dcc/pid"] = base::utils::get_pid_string();

            image_handle = render_view_open_image(m_render_view_ipc->connection, image_handle, image_desc);
            m_render_view_ipc->image_handles[aov.name] = image_handle;
            if (m_render_view_ipc->beauty_id == -1 && aov.name == "color" || aov.name == "beauty")
            {
                m_render_view_ipc->beauty_id = image_handle;
            }
        }

        display_driver_api::ROI roi;
        if (params.crop_region.IsValid())
        {
#if PXR_VERSION > 2008
            roi.xstart = params.crop_region.GetMinX();
            roi.xend = params.crop_region.GetMaxX() + 1;
            roi.ystart = params.crop_region.GetMinY();
            roi.yend = params.crop_region.GetMaxY() + 1;
#else
            roi.xstart = params.crop_region.GetLeft();
            roi.xend = params.crop_region.GetRight() + 1;
            roi.ystart = params.crop_region.GetTop();
            roi.yend = params.crop_region.GetBottom() + 1;
#endif
        }
        else
        {
            roi.xstart = 0;
            roi.xend = (uint32_t)aov.desc.dimensions[0];
            roi.ystart = 0;
            roi.yend = (uint32_t)aov.desc.dimensions[1];
        }

        render_view_write_region(m_render_view_ipc->connection, image_handle, roi, data_size, aov.data.data());
    }
}

render_system::RenderStatus IpcRender::write_aovs()
{
    namespace fs = std::filesystem;

    const auto& settings_aovs = m_render_settings->get_aovs();
    for (const auto& aov : m_processor->get_aovs())
    {
        auto aov_setting_iter = std::find_if(settings_aovs.begin(), settings_aovs.end(),
                                             [&aov](const HydraRenderSettings::AOV& aov_settings) { return aov_settings.name == aov.name; });
        if (aov_setting_iter == settings_aovs.end())
        {
            continue;
        }

        const auto file_path = fs::u8path(aov_setting_iter->product_name.GetString());
        if (file_path.empty())
        {
            OPENDCC_ERROR("Failed to write aov: product name is empty.");
            return render_system::RenderStatus::FAILED;
        }
        const size_t w = aov.desc.dimensions[0];
        const size_t h = aov.desc.dimensions[1];

#if PXR_VERSION < 2108
        GlfImage::StorageSpec storage;
        GLenum dummy;
        HdStGLConversions::GetGlFormat(HdxHgiConversions::GetHdFormat(aov.desc.format), &storage.format, &storage.type, &dummy);
#else
        HioImage::StorageSpec storage;
        storage.format = HdxGetHioFormat(aov.desc.format);
#endif
        storage.width = w;
        storage.height = h;
        storage.depth = 1;
        storage.flipped = false;
        storage.data = (void*)(aov.data.data());
#if PXR_VERSION < 2108
        const auto image = GlfImage::OpenForWriting(file_path.u8string());
#else
        const auto image = HioImage::OpenForWriting(file_path.u8string());
#endif
        const bool result = image && image->Write(storage);
        if (!result)
        {
            OPENDCC_ERROR("Failed to write aov to {}", file_path.u8string().c_str());
            return render_system::RenderStatus::FAILED;
        }
    }
    return render_system::RenderStatus::FINISHED;
}

void IpcRender::create_command_server()
{
#if PXR_VERSION >= 2108
    const auto& config = Application::get_app_config();

    m_main_server_info.hostname = "127.0.0.1";
    m_main_server_info.input_port = config.get<uint32_t>("ipc.command_server.port", 8000);

    ipc::CommandServer::set_server_timeout(config.get<int>("ipc.command_server.server_timeout", 1000));

    ipc::ServerInfo info;
    info.hostname = "127.0.0.1";
    m_server = std::make_unique<ipc::CommandServer>(info);
    info = m_server->get_info();

    ipc::Command command;
    command.name = "ServerCreated";
    command.args["pid"] = base::utils::get_pid_string();
    command.args["hostname"] = info.hostname;
    command.args["input_port"] = std::to_string(info.input_port);
    m_server->send_command(m_main_server_info, command);

    ipc::CommandRegistry::instance().add_handler("CropUsdRender", [this](const ipc::Command& command) {
        const auto min_x_str = command.args.find("min_x");
        const auto max_x_str = command.args.find("max_x");
        const auto min_y_str = command.args.find("min_y");
        const auto max_y_str = command.args.find("max_y");

        const auto end = command.args.end();
        std::lock_guard<std::mutex> lock(m_params_mtx);
        if (min_x_str == end || max_x_str == end || min_y_str == end || max_y_str == end)
        {
            m_params.crop_region = GfRect2i();
        }
        else
        {
            const auto min_x = std::atoi(min_x_str->second.c_str());
            const auto max_x = std::atoi(max_x_str->second.c_str());
            const auto min_y = std::atoi(min_y_str->second.c_str());
            const auto max_y = std::atoi(max_y_str->second.c_str());

            m_params.crop_region = GfRect2i(GfVec2i(min_x, min_y), GfVec2i(max_x, max_y));
        }

        set_crop_update(true);
    });
#endif
}

bool IpcRender::get_crop_update()
{
    return m_crop_update.load();
}

void IpcRender::set_crop_update(const bool crop_update)
{
    m_crop_update.store(crop_update);
}

//////////////////////////////////////////////////////////////////////////
// render
//////////////////////////////////////////////////////////////////////////

render_system::RenderStatus ipr_render(std::shared_ptr<ViewportSceneContext> scene_context,
                                       const std::vector<PXR_NS::UsdUtilsTimeCodeRange>& time_ranges)
{
    auto time = time_ranges.front().GetStartTimeCode();
    Application::instance().set_current_time(time.GetValue());
    IpcRender render(scene_context);
    if (!render.valid())
    {
        return render_system::RenderStatus::FAILED;
    }
    render.create_command_server();

    ShareEditsContext::ConnectionSettings connection_settings;
    auto live_share =
        std::shared_ptr<LiveShareSession>(new LiveShareSession(Application::instance().get_session()->get_current_stage(), connection_settings));
    live_share->start_share();

    bool dirty = true;
    while (true)
    {
        live_share->process();

        const auto crop = render.get_crop_update();

        if (!dirty && !crop)
        {
            continue;
        }

        if (crop)
        {
            render.set_crop_update(false);
        }

        render.exec_render();
        render.send_aovs();
        dirty = !render.converged();
    };

    live_share->stop_share();
    live_share.reset();
    return render_system::RenderStatus::FINISHED;
}

render_system::RenderStatus preview_render(std::shared_ptr<ViewportSceneContext> scene_context,
                                           const std::vector<PXR_NS::UsdUtilsTimeCodeRange>& time_ranges)
{
    auto start_time = time_ranges.front().GetStartTimeCode();
    Application::instance().set_current_time(start_time.GetValue());
    IpcRender render(scene_context);
    if (!render.valid())
    {
        return render_system::RenderStatus::FAILED;
    }

    auto status = render_system::RenderStatus::NOT_STARTED;
    for (const auto& range : time_ranges)
    {
        for (const auto time : range)
        {
            Application::instance().set_current_time(time.GetValue());
            do
            {
                status = render.exec_render();
                render.send_aovs();
            } while (!render.converged());
        }
    }
    return status;
}

render_system::RenderStatus disk_render(std::shared_ptr<ViewportSceneContext> scene_context,
                                        const std::vector<PXR_NS::UsdUtilsTimeCodeRange>& time_ranges)
{
    auto start_time = time_ranges.front().GetStartTimeCode();
    Application::instance().set_current_time(start_time.GetValue());
    IpcRender render(scene_context);
    if (!render.valid() || render.get_settings().get_aovs().empty())
    {
        return render_system::RenderStatus::FAILED;
    }

    auto status = render_system::RenderStatus::NOT_STARTED;

    for (const auto& range : time_ranges)
    {
        for (const auto time : range)
        {
            Application::instance().set_current_time(time.GetValue());
            do
            {
                status = render.exec_render();
            } while (!render.converged());

            if (status != render_system::RenderStatus::FINISHED)
            {
                return status;
            }
            if (render.write_aovs() != render_system::RenderStatus::FINISHED)
            {
                return status;
            }
        }
    }

    return render_system::RenderStatus::FINISHED;
}

OPENDCC_NAMESPACE_CLOSE
