// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/ui/scene_browser/entry_point.h"

#include "opendcc/hydra_op/ui/scene_browser/sceneIndexDebuggerWidget.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/panel_factory.h"

#include <pxr/base/tf/type.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(HydraSceneBrowserEntryPoint);

void HydraSceneBrowserEntryPoint::initialize(const Package& package)
{
    PanelFactory::instance().register_panel(
        "hydra_scene_browser", [] { return new HduiSceneIndexDebuggerWidget(); }, i18n("panels", "Hydra Scene Browser").toStdString(), true,
        ":/icons/panel_hydra_scene_browser", "Hydra");
}

void HydraSceneBrowserEntryPoint::uninitialize(const Package& package)
{
    PanelFactory::instance().unregister_panel("hydra_scene_browser");
}

OPENDCC_NAMESPACE_CLOSE
