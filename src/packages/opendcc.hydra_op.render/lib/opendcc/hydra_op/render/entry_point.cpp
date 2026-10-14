// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/render/entry_point.h"
#include "opendcc/render_system/render_system.h"
#include <opendcc/app/viewport/usd_render_control.h>
#include <opendcc/app/viewport/usd_render.h>
#include <opendcc/hydra_op/session.h>
#include "opendcc/usd/render/render_app_controller.h"
#include "opendcc/hydra_op/render/hydra_op_render_app_controller.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

void HydraOpRenderEntryPoint::initialize(const Package& package)
{
    auto hydra_render_control = std::make_shared<UsdRenderControl>("HydraOp", std::make_shared<UsdRender>([] {
                                                                       return "\"" + Application::instance().get_application_root_path() +
                                                                              "/bin/hydra_op_render\" " + s_view_node_opt.name + " " +
                                                                              HydraOpSession::instance().get_view_node().GetString();
                                                                   }));
    render_system::RenderControlHub::instance().add_render_control(hydra_render_control);
    RenderAppControllerFactory::get_instance().register_app_controller(TfToken("HydraOp"),
                                                                       [] { return std::make_unique<HydraOpRenderAppController>(); });
}

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(HydraOpRenderEntryPoint);

OPENDCC_NAMESPACE_CLOSE
