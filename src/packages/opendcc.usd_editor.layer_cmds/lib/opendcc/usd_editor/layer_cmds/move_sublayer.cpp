// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/move_sublayer.h"

#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/usd_editor/layer_cmds/sublayer_utils.h"
#include "opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/changeBlock.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

std::string commands::MoveSubLayerCommand::cmd_name = "move_sublayer";

CommandSyntax commands::MoveSubLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("parent_identifier", "Identifier of the parent layer that owns the sublayer")
        .arg<std::string>("sublayer_identifier", "Identifier of the sublayer to move")
        .arg<int>("index", "Final index in the destination's sublayer list")
        .kwarg<std::string>("new_parent_identifier", "Destination parent; omit to reorder within the current parent")
        .description("Reorders a sublayer or moves it to another parent.");
}

std::shared_ptr<Command> commands::MoveSubLayerCommand::create_cmd()
{
    return std::make_shared<commands::MoveSubLayerCommand>();
}

CommandResult commands::MoveSubLayerCommand::execute(const CommandArgs& args)
{
    auto parent_arg = args.get_arg<std::string>(0);
    auto sublayer_arg = args.get_arg<std::string>(1);
    auto index_arg = args.get_arg<int>(2);
    if (!parent_arg || !sublayer_arg || !index_arg)
    {
        OPENDCC_WARN("Failed to move sublayer: parent identifier, sublayer identifier and target index are required.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    SdfLayerRefPtr parent_layer = SdfLayer::Find(parent_arg->get_value());
    if (!parent_layer)
        parent_layer = SdfLayer::FindOrOpen(parent_arg->get_value());
    if (!parent_layer)
    {
        OPENDCC_WARN("Failed to move sublayer: parent layer '{}' could not be found.", parent_arg->get_value().c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    const int from = commands::utils::find_sublayer_index(parent_layer, sublayer_arg->get_value());
    if (from < 0)
    {
        OPENDCC_WARN("Failed to move sublayer: '{}' is not a sublayer of '{}'.", sublayer_arg->get_value().c_str(), parent_arg->get_value().c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto destination = parent_layer;
    if (auto new_parent_arg = args.get_kwarg<std::string>("new_parent_identifier"))
    {
        destination = SdfLayer::Find(new_parent_arg->get_value());
        if (!destination)
            destination = SdfLayer::FindOrOpen(new_parent_arg->get_value());
    }
    if (!destination || !parent_layer->PermissionToEdit() || !destination->PermissionToEdit())
        return CommandResult(CommandResult::Status::INVALID_ARG);

    const bool reparent = destination != parent_layer;
    const int to = index_arg->get_value();
    const int destination_count = static_cast<int>(destination->GetSubLayerPaths().size());
    if (to < 0 || to >= destination_count + static_cast<int>(reparent))
        return CommandResult(CommandResult::Status::INVALID_ARG);
    if (!reparent && to == from)
        return CommandResult(CommandResult::Status::SUCCESS);

    std::string moved_path = parent_layer->GetSubLayerPaths()[from];
    const auto moved_layer = SdfLayer::FindOrOpenRelativeToLayer(parent_layer, moved_path);
    if (reparent)
    {
        if (!moved_layer || commands::utils::find_sublayer_index(destination, moved_layer->GetIdentifier()) >= 0 ||
            commands::utils::is_sublayer_reachable(moved_layer, destination))
            return CommandResult(CommandResult::Status::INVALID_ARG);
        moved_path = moved_layer->GetIdentifier();
    }
    m_moved_layers = commands::utils::retain_layer_tree(moved_layer);

    const auto remember_parent = [this](const SdfLayerRefPtr& layer) {
        const auto paths = layer->GetSubLayerPaths();
        m_parents.push_back({ layer, { paths.begin(), paths.end() }, layer->GetSubLayerOffsets(), layer->IsDirty() });
    };
    remember_parent(parent_layer);
    if (reparent)
        remember_parent(destination);

    UsdEditsBlock edits;
    SdfChangeBlock changes;
    const auto moved_offset = parent_layer->GetSubLayerOffset(from);
    parent_layer->RemoveSubLayerPath(from);
    destination->InsertSubLayerPath(moved_path, to);
    destination->SetSubLayerOffset(moved_offset, to);
    return CommandResult(CommandResult::Status::SUCCESS);
}

void commands::MoveSubLayerCommand::do_cmd()
{
    if (m_parents.empty())
        return;

    UsdEditsBlock edits;
    SdfChangeBlock changes;
    for (auto& parent : m_parents)
    {
        const bool was_dirty = parent.layer->IsDirty();
        commands::utils::swap_sublayers(parent.layer, parent.paths, parent.offsets);
        if (auto proxy = TfDynamic_cast<LayerStateDelegateProxyRefPtr>(parent.layer->GetStateDelegate()))
            proxy->set_dirty(parent.dirty);
        parent.dirty = was_dirty;
    }
}

void commands::MoveSubLayerCommand::undo()
{
    do_cmd();
}

void commands::MoveSubLayerCommand::redo()
{
    do_cmd();
}

OPENDCC_NAMESPACE_CLOSE
