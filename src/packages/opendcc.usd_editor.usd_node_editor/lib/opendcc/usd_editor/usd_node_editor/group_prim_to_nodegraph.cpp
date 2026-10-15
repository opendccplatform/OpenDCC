// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/group_prim_to_nodegraph.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/usd_editor/common_cmds/parent_prim.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/router.h"
#include <pxr/usd/usdUI/nodeGraphNodeAPI.h>
#include <algorithm>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<commands::GroupPrimToNodeGraphCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command(
        "group_prim_to_nodegraph", CommandSyntax().kwarg<std::vector<UsdPrim>>("prims").kwarg<UsdStageWeakPtr>("stage").kwarg<SdfPathVector>("paths"),
        [] { return std::make_shared<commands::GroupPrimToNodeGraphCommand>(); });
}

CommandResult commands::GroupPrimToNodeGraphCommand::execute(const CommandArgs& args)
{
    SdfPathVector prim_paths;
    UsdStageWeakPtr stage;
    if (auto prims_arg = args.get_kwarg<std::vector<UsdPrim>>("prims"))
    {
        for (const auto& prim : prims_arg->get_value())
        {
            if (!stage)
            {
                stage = prim.GetStage();
            }
            else if (stage != prim.GetStage())
            {
                OPENDCC_WARN("Failed to group prim: prims are defined at different stages.\n");
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            prim_paths.push_back(prim.GetPrimPath());
        }
    }
    else if (auto paths_arg = args.get_kwarg<SdfPathVector>("paths"))
    {
        prim_paths = *paths_arg;
    }
    else
    {
        prim_paths = Application::instance().get_prim_selection();
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
    {
        stage = *stage_kwarg;
    }
    else
    {
        stage = Application::instance().get_session()->get_current_stage();
    }

    if (!stage)
    {
        OPENDCC_WARN("Failed to group prims: no valid stage was specified.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }
    if (prim_paths.empty())
    {
        OPENDCC_WARN("Failed to group prims: no valid prims to group were specified.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    UsdEditsBlock change_block;
    auto group_path = define_group_root(stage, prim_paths);
    if (group_path.IsEmpty())
    {
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    m_old_selection = Application::instance().get_selection();

    auto selection_prim = stage->GetPrimAtPath(m_old_selection.begin()->first.GetPrimPath());
    auto node_api = UsdUINodeGraphNodeAPI(selection_prim);
    if (!node_api)
    {
        UsdUINodeGraphNodeAPI::Apply(selection_prim);
    }

    auto pos_attr = node_api.GetPosAttr();
    VtValue pos_value;
    pos_attr.Get(&pos_value);
    auto group_prim = stage->GetPrimAtPath(group_path);
    node_api = UsdUINodeGraphNodeAPI(group_prim);
    if (!node_api)
    {
        UsdUINodeGraphNodeAPI::Apply(group_prim);
    }
    node_api.CreatePosAttr(pos_value);

    SdfPathVector new_path_old_selections;
    m_parent_cmd = std::make_unique<ParentPrimCommand>();
    auto result = m_parent_cmd->execute(CommandArgs().arg(group_path).kwarg("stage", stage).kwarg("paths", prim_paths));
    if (result.get_status() != CommandResult::Status::SUCCESS)
    {
        stage->RemovePrim(group_path);
        return CommandResult(CommandResult::Status::FAIL);
    }
    else
    {
        new_path_old_selections = *result.get_result<SdfPathVector>();
    }

    for (const auto path : new_path_old_selections)
    {
        auto prim = stage->GetPrimAtPath(path);
        for (const auto attr : prim.GetAttributes())
        {
            if (!attr.HasAuthoredConnections())
            {
                continue;
            }

            SdfPathVector attr_connections;
            attr.GetConnections(&attr_connections);
            for (auto connection : attr_connections)
            {
                SdfChangeBlock change_block;
                bool connection_to_prim_in_nodegraph = std::find(new_path_old_selections.begin(), new_path_old_selections.end(),
                                                                 connection.GetPrimPath()) != new_path_old_selections.end();
                bool connection_from_output = attr.GetName().GetString().find("outputs") != std::string::npos && prim.GetTypeName() == "NodeGraph";
                if (connection_to_prim_in_nodegraph || connection_from_output)
                {
                    continue;
                }
                auto group_attr = group_prim.CreateAttribute(attr.GetName(), attr.GetTypeName());
                group_attr.AddConnection(connection);
                attr.RemoveConnection(connection);
                attr.AddConnection(group_attr.GetPath());
            }
        }
    }

    std::vector<UsdPrim> prims_vector;
    auto parent_prim = group_prim.GetParent();
    prims_vector.push_back(parent_prim);
    for (const auto child_name : parent_prim.GetAllChildrenNames())
    {
        auto child = parent_prim.GetChild(child_name);
        if (child != group_prim)
        {
            prims_vector.push_back(parent_prim.GetChild(child_name));
        }
    }

    for (auto prim : prims_vector)
    {
        for (const auto attr : prim.GetAttributes())
        {
            if (!attr.HasAuthoredConnections())
            {
                continue;
            }

            SdfPathVector attr_connections;
            attr.GetConnections(&attr_connections);
            for (auto connection : attr_connections)
            {
                bool connection_outside_nodegraph = std::find(new_path_old_selections.begin(), new_path_old_selections.end(),
                                                              connection.GetPrimPath()) == new_path_old_selections.end();
                if (connection_outside_nodegraph)
                {
                    continue;
                }
                SdfChangeBlock change_block;
                auto group_attr = group_prim.CreateAttribute(connection.GetNameToken(), stage->GetAttributeAtPath(connection).GetTypeName());
                attr.AddConnection(group_attr.GetPath());
                group_attr.AddConnection(connection);
                attr.RemoveConnection(connection);
            }
        }
    }

    m_new_selection = SelectionList({ group_path });
    Application::instance().set_selection(m_new_selection);
    m_create_cmd = change_block.take_edits();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void commands::GroupPrimToNodeGraphCommand::undo()
{
    {
        SdfChangeBlock block;
        if (m_create_cmd)
            m_create_cmd->invert();
        if (m_parent_cmd)
            m_parent_cmd->undo();
    }
    Application::instance().set_selection(m_old_selection);
}

void commands::GroupPrimToNodeGraphCommand::redo()
{
    {
        SdfChangeBlock block;
        if (m_parent_cmd)
            m_parent_cmd->redo();
        if (m_create_cmd)
            m_create_cmd->invert();
    }
    Application::instance().set_selection(m_new_selection);
}

PXR_NS::SdfPath commands::GroupPrimToNodeGraphCommand::define_group_root(PXR_NS::UsdStageWeakPtr stage, const SdfPathVector& prim_paths) const
{
    if (!stage)
    {
        OPENDCC_WARN("Failed to group prims: stage doesn't exist.");
        return SdfPath::EmptyPath();
    }

    if (prim_paths.empty())
    {
        OPENDCC_WARN("Failed to group prims: prim paths are empty.");
        return SdfPath::EmptyPath();
    }

    auto common_parent = utils::get_common_parent(prim_paths);
    auto group_name = utils::get_new_name_for_prim(TfToken("NodeGraph"), stage->GetPrimAtPath(common_parent));
    auto group_path = common_parent.AppendChild(group_name);
    auto group_prim = stage->DefinePrim(group_path, TfToken("NodeGraph"));
    if (!group_prim)
    {
        OPENDCC_WARN("Failed to create group prim.\n");
        return SdfPath::EmptyPath();
    }
    return group_prim.GetPrimPath();
}
OPENDCC_NAMESPACE_CLOSE
