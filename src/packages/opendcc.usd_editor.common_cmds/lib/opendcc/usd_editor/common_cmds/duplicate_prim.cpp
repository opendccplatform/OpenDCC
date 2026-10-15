// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/duplicate_prim.h"
#include "opendcc/app/core/command_utils.h"
#include <pxr/usd/sdf/copyUtils.h>
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/undo/router.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::DuplicatePrimCommand::cmd_name = "duplicate_prim";

CommandSyntax commands::DuplicatePrimCommand::cmd_syntax()
{
    return CommandSyntax()
        .kwarg<UsdStageWeakPtr>("stage")
        .kwarg<SdfPathVector>("paths")
        .kwarg<std::vector<UsdPrim>>("prims")
        .kwarg<bool>("collapsed")
        .kwarg<bool>("each_layer")
        .result<SdfPathVector>();
}

std::shared_ptr<Command> commands::DuplicatePrimCommand::create_cmd()
{
    return std::make_shared<commands::DuplicatePrimCommand>();
}

CommandResult commands::DuplicatePrimCommand::execute(const CommandArgs& args)
{
    SdfPathVector prim_paths;
    UsdStageWeakPtr stage;
    if (auto prims_arg = args.get_kwarg<std::vector<UsdPrim>>("prims"))
    {
        for (const auto& p : prims_arg->get_value())
        {
            if (!p)
            {
                OPENDCC_WARN("Failed to duplicate prim at path '{}': prim doesn't exist.", p.GetPath().GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            if (!stage)
            {
                stage = p.GetStage();
            }
            else if (stage != p.GetStage())
            {
                OPENDCC_WARN("Failed to duplicate prim at path '{}': prims defined at different stages.", p.GetPath().GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            prim_paths.push_back(p.GetPath());
        }
    }
    else if (auto paths_kwarg = args.get_kwarg<SdfPathVector>("paths"))
    {
        prim_paths = *paths_kwarg;
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
        OPENDCC_WARN("Failed to duplicate prims: stage doesn't exist.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (prim_paths.empty())
    {
        OPENDCC_WARN("Failed to duplicate prims: prim paths are empty.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto collapsed = false;
    if (auto collapsed_kwarg = args.get_kwarg<bool>("collapsed"))
        collapsed = *collapsed_kwarg;
    auto each_layer = false;
    if (auto each_layer_arg = args.get_kwarg<bool>("each_layer"))
        each_layer = *each_layer_arg;

    SdfPathVector duplicated_paths;
    CommandResult result;
    struct Finalization
    {
        Finalization(UsdStageWeakPtr stage, const SdfPathVector& duplicated_paths, const CommandResult& result)
            : stage(stage)
            , duplicated_paths(duplicated_paths)
            , result(result)
        {
        }

        UsdStageWeakPtr stage;
        const CommandResult& result;
        const SdfPathVector& duplicated_paths;

        ~Finalization()
        {
            if (result.is_successful())
                return;

            UsdEditsBlock cb;
            for (const auto& path : duplicated_paths)
                stage->RemovePrim(path);
        }

    } finalization(stage, duplicated_paths, result);

    SdfPathVector resolved_paths(prim_paths);
    UsdEditsBlock change_block;
    SdfPath::RemoveDescendentPaths(&resolved_paths);
    {
        const auto& layer = stage->GetEditTarget().GetLayer();
        for (const auto& path : resolved_paths)
        {
            const auto new_name = utils::get_new_name_for_prim(path.GetNameToken(), stage->GetPrimAtPath(path.GetParentPath()), duplicated_paths);
            const auto new_path = path.GetParentPath().AppendChild(new_name);
            auto src_prim = stage->GetPrimAtPath(path);
            if (collapsed)
            {
                utils::flatten_prim(src_prim, new_path, src_prim.GetStage()->GetEditTarget().GetLayer());
                duplicated_paths.push_back(new_path);
            }
            else if (each_layer)
            {
                for (const auto& prim_spec : src_prim.GetPrimStack())
                {

                    if (!SdfCopySpec(prim_spec->GetLayer(), prim_spec->GetPath(), prim_spec->GetLayer(),
                                     prim_spec->GetPath().GetParentPath().AppendChild(new_name)))
                    {
                        OPENDCC_WARN("Failed to copy prim spec from layer '{}' at path '{}'.", prim_spec->GetLayer()->GetIdentifier().c_str(),
                                     new_path.GetString().c_str());
                        result = CommandResult(CommandResult::Status::INVALID_ARG);
                        return result;
                    }
                }
                duplicated_paths.push_back(new_path);
            }
            else
            {
                if (SdfCopySpec(layer, path, layer, new_path))
                {
                    duplicated_paths.push_back(new_path);
                }
                else
                {
                    OPENDCC_WARN("Can't copy PrimSpec. Source PrimSpec is on another layer.");
                    result = CommandResult(CommandResult::Status::INVALID_ARG);
                    return result;
                }
            }
        }
    }

    m_inverse = change_block.take_edits();

    m_old_selection = Application::instance().get_selection();
    Application::instance().set_prim_selection(duplicated_paths);
    result = CommandResult(CommandResult::Status::SUCCESS, duplicated_paths);
    return result;
}

void commands::DuplicatePrimCommand::undo()
{
    do_cmd();
}

void commands::DuplicatePrimCommand::redo()
{
    do_cmd();
}

void commands::DuplicatePrimCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
    auto cur_selection = Application::instance().get_selection();
    Application::instance().set_selection(m_old_selection);
    m_old_selection = cur_selection;
}
OPENDCC_NAMESPACE_CLOSE
