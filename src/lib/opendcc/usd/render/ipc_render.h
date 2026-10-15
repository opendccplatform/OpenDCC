/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/ipc_commands_api/server_info.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/render_system/render_system.h"
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usdUtils/timeCodeRange.h>

#include <string>
#include <atomic>

OPENDCC_NAMESPACE_OPEN

class GlContext;

struct RenderViewIpc;

class ViewportOffscreenRender;
class ViewportRenderAOVs;

class HydraRenderSettings;

namespace base::ipc
{
    class CommandServer;
}

class IpcRender
{
public:
    IpcRender(std::shared_ptr<ViewportSceneContext> scene_context);
    ~IpcRender();

    bool valid() const;

    bool converged() const;

    HydraRenderSettings& get_settings() const;

    void update_render_settings();

    render_system::RenderStatus exec_render();

    void send_aovs();

    render_system::RenderStatus write_aovs();

    void create_command_server();

    bool get_crop_update();
    void set_crop_update(const bool crop_update);

private:
    std::unique_ptr<GlContext> m_ctx;
    std::shared_ptr<HydraRenderSettings> m_render_settings;
    std::unique_ptr<RenderViewIpc> m_render_view_ipc;
    std::unique_ptr<base::ipc::CommandServer> m_server;
    std::unique_ptr<ViewportOffscreenRender> m_render;
    std::shared_ptr<ViewportRenderAOVs> m_processor;
    std::shared_ptr<ViewportSceneContext> m_scene_context;
    ViewportHydraEngineParams m_params;
    PXR_NS::UsdStageRefPtr m_stage;
    PXR_NS::SdfPath m_settings_path;
    PXR_NS::UsdTimeCode m_last_time;

    base::ipc::ServerInfo m_main_server_info;

    std::atomic_bool m_crop_update;
    std::mutex m_params_mtx;
    ViewportSceneContext::DispatcherHandle m_dirty_render_settings_cid;
};

render_system::RenderStatus ipr_render(std::shared_ptr<ViewportSceneContext> scene_context,
                                       const std::vector<PXR_NS::UsdUtilsTimeCodeRange>& time_ranges);

render_system::RenderStatus preview_render(std::shared_ptr<ViewportSceneContext> scene_context,
                                           const std::vector<PXR_NS::UsdUtilsTimeCodeRange>& time_range);

render_system::RenderStatus disk_render(std::shared_ptr<ViewportSceneContext> scene_context,
                                        const std::vector<PXR_NS::UsdUtilsTimeCodeRange>& time_range);

OPENDCC_NAMESPACE_CLOSE
