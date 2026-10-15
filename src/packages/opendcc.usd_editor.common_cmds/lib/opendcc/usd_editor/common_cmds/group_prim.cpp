// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/group_prim.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/usd_editor/common_cmds/parent_prim.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/router.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::GroupPrimCommand::cmd_name = "group_prim";

CommandSyntax commands::GroupPrimCommand::cmd_syntax()
{
    return CommandSyntax()
        .kwarg<std::vector<UsdPrim>>("prims", "List of prims to group")
        .kwarg<UsdStageWeakPtr>("stage", "Stage on which the prims are grouped")
        .kwarg<SdfPathVector>("paths", "List of SdfPaths of the prims that are grouped")
        .description("Group prims into a new Xform at the common parent of the selected prims.")
        .result<SdfPathVector>();
}

std::shared_ptr<Command> commands::GroupPrimCommand::create_cmd()
{
    return std::make_shared<commands::GroupPrimCommand>();
}

CommandResult commands::GroupPrimCommand::execute(const CommandArgs& args)
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
        stage = *stage_kwarg;
    else
        stage = Application::instance().get_session()->get_current_stage();

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
        return CommandResult(CommandResult::Status::INVALID_ARG);

    m_old_selection = Application::instance().get_selection();
    m_create_cmd = change_block.take_edits();

    m_parent_cmd = std::make_unique<ParentPrimCommand>();
    if (!m_parent_cmd->execute(CommandArgs().arg(group_path).kwarg("stage", stage).kwarg("paths", prim_paths)))
    {
        stage->RemovePrim(group_path);
        return CommandResult(CommandResult::Status::FAIL);
    }

    m_new_selection = SelectionList({ group_path });
    Application::instance().set_selection(m_new_selection);
    return CommandResult(CommandResult::Status::SUCCESS);
}

void commands::GroupPrimCommand::undo()
{
    {
        SdfChangeBlock block;
        if (m_parent_cmd)
            m_parent_cmd->undo();
        if (m_create_cmd)
            m_create_cmd->invert();
    }
    Application::instance().set_selection(m_old_selection);
}

void commands::GroupPrimCommand::redo()
{
    {
        SdfChangeBlock block;
        if (m_create_cmd)
            m_create_cmd->invert();
        if (m_parent_cmd)
            m_parent_cmd->redo();
    }
    Application::instance().set_selection(m_new_selection);
}

PXR_NS::SdfPath commands::GroupPrimCommand::define_group_root(PXR_NS::UsdStageWeakPtr stage, const SdfPathVector& prim_paths) const
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
    auto group_name = utils::get_new_name_for_prim(TfToken("group1"), stage->GetPrimAtPath(common_parent));
    auto group_path = common_parent.AppendChild(group_name);
    auto group_prim = stage->DefinePrim(group_path, TfToken("Xform"));
    if (!group_prim)
    {
        OPENDCC_WARN("Failed to create group prim.\n");
        return SdfPath::EmptyPath();
    }
    return group_prim.GetPrimPath();
}
OPENDCC_NAMESPACE_CLOSE
