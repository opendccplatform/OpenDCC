// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/ui/attribute_view/entry_point.h"

#include "opendcc/hydra_op/ui/attribute_view/hydra_op_attribute_view.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/panel_factory.h"

OPENDCC_NAMESPACE_OPEN

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(HydraOpAttributeViewEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

void HydraOpAttributeViewEntryPoint::initialize(const Package& package)
{
    PanelFactory::instance().register_panel(
        "hydra_op_attribute_view", [] { return new HduiSceneIndexDebuggerWidget(); }, i18n("panels", "HydraOp Attribute View").toStdString(), true,
        ":/icons/panel_hydraop_attribute_view", "Hydra");
}

void HydraOpAttributeViewEntryPoint::uninitialize(const Package& package)
{
    PanelFactory::instance().unregister_panel("hydra_op_attribute_view");
}

OPENDCC_NAMESPACE_CLOSE
