// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/apply_layer_text.h"
#include "opendcc/usd_editor/layer_cmds/sublayer_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/logging/logger.h"

#include <pxr/usd/sdf/layer.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

std::string commands::ApplyLayerTextCommand::cmd_name = "apply_layer_text";

CommandSyntax commands::ApplyLayerTextCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("layer_identifier", "Identifier of the layer to update")
        .arg<std::string>("text", "Replacement USDA text")
        .result<bool>("Whether the layer contents were applied")
        .description("Replaces layer contents with validated USDA text.");
}

std::shared_ptr<Command> commands::ApplyLayerTextCommand::create_cmd()
{
    return std::make_shared<commands::ApplyLayerTextCommand>();
}

CommandResult commands::ApplyLayerTextCommand::execute(const CommandArgs& args)
{
    auto identifier = args.get_arg<std::string>(0);
    auto text = args.get_arg<std::string>(1);
    if (!identifier || !text)
        return CommandResult(CommandResult::Status::INVALID_ARG);
    auto layer = SdfLayer::Find(identifier->get_value());
    if (!layer || !layer->PermissionToEdit())
        return CommandResult(CommandResult::Status::INVALID_ARG);

    auto parsed = SdfLayer::CreateAnonymous("layer_text.usda");
    if (!parsed->ImportFromString(text->get_value()))
        return CommandResult(CommandResult::Status::FAIL);

    m_layer = layer;
    m_retained_layers = commands::utils::retain_layer_tree(layer);
    m_contents = SdfLayer::CreateAnonymous("layer_text_undo.usda");
    m_contents->TransferContent(layer);
    UsdEditsBlock change_block;
    layer->TransferContent(parsed);
    m_inverse = change_block.take_edits();
    auto applied_layers = commands::utils::retain_layer_tree(layer);
    m_retained_layers.insert(m_retained_layers.end(), applied_layers.begin(), applied_layers.end());
    return CommandResult(CommandResult::Status::SUCCESS, true);
}

void commands::ApplyLayerTextCommand::do_cmd()
{
    if (m_inverse && m_inverse->size())
        m_inverse->invert();
    else
    {
        auto current = SdfLayer::CreateAnonymous("layer_text_undo.usda");
        current->TransferContent(m_layer);
        UsdEditsBlock edits;
        m_layer->TransferContent(m_contents);
        m_contents = current;
    }
}

void commands::ApplyLayerTextCommand::undo()
{
    do_cmd();
}

void commands::ApplyLayerTextCommand::redo()
{
    do_cmd();
}

OPENDCC_NAMESPACE_CLOSE
