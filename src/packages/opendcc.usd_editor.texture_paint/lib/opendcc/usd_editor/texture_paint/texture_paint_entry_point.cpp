// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/texture_paint/texture_paint_entry_point.h"
#include "opendcc/app/ui/panel_factory.h"
#include "opendcc/app/viewport/tool_settings_view.h"
#include "opendcc/usd_editor/texture_paint/texture_paint_tool_settings.h"
#include "opendcc/usd_editor/texture_paint/texture_paint_tool_context.h"
#include <pxr/base/plug/registry.h>
OPENDCC_NAMESPACE_OPEN

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(TexturePaintEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

void TexturePaintEntryPoint::initialize(const Package& package)
{
    PlugRegistry::GetInstance().RegisterPlugins(package.get_root_dir() + "/pxr_plugins");
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("texture_paint"), [] { return new TexturePaintToolContext; });
    ToolSettingsViewRegistry::register_tool_settings_view(TfToken("texture_paint"), TfToken("USD"), [] {
        auto derived_context = dynamic_cast<TexturePaintToolContext*>(ApplicationUI::instance().get_current_viewport_tool());
        return derived_context ? new TexturePaintToolSettingsWidget(derived_context) : nullptr;
    });
}

void TexturePaintEntryPoint::uninitialize(const Package& package) {}

OPENDCC_NAMESPACE_CLOSE
