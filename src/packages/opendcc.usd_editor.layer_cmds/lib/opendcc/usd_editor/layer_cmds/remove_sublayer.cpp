// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/remove_sublayer.h"

#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/usd_editor/layer_cmds/sublayer_utils.h"

#include <pxr/usd/sdf/layer.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

std::string commands::RemoveSubLayerCommand::cmd_name = "remove_sublayer";

CommandSyntax commands::RemoveSubLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("parent_identifier", "Identifier of the parent layer that owns the sublayer")
        .arg<std::string>("sublayer_identifier", "Identifier of the sublayer to remove")
        .description("Removes a sublayer entry from a parent layer's sublayer list.");
}

std::shared_ptr<Command> commands::RemoveSubLayerCommand::create_cmd()
{
    return std::make_shared<commands::RemoveSubLayerCommand>();
}

CommandResult commands::RemoveSubLayerCommand::execute(const CommandArgs& args)
{
    auto parent_arg = args.get_arg<std::string>(0);
    auto sublayer_arg = args.get_arg<std::string>(1);
    if (!parent_arg || !sublayer_arg)
    {
        OPENDCC_WARN("Failed to remove sublayer: parent and sublayer identifiers are required.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    SdfLayerRefPtr parent_layer = SdfLayer::Find(parent_arg->get_value());
    if (!parent_layer)
        parent_layer = SdfLayer::FindOrOpen(parent_arg->get_value());
    if (!parent_layer)
    {
        OPENDCC_WARN("Failed to remove sublayer: parent layer '{}' could not be found.", parent_arg->get_value().c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    const int index = commands::utils::find_sublayer_index(parent_layer, sublayer_arg->get_value());
    if (index < 0)
    {
        OPENDCC_WARN("Failed to remove sublayer: '{}' is not a sublayer of '{}'.", sublayer_arg->get_value().c_str(),
                     parent_arg->get_value().c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (!parent_layer->PermissionToEdit())
        return CommandResult(CommandResult::Status::INVALID_ARG);

    // Detached anonymous and dirty descendants must survive until undo restores the path.
    m_layers = commands::utils::retain_layer_tree(SdfLayer::FindRelativeToLayer(parent_layer, parent_layer->GetSubLayerPaths()[index]));

    m_parent = parent_layer;
    const auto paths = parent_layer->GetSubLayerPaths();
    m_paths.assign(paths.begin(), paths.end());
    m_offsets = parent_layer->GetSubLayerOffsets();
    {
        UsdEditsBlock change_block;
        parent_layer->RemoveSubLayerPath(index);
        m_inverse = change_block.take_edits();
    }

    return CommandResult(CommandResult::Status::SUCCESS);
}

void commands::RemoveSubLayerCommand::do_cmd()
{
    if (m_inverse && m_inverse->size())
        m_inverse->invert();
    else
    {
        UsdEditsBlock edits;
        commands::utils::swap_sublayers(m_parent, m_paths, m_offsets);
    }
}

void commands::RemoveSubLayerCommand::undo()
{
    do_cmd();
}

void commands::RemoveSubLayerCommand::redo()
{
    do_cmd();
}

OPENDCC_NAMESPACE_CLOSE
