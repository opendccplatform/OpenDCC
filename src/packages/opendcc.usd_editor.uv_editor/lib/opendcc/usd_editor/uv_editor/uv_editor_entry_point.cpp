// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/uv_editor/uv_editor_entry_point.h"

#include "opendcc/usd_editor/uv_editor/uv_editor.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/panel_factory.h"
#include <pxr/base/plug/registry.h>

OPENDCC_NAMESPACE_OPEN

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(UVEditorEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

UVEditorEntryPoint::UVEditorEntryPoint() {}

void UVEditorEntryPoint::initialize(const Package& package)
{
    PlugRegistry::GetInstance().RegisterPlugins(package.get_root_dir() + "/pxr_plugins");

    PanelFactory::instance().register_panel(
        "uv_editor", [] { return new UVEditor; }, i18n("panels", "UV Editor").toStdString(), false, ":icons/panel_uv_editor");
}

void UVEditorEntryPoint::uninitialize(const Package& package) {}

UVEditorEntryPoint::~UVEditorEntryPoint() {}

OPENDCC_NAMESPACE_CLOSE
