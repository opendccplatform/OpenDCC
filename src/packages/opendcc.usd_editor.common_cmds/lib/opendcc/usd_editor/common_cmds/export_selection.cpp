// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/export_selection.h"
#include "opendcc/app/core/command_utils.h"
#include <pxr/usd/sdf/copyUtils.h>
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::ExportSelectionCommand::cmd_name = "export_selection";

CommandSyntax commands::ExportSelectionCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("path", "Export path")
        .kwarg<UsdStageWeakPtr>("stage", "Stage from which prims will be exported")
        .kwarg<SdfPathVector>("paths", "List of SdfPaths of the prims that are exported")
        .kwarg<std::vector<UsdPrim>>("prims", "List of prims to export")
        .kwarg<bool>("collapsed", "Flatten layers for the prims that are exported")
        .kwarg<bool>("export_parents", "Export parents of the selected prims")
        .kwarg<bool>("export_root", "Export Metadata of the pseudo-root")
        .kwarg<bool>("export_connections", "Export connections of selected prims")
        .description("Export selected prims to a file")
        .result<SdfPathVector>();
}
std::shared_ptr<Command> commands::ExportSelectionCommand::create_cmd()
{
    return std::make_shared<commands::ExportSelectionCommand>();
}

CommandResult commands::ExportSelectionCommand::execute(const CommandArgs& args)
{
    std::string file_path = *args.get_arg<std::string>(0);

    SdfPathVector prim_paths;
    UsdStageWeakPtr stage;
    if (auto prims_arg = args.get_kwarg<std::vector<UsdPrim>>("prims"))
    {
        for (const auto& p : prims_arg->get_value())
        {
            if (!p)
            {
                OPENDCC_WARN("Failed to export prim at path '{}': prim doesn't exists.", p.GetPath().GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            if (!stage)
            {
                stage = p.GetStage();
            }
            else if (stage != p.GetStage())
            {
                OPENDCC_WARN("Failed to export prim at path '{}': prims defined at different stages.", p.GetPath().GetText());
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
        OPENDCC_WARN("Failed to export prims: stage doesn't exists.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (prim_paths.empty())
    {
        OPENDCC_WARN("Failed to export prims: prim paths are empty.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    bool collapsed = false;
    if (auto collapsed_kwarg = args.get_kwarg<bool>("collapsed"))
        collapsed = *collapsed_kwarg;

    bool export_parents = true;
    if (auto export_parents_kwarg = args.get_kwarg<bool>("export_parents"))
        export_parents = *export_parents_kwarg;

    bool export_root = true;
    if (auto export_root_kwarg = args.get_kwarg<bool>("export_root"))
        export_root = *export_root_kwarg;

    bool export_connections = true;
    if (auto export_connections_kwarg = args.get_kwarg<bool>("export_connections"))
        export_connections = *export_connections_kwarg;

    SdfPathVector exported_paths;
    CommandResult result;

    auto new_stage = UsdStage::CreateInMemory(file_path);
    if (!new_stage)
    {
        OPENDCC_WARN("Failed to open stage \"{}\"", file_path.c_str());
        result = CommandResult(CommandResult::Status::FAIL);
        return result;
    }

    auto dst_layer = new_stage->GetRootLayer();
    const auto& src_layer = stage->GetEditTarget().GetLayer();

    SdfPathVector resolved_paths(prim_paths);
    std::set<UsdPrim> parents;

    auto copy_spec = [&](const UsdPrim& src_prim, const SdfPath& path) -> bool {
        if (collapsed)
        {
            utils::flatten_prim(src_prim, path, dst_layer, false);
            exported_paths.push_back(path);
        }
        else
        {
            SdfCreatePrimInLayer(dst_layer, path);
            if (utils::copy_spec_without_children(src_layer, path, dst_layer, path))
            {
                exported_paths.push_back(path);
            }
            else
            {
                OPENDCC_WARN("Can't copy PrimSpec. Source PrimSpec is on another layer.");
                return false;
            }
        }
        return true;
    };

    std::function<bool(const UsdPrim&)> traverse = [&](const UsdPrim& prim) -> bool {
        auto path = prim.GetPath();

        /*
        // there's probably no need for this
        for (const auto& child : prim.GetChildren())
        {
            if (!traverse(child))
                return false;
        }
        */

        for (const auto& relationship : prim.GetRelationships())
        {
            SdfPathVector targets;
            if (relationship.GetTargets(&targets))
            {
                for (const auto& target : targets)
                {
                    auto target_path = target.GetPrimPath();
                    auto target_prim = stage->GetPrimAtPath(target_path);

                    if (target_prim)
                    {
                        SdfCreatePrimInLayer(dst_layer, target_path);

                        if (!copy_spec(target_prim, target_path))
                            return false;

                        if (!traverse(target_prim))
                            return false;
                    }
                }
            }
        }

        for (const auto& attribute : prim.GetAttributes())
        {
            SdfPathVector connections;
            if (attribute.GetConnections(&connections))
            {
                for (const auto& connection : connections)
                {
                    auto connection_path = connection.GetPrimPath();
                    auto connection_prim = stage->GetPrimAtPath(connection_path);

                    if (connection_prim)
                    {
                        SdfCreatePrimInLayer(dst_layer, connection_path);

                        if (!copy_spec(connection_prim, connection_path))
                            return false;

                        if (!traverse(connection_prim))
                            return false;
                    }
                }
            }
        }

        if (export_parents)
        {
            auto prim_parent = prim.GetParent();
            while (prim_parent)
            {
                if (prim_parent.IsPseudoRoot())
                    break;

                parents.insert(prim_parent);
                prim_parent = prim_parent.GetParent();
            }
        }

        return true;
    };

    UsdEditsBlock change_block;
    SdfPath::RemoveDescendentPaths(&resolved_paths);
    {
        for (const auto& path : resolved_paths)
        {
            auto src_prim = stage->GetPrimAtPath(path);

            if (export_parents)
            {
                auto src_prim_parent = src_prim.GetParent();
                while (src_prim_parent)
                {
                    if (src_prim_parent.IsPseudoRoot())
                        break;

                    parents.insert(src_prim_parent);
                    src_prim_parent = src_prim_parent.GetParent();
                }
            }

            if (collapsed)
            {
                utils::flatten_prim(src_prim, path, dst_layer);
                exported_paths.push_back(path);
            }
            else
            {
                SdfCreatePrimInLayer(dst_layer, path);
                if (SdfCopySpec(src_layer, path, dst_layer, path))
                {
                    exported_paths.push_back(path);
                }
                else
                {
                    OPENDCC_WARN("Can't copy PrimSpec. Source PrimSpec is on another layer.");
                    result = CommandResult(CommandResult::Status::INVALID_ARG);
                    return result;
                }
            }

            if (export_connections)
            {
                if (!traverse(src_prim))
                {
                    OPENDCC_WARN("Failed to export connections.");
                    result = CommandResult(CommandResult::Status::FAIL);
                    return result;
                }
            }
        }

        for (const auto& src_prim : parents)
        {
            auto path = src_prim.GetPath();
            copy_spec(src_prim, path);
        }

        if (export_root)
        {
            const auto& metadata = stage->GetPseudoRoot().GetAllMetadata();

            for (const auto& item : metadata)
            {
                new_stage->GetPseudoRoot().SetMetadata(item.first, item.second);
            }
        }
    }

    if (!new_stage->GetRootLayer()->Export(file_path))
    {
        OPENDCC_WARN("Failed to save file \"{}\"", file_path.c_str());
        result = CommandResult(CommandResult::Status::FAIL);
        return result;
    }

    result = CommandResult(CommandResult::Status::SUCCESS, exported_paths);
    return result;
}

OPENDCC_NAMESPACE_CLOSE
