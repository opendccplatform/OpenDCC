// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/layout_command.h"
#include "opendcc/ui/node_editor/layout.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/ui/node_editor/scene.h"
#include "opendcc/ui/node_editor/node.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/base/tf/type.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<NodeEditorLayoutCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command("node_editor_layout2",
                                      CommandSyntax().arg<NodeEditorScene*, int64_t>("scene").kwarg<bool>("vertical").kwarg<bool>("only_selected"),
                                      [] { return std::make_shared<NodeEditorLayoutCommand>(); });
}

void NodeEditorLayoutCommand::undo()
{
    if (!m_scene)
        return;

    m_scene->begin_move(m_nodes);
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        auto item = m_scene->get_item_for_node(m_nodes[i]);
        if (!item)
            continue;
        item->setPos(m_old_pos[i]);
    }
    m_scene->end_move();
}

void NodeEditorLayoutCommand::redo()
{
    if (!m_scene)
        return;

    m_scene->begin_move(m_nodes);
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        auto item = m_scene->get_item_for_node(m_nodes[i]);
        if (!item)
            continue;
        item->setPos(m_new_pos[i]);
    }
    m_scene->end_move();
}

CommandResult NodeEditorLayoutCommand::execute(const CommandArgs& args)
{
    NodeEditorScene* scene = nullptr;
    if (auto arg = args.get_arg<NodeEditorScene*>(0))
        scene = arg->get_value();
    if (!scene)
    {
        if (auto arg = args.get_arg<int64_t>(0))
            scene = reinterpret_cast<NodeEditorScene*>(arg->get_value());
    }
    if (!scene)
        return CommandResult::Status::INVALID_ARG;

    const auto only_selected = args.get_kwarg<bool>("only_selected") && args.get_kwarg<bool>("only_selected")->get_value();

    const auto node_items = only_selected ? scene->get_selected_node_items() : scene->get_node_items();

    if (node_items.empty())
        return CommandResult::Status::SUCCESS;

    m_nodes.clear();
    m_old_pos.clear();
    m_new_pos.clear();
    m_nodes.reserve(node_items.size());
    m_old_pos.reserve(node_items.size());
    m_new_pos.reserve(node_items.size());
    for (auto item : node_items)
    {
        m_nodes.push_back(item->get_id());
        m_old_pos.push_back(item->scenePos());
    }

    const auto vertical = args.get_kwarg<bool>("vertical") && args.get_kwarg<bool>("vertical")->get_value();
    layout_items(node_items, vertical);

    for (auto item : node_items)
    {
        m_new_pos.push_back(item->scenePos());
    }
    return CommandResult(CommandResult::Status::SUCCESS);
}

OPENDCC_NAMESPACE_CLOSE
