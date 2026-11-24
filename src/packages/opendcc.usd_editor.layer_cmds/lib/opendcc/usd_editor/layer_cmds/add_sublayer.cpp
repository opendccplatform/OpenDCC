// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/add_sublayer.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/usd_editor/layer_cmds/sublayer_utils.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/ar/resolver.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

std::string commands::AddSubLayerCommand::cmd_name = "add_sublayer";

CommandSyntax commands::AddSubLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("parent_identifier", "Identifier of the parent layer to receive the sublayer")
        .arg<std::string>("sublayer_identifier", "Identifier of the sublayer to insert (created if it doesn't exist)")
        .kwarg<int>("index", "Insertion index in the parent's sublayer list (defaults to 0, the strongest position)")
        .kwarg<bool>("anonymous", "Create an anonymous layer using sublayer_identifier as its display name")
        .result<std::string>("Identifier of the inserted layer")
        .description("Inserts a sublayer into a parent layer's sublayer list (creating the sublayer if needed).");
}

std::shared_ptr<Command> commands::AddSubLayerCommand::create_cmd()
{
    return std::make_shared<commands::AddSubLayerCommand>();
}

CommandResult commands::AddSubLayerCommand::execute(const CommandArgs& args)
{
    auto parent_arg = args.get_arg<std::string>(0);
    auto sublayer_arg = args.get_arg<std::string>(1);
    if (!parent_arg || !sublayer_arg)
    {
        OPENDCC_WARN("Failed to add sublayer: parent and sublayer identifiers are required.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    SdfLayerRefPtr parent_layer = SdfLayer::Find(parent_arg->get_value());
    if (!parent_layer)
        parent_layer = SdfLayer::FindOrOpen(parent_arg->get_value());
    if (!parent_layer)
    {
        OPENDCC_WARN("Failed to add sublayer: parent layer '{}' could not be found.", parent_arg->get_value().c_str());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    const std::string& sublayer_identifier = sublayer_arg->get_value();
    int index = 0;
    if (auto index_kwarg = args.get_kwarg<int>("index"))
        index = index_kwarg->get_value();
    const bool anonymous = args.get_kwarg<bool>("anonymous") && args.get_kwarg<bool>("anonymous")->get_value();
    if (!parent_layer->PermissionToEdit() || sublayer_identifier.empty() || index < 0 ||
        index > static_cast<int>(parent_layer->GetSubLayerPaths().size()) ||
        (!anonymous && commands::utils::find_sublayer_index(parent_layer, sublayer_identifier) >= 0))
        return CommandResult(CommandResult::Status::INVALID_ARG);

    auto sublayer =
        anonymous ? SdfLayer::CreateAnonymous(sublayer_identifier) : SdfLayer::FindOrOpenRelativeToLayer(parent_layer, sublayer_identifier);
    if (sublayer && commands::utils::find_sublayer_index(parent_layer, sublayer->GetIdentifier()) >= 0)
        return CommandResult(CommandResult::Status::INVALID_ARG);
    if (!sublayer)
    {
        const auto path = SdfComputeAssetPathRelativeToLayer(parent_layer, sublayer_identifier);
        if (ArGetResolver().Resolve(path))
            return CommandResult(CommandResult::Status::FAIL);
        sublayer = SdfLayer::CreateNew(path);
    }
    if (!sublayer)
    {
        OPENDCC_WARN("Failed to add sublayer: layer '{}' could not be found or created.", sublayer_identifier.c_str());
        return CommandResult(CommandResult::Status::FAIL);
    }

    if (sublayer == parent_layer)
        return CommandResult(CommandResult::Status::INVALID_ARG);
    m_sublayer = sublayer;
    m_parent = parent_layer;
    const auto paths = parent_layer->GetSubLayerPaths();
    m_paths.assign(paths.begin(), paths.end());
    m_offsets = parent_layer->GetSubLayerOffsets();
    {
        UsdEditsBlock change_block;
        parent_layer->InsertSubLayerPath(anonymous ? sublayer->GetIdentifier() : sublayer_identifier, index);
        m_inverse = change_block.take_edits();
    }

    return CommandResult(CommandResult::Status::SUCCESS, sublayer->GetIdentifier());
}

void commands::AddSubLayerCommand::do_cmd()
{
    if (m_inverse && m_inverse->size())
        m_inverse->invert();
    else
    {
        UsdEditsBlock edits;
        commands::utils::swap_sublayers(m_parent, m_paths, m_offsets);
    }
}

void commands::AddSubLayerCommand::undo()
{
    do_cmd();
}

void commands::AddSubLayerCommand::redo()
{
    do_cmd();
}

OPENDCC_NAMESPACE_CLOSE
