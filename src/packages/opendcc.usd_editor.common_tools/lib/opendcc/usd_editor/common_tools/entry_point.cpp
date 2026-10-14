// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/entry_point.h"
#include "opendcc/base/logging/logger.h"

#include "opendcc/base/commands_api/core/command_registry.h"

#include "opendcc/usd_editor/common_tools/viewport_move_tool_command.h"
#include "opendcc/usd_editor/common_tools/viewport_change_pivot_command.h"
#include "opendcc/usd_editor/common_tools/viewport_move_tool_context.h"

#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_command.h"
#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_context.h"

#include "opendcc/usd_editor/common_tools/viewport_scale_tool_command.h"
#include "opendcc/usd_editor/common_tools/viewport_scale_tool_context.h"
#include "opendcc/usd_editor/common_tools/viewport_render_region_tool_context.h"
#include "opendcc/usd_editor/common_tools/viewport_render_region_extension.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(UsdEditorCommonToolsEntryPoint);
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

void UsdEditorCommonToolsEntryPoint::initialize(const Package& package)
{
    CommandRegistry::register_command(ViewportMoveToolCommand::cmd_name, ViewportMoveToolCommand::cmd_syntax(), &ViewportMoveToolCommand::create_cmd);

    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("move_tool"), []() { return new ViewportMoveToolContext; });

    CommandRegistry::register_command(ViewportRotateToolCommand::cmd_name, ViewportRotateToolCommand::cmd_syntax(),
                                      &ViewportRotateToolCommand::create_cmd);
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("rotate_tool"), []() { return new ViewportRotateToolContext; });

    CommandRegistry::register_command(ViewportScaleToolCommand::cmd_name, ViewportScaleToolCommand::cmd_syntax(),
                                      &ViewportScaleToolCommand::create_cmd);
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("scale_tool"), []() { return new ViewportScaleToolContext; });

    CommandRegistry::register_command(ViewportChangePivotCommand::cmd_name, ViewportChangePivotCommand::cmd_syntax(),
                                      &ViewportChangePivotCommand::create_cmd);

    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("render_region_tool"),
                                                       []() { return new ViewportRenderRegionToolContext(); });

    ViewportUIExtensionRegistry::instance().register_ui_extension(
        TfToken("render_region"), [](auto widget) { return std::make_shared<ViewportRenderRegionExtension>(widget); });
}

void UsdEditorCommonToolsEntryPoint::uninitialize(const Package& package)
{

    CommandRegistry::unregister_command(ViewportMoveToolCommand::cmd_name);
    CommandRegistry::unregister_command(ViewportRotateToolCommand::cmd_name);
    CommandRegistry::unregister_command(ViewportScaleToolCommand::cmd_name);
    CommandRegistry::unregister_command(ViewportChangePivotCommand::cmd_name);
}

OPENDCC_NAMESPACE_CLOSE
