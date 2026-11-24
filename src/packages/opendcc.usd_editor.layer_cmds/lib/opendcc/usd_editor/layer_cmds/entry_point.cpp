// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/entry_point.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/usd_editor/layer_cmds/set_edit_target.h"
#include "opendcc/usd_editor/layer_cmds/mute_layer.h"
#include "opendcc/usd_editor/layer_cmds/add_sublayer.h"
#include "opendcc/usd_editor/layer_cmds/remove_sublayer.h"
#include "opendcc/usd_editor/layer_cmds/move_sublayer.h"
#include "opendcc/usd_editor/layer_cmds/apply_layer_text.h"
#include "opendcc/usd_editor/layer_cmds/layer_file_commands.h"

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandRegistry;
using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(UsdEditorLayerCmdsEntryPoint);
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

void UsdEditorLayerCmdsEntryPoint::initialize(const Package& package)
{
    CommandRegistry::register_command(commands::SetEditTargetCommand::cmd_name, commands::SetEditTargetCommand::cmd_syntax(),
                                      &commands::SetEditTargetCommand::create_cmd);
    CommandRegistry::register_command(commands::MuteLayerCommand::cmd_name, commands::MuteLayerCommand::cmd_syntax(),
                                      &commands::MuteLayerCommand::create_cmd);
    CommandRegistry::register_command(commands::AddSubLayerCommand::cmd_name, commands::AddSubLayerCommand::cmd_syntax(),
                                      &commands::AddSubLayerCommand::create_cmd);
    CommandRegistry::register_command(commands::RemoveSubLayerCommand::cmd_name, commands::RemoveSubLayerCommand::cmd_syntax(),
                                      &commands::RemoveSubLayerCommand::create_cmd);
    CommandRegistry::register_command(commands::MoveSubLayerCommand::cmd_name, commands::MoveSubLayerCommand::cmd_syntax(),
                                      &commands::MoveSubLayerCommand::create_cmd);
    CommandRegistry::register_command(commands::ApplyLayerTextCommand::cmd_name, commands::ApplyLayerTextCommand::cmd_syntax(),
                                      &commands::ApplyLayerTextCommand::create_cmd);
    CommandRegistry::register_command(commands::SaveLayerCommand::cmd_name, commands::SaveLayerCommand::cmd_syntax(),
                                      &commands::SaveLayerCommand::create_cmd);
    CommandRegistry::register_command(commands::RetargetLayerCommand::cmd_name, commands::RetargetLayerCommand::cmd_syntax(),
                                      &commands::RetargetLayerCommand::create_cmd);
    CommandRegistry::register_command(commands::ReloadLayerCommand::cmd_name, commands::ReloadLayerCommand::cmd_syntax(),
                                      &commands::ReloadLayerCommand::create_cmd);
}

void UsdEditorLayerCmdsEntryPoint::uninitialize(const Package& package)
{
    CommandRegistry::unregister_command(commands::SetEditTargetCommand::cmd_name);
    CommandRegistry::unregister_command(commands::MuteLayerCommand::cmd_name);
    CommandRegistry::unregister_command(commands::AddSubLayerCommand::cmd_name);
    CommandRegistry::unregister_command(commands::RemoveSubLayerCommand::cmd_name);
    CommandRegistry::unregister_command(commands::MoveSubLayerCommand::cmd_name);
    CommandRegistry::unregister_command(commands::ApplyLayerTextCommand::cmd_name);
    CommandRegistry::unregister_command(commands::SaveLayerCommand::cmd_name);
    CommandRegistry::unregister_command(commands::RetargetLayerCommand::cmd_name);
    CommandRegistry::unregister_command(commands::ReloadLayerCommand::cmd_name);
}
OPENDCC_NAMESPACE_CLOSE
