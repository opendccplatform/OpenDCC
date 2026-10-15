// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/parent_prim.h"
#include "opendcc/app/core/undo/block.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/core/undo/router.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::ParentPrimCommand::cmd_name = "parent_prim";

CommandSyntax commands::ParentPrimCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPath>("parent_path", "New parent for target prims")
        .kwarg<std::vector<UsdPrim>>("prims", "Prims to parent")
        .kwarg<SdfPathVector>("paths", "Paths to parent")
        .kwarg<UsdStageWeakPtr>("stage", "Stage to parent")
        .kwarg<bool>("preserve_transform", "Preserve prims' transform")
        .result<SdfPathVector>("New prims' paths.");
}

std::shared_ptr<Command> commands::ParentPrimCommand::create_cmd()
{
    return std::make_shared<commands::ParentPrimCommand>();
}

CommandResult commands::ParentPrimCommand::execute(const CommandArgs& args)
{
    SdfPathVector old_paths;
    UsdStageWeakPtr stage;
    if (auto prims_arg = args.get_kwarg<std::vector<UsdPrim>>("prims"))
    {
        for (const auto& p : prims_arg->get_value())
        {
            if (!p)
            {
                OPENDCC_WARN("Failed to reparent prim at path '{}': prim doesn't exist.", p.GetPath().GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            old_paths.push_back(p.GetPrimPath());
            if (!stage)
            {
                stage = p.GetStage();
            }
            else if (stage != p.GetStage())
            {
                OPENDCC_WARN("Failed to reparent prim at path '{}': prims defined at different stages.", p.GetPath().GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
        }
    }
    else if (auto paths_arg = args.get_kwarg<SdfPathVector>("paths"))
    {
        old_paths = *paths_arg;
    }
    else
    {
        old_paths = Application::instance().get_prim_selection();
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
        stage = *stage_kwarg;
    else
        stage = Application::instance().get_session()->get_current_stage();

    auto parent_path = args.get_arg<SdfPath>(0)->get_value();
    if (parent_path.IsEmpty())
    {
        OPENDCC_WARN("Failed to reparent prims: new parent path is empty.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (old_paths.empty())
    {
        OPENDCC_WARN("Failed to reparent prims: no valid prims were specified.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto new_parent_prim = get_parent_prim(stage, parent_path);
    if (!new_parent_prim)
        return CommandResult(CommandResult::Status::INVALID_ARG);

    SdfPathVector reparented_paths;
    UsdEditsBlock change_block;
    SdfBatchNamespaceEdit batch;
    {
        SdfChangeBlock sdf_change_block;

        const auto& layer = stage->GetEditTarget().GetLayer();
        SdfPrimSpecHandle created_parent_spec;
        if (!layer->GetObjectAtPath(parent_path))
        {
            OPENDCC_DEBUG("Over prim at path '{}' defined in other layer.", parent_path.GetText());
            created_parent_spec = SdfCreatePrimInLayer(layer, parent_path);
        }

        for (const auto& path : old_paths)
        {
            const auto prim = stage->GetPrimAtPath(path);
            if (!prim)
            {
                OPENDCC_WARN("Failed to reparent prim at path '{}': prim doesn't exist.", path.GetText());
                layer->RemovePrimIfInert(created_parent_spec);
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }

            if (parent_path == path.GetParentPath())
            {
                OPENDCC_DEBUG("Unable to reparent prim at path '{}': prim already has the same parent.", path.GetText());
                continue;
            }

            if (path == parent_path)
            {
                OPENDCC_WARN("Failed to reparent prim at path '{}': unable to reparent prim to himself.", path.GetText());
                layer->RemovePrimIfInert(created_parent_spec);
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }

            const auto new_name = utils::get_new_name_for_prim(path.GetNameToken(), new_parent_prim, reparented_paths);
            const auto new_path = parent_path.AppendChild(new_name);
            const auto new_edit = SdfNamespaceEdit::ReparentAndRename(path, parent_path, new_name, 0);

            m_old_paths.push_back(new_edit.currentPath);
            m_new_paths.push_back(new_edit.newPath);
            batch.Add(new_edit);
            reparented_paths.push_back(new_path);
        }

        SdfNamespaceEditDetailVector details;
        if (layer->CanApply(batch, &details))
        {
            for (const auto& edit : batch.GetEdits())
            {
                if (args.has_kwarg("preserve_transform") && args.get_kwarg<bool>("preserve_transform")->get_value())
                    utils::preserve_transform(stage->GetPrimAtPath(edit.currentPath), new_parent_prim);
                utils::rename_targets(stage, edit.currentPath, edit.newPath);
            }
            if (!layer->Apply(batch))
            {
                OPENDCC_WARN("Failed to reparent prims.");
                return CommandResult(CommandResult::Status::FAIL);
            }
        }
        else
        {
            for (const auto& detail : details)
            {
                OPENDCC_WARN("Failed to reparent prims: %s", detail.reason.c_str());
                return CommandResult(CommandResult::Status::FAIL);
            }
        }
        get_notifier().notify(m_old_paths, m_new_paths);
    }

    m_inverse = change_block.take_edits();
    m_old_selection = Application::instance().get_selection();
    Application::instance().set_prim_selection(reparented_paths);

    return CommandResult { CommandResult::Status::SUCCESS, reparented_paths };
}

void commands::ParentPrimCommand::undo()
{
    get_notifier().notify(m_new_paths, m_old_paths);
    do_cmd();
}

void commands::ParentPrimCommand::redo()
{
    get_notifier().notify(m_old_paths, m_new_paths);
    do_cmd();
}

commands::ParentCommandNotifier& commands::ParentPrimCommand::get_notifier()
{
    static commands::ParentCommandNotifier notifier;
    return notifier;
}

UsdPrim commands::ParentPrimCommand::get_parent_prim(UsdStageWeakPtr stage, const PXR_NS::SdfPath& prim_path) const
{
    if (!stage.IsExpired() && !stage.IsInvalid())
    {
        if (auto prim = stage->GetPrimAtPath(prim_path))
        {
            return prim;
        }
        else
        {
            OPENDCC_WARN("Failed to parent to prim at path '{}': prim doesn't exist.", prim_path.GetText());
            return UsdPrim();
        }
    }
    OPENDCC_WARN("Failed to parent to prim at path '{}': stage doesn't exist.", prim_path.GetText());
    return UsdPrim();
}

void commands::ParentPrimCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
    auto cur_selection = Application::instance().get_selection();
    Application::instance().set_selection(m_old_selection);
    m_old_selection = cur_selection;
}
OPENDCC_NAMESPACE_CLOSE
