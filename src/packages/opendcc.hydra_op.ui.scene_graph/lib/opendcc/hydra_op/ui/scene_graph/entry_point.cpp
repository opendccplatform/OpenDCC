// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/ui/scene_graph/entry_point.h"

#include "opendcc/hydra_op/ui/scene_graph/hydra_op_scene_graph.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/panel_factory.h"

OPENDCC_NAMESPACE_OPEN

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(HydraOpSceneGraphEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

HydraOpSceneGraphEntryPoint::HydraOpSceneGraphEntryPoint() {}

void HydraOpSceneGraphEntryPoint::initialize(const Package& package)
{
    PanelFactory::instance().register_panel(
        "hydra_op_scene_graph", [] { return new HydraOpSceneGraph(); }, i18n("panels", "HydraOp Scene Graph").toStdString(), true,
        ":/icons/panel_hydraop_scene_graph", "Hydra");
}

void HydraOpSceneGraphEntryPoint::uninitialize(const Package& package)
{
    PanelFactory::instance().unregister_panel("hydra_op_scene_graph");
}

HydraOpSceneGraphEntryPoint::~HydraOpSceneGraphEntryPoint() {}

OPENDCC_NAMESPACE_CLOSE
