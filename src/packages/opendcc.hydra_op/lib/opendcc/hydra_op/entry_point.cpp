// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/entry_point.h"
#include <opendcc/app/viewport/viewport_widget.h>
#include <opendcc/hydra_op/select_tool.h>
#include <opendcc/hydra_op/scene_context.h>
#include <opendcc/app/ui/application_ui.h>
#include <opendcc/app/viewport/iviewport_tool_context.h>
#include <opendcc/hydra_op/session.h>
#include <opendcc/hydra_op/viewport_ui_extension.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using base::packaging::Package;

void HydraOpDCCEntryPoint::initialize(const Package& package)
{
    ViewportSceneContextRegistry::get_instance().register_scene_context(TfToken("HydraOp"),
                                                                        [] { return std::make_shared<HydraOpSceneContext>(TfToken("HydraOp")); });
    ViewportToolContextRegistry::register_tool_context(TfToken("HydraOp"), TfToken("SelectTool"), [] { return new HydraOpSelectToolContext; });
    ViewportUIExtensionRegistry::instance().register_ui_extension(TfToken("HydraOp"),
                                                                  [](auto widget) { return std::make_shared<HydraOpViewportUIExtension>(widget); });
}
void HydraOpDCCEntryPoint::uninitialize(const Package& package)
{
    ViewportUIExtensionRegistry::instance().unregister_ui_extension(TfToken("HydraOp"));
}

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(HydraOpDCCEntryPoint);

OPENDCC_NAMESPACE_CLOSE
