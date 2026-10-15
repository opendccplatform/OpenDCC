// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/pick_walk.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include "opendcc/base/commands_api/core/command_registry.h"
#include <iostream>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::PickWalkCommand::cmd_name = "pick_walk";

CommandSyntax commands::PickWalkCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<TfToken>("direction", "The direction to walk from the prim")
        .result<SdfPathVector>("Selected prims' paths.")
        .description("The pick_walk command allows you to quickly change the selection list relative to the prims that are currently selected.");
}

std::shared_ptr<Command> commands::PickWalkCommand::create_cmd()
{
    return std::make_shared<commands::PickWalkCommand>();
}

static UsdPrim get_sibling(UsdPrim& prim, bool forward) // it's not the prettiest code in the world but it works
{
    if (auto parent = prim.GetParent())
    {
        auto children = parent.GetChildren();
        int index = -1;
        std::vector<UsdPrim> siblings;
        for (auto child : children)
        {
            if (child == prim)
                index = siblings.size();

            siblings.push_back(child);
        }
        if (index != -1)
        {
            index = index + (forward ? 1 : -1);
            if (index < 0)
                index = siblings.size() + index;
            index = index % siblings.size();
            return siblings[index];
        }
    }
    return UsdPrim();
}

CommandResult commands::PickWalkCommand::execute(const CommandArgs& args)
{
    m_old_selection = Application::instance().get_selection();
    UsdStageWeakPtr stage = Application::instance().get_session()->get_current_stage();

    if (!stage)
    {
        OPENDCC_WARN("Failed to pick walk: stage doesn't exist.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    SdfPathSet selection_set;

    auto direction = args.get_arg<TfToken>(0)->get_value();

    if (direction == "up")
    {
        for (auto& sel_data : m_old_selection)
        {
            auto prim_path = sel_data.first;
            if (auto prim = stage->GetPrimAtPath(prim_path))
            {
                if (auto parent = prim.GetParent())
                {
                    if (!parent.IsPseudoRoot()) // TODO: fix this
                    {
                        selection_set.insert(parent.GetPath());
                    }
                    else
                    {
                        selection_set.insert(prim.GetPath());
                    }
                }
            }
        }
    }
    else if (direction == "down")
    {
        for (auto& sel_data : m_old_selection)
        {
            auto prim_path = sel_data.first;
            if (auto prim = stage->GetPrimAtPath(prim_path))
            {
                auto children = prim.GetChildren();
                if (!children.empty())
                {
                    selection_set.insert(children.front().GetPath());
                }
                else
                {
                    selection_set.insert(prim.GetPath());
                }
            }
        }
    }
    else if (direction == "left")
    {
        for (auto& sel_data : m_old_selection)
        {
            auto prim_path = sel_data.first;
            if (auto prim = stage->GetPrimAtPath(prim_path))
            {
                auto sibling = get_sibling(prim, false);
                if (sibling.IsValid())
                {
                    selection_set.insert(sibling.GetPath());
                }
            }
        }
    }
    else if (direction == "right")
    {
        for (auto& sel_data : m_old_selection)
        {
            auto prim_path = sel_data.first;
            if (auto prim = stage->GetPrimAtPath(prim_path))
            {
                auto sibling = get_sibling(prim, true);
                if (sibling.IsValid())
                {
                    selection_set.insert(sibling.GetPath());
                }
            }
        }
    }
    else
    {
        OPENDCC_WARN("Unknown direction");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    SdfPathVector result(selection_set.size());
    std::copy(selection_set.begin(), selection_set.end(), result.begin());

    Application::instance().set_prim_selection(result);
    return CommandResult { CommandResult::Status::SUCCESS, result };
}

void commands::PickWalkCommand::undo()
{
    do_cmd();
}

void commands::PickWalkCommand::redo()
{
    do_cmd();
}

void commands::PickWalkCommand::do_cmd()
{
    auto cur_selection = Application::instance().get_selection();
    Application::instance().set_selection(m_old_selection);
    m_old_selection = cur_selection;
}

OPENDCC_NAMESPACE_CLOSE
