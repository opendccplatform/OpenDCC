// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/assign_material.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "opendcc/base/commands_api/core/command_registry.h"

#include <QDebug>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::AssignMaterialCommand::cmd_name = "assign_material";

CommandSyntax commands::AssignMaterialCommand::cmd_syntax()
{
    return CommandSyntax()
        .kwarg<SdfPath>("material", "Material to assign")
        .kwarg<std::vector<UsdPrim>>("prims", "If empty assign to selected prims")
        .kwarg<std::string>("material_purpose", "Material purpose");
}

std::shared_ptr<Command> commands::AssignMaterialCommand::create_cmd()
{
    return std::make_shared<commands::AssignMaterialCommand>();
}

CommandResult commands::AssignMaterialCommand::execute(const CommandArgs& args)
{
    auto session = Application::instance().get_session();
    UsdStageWeakPtr stage = Application::instance().get_session()->get_current_stage();

    if (!stage)
    {
        OPENDCC_WARN("Failed to assign material: stage doesn't exist.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    auto selection_list = Application::instance().get_selection();

    SdfPathVector prim_paths;

    if (auto prims_arg = args.get_kwarg<std::vector<UsdPrim>>("prims"))
    {
        for (const auto& p : prims_arg->get_value())
        {
            if (!p)
            {
                OPENDCC_WARN("Failed to assign material at path '{}': prim doesn't exists.", p.GetPath().GetText());
                return CommandResult(CommandResult::Status::INVALID_ARG);
            }
            prim_paths.push_back(p.GetPath());
        }
    }
    else
    {
        prim_paths = Application::instance().get_prim_selection();
    }

    if (prim_paths.empty())
    {
        OPENDCC_WARN("Failed to assign material: no prims.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    auto purpose = UsdShadeTokens->allPurpose;
    if (auto purpose_kwarg = args.get_kwarg<std::string>("material_purpose"))
    {
        std::string purpose_value = *purpose_kwarg;
        if (purpose_value == "full")
        {
            purpose = UsdShadeTokens->full;
        }
        else if (purpose_value == "preview")
        {
            purpose = UsdShadeTokens->preview;
        }
        else
        {
            OPENDCC_WARN("Failed to assign material: unknown purpose.");
            return CommandResult(CommandResult::Status::FAIL);
        }
    }

    SdfPath material_path;

    if (auto material_kwarg = args.get_kwarg<SdfPath>("material"))
    {
        material_path = *material_kwarg;
    }
    else
    {
        OPENDCC_WARN("Failed to assign material: material isn't set.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto material_prim = stage->GetPrimAtPath(material_path);

    if (!material_prim.IsValid())
    {
        OPENDCC_WARN("Failed to assign material: material isn't valid.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    UsdEditsBlock change_block;

    UsdShadeMaterial material(material_prim);

#if PXR_VERSION > 1911
    auto schema_type = UsdSchemaRegistry::GetAPITypeFromSchemaTypeName(TfToken("MaterialBindingAPI"));
#endif

    for (const auto& path : prim_paths)
    {
        auto prim = stage->GetPrimAtPath(path);

#if PXR_VERSION > 1911
        if (schema_type != TfType())
        {
            prim.ApplyAPI(schema_type);
        }
#endif

        auto selection_data = selection_list.get_selection_data(path);
        auto indices = selection_data.get_element_indices();

        if (indices.empty())
        {
            if (purpose == UsdShadeTokens->allPurpose)
            {
                auto binding_api = UsdShadeMaterialBindingAPI(prim);
                auto full_purpose_binding_rel = binding_api.GetDirectBindingRel(UsdShadeTokens->full);
                if (full_purpose_binding_rel.IsValid())
                {
                    binding_api.Bind(material, UsdShadeTokens->fallbackStrength, UsdShadeTokens->full);
                }
                else
                {
                    binding_api.Bind(material, UsdShadeTokens->fallbackStrength, UsdShadeTokens->allPurpose);
                }
            }
            else
            {
                UsdShadeMaterialBindingAPI(prim).Bind(material, UsdShadeTokens->fallbackStrength, purpose);
            }
        }
        else
        {
            pxr::VtIntArray vt_indices;
            for (const auto face_ind : indices)
            {
                vt_indices.push_back(face_ind);
            }

            auto element_type = TfToken("face");
            auto family_name = TfToken("materialBind");

            bool found_subset = false;
            for (auto subset : UsdGeomSubset::GetGeomSubsets(UsdGeomImageable(prim), element_type, family_name))
            {
                SdfPathVector binding_targets;
                UsdShadeMaterialBindingAPI(subset).GetDirectBindingRel().GetTargets(&binding_targets);
                for (auto target : binding_targets)
                {
                    if (target == material_path)
                    {
                        found_subset = true;
                        break;
                    }
                }

                if (found_subset)
                {
                    pxr::VtIntArray new_indices;
                    auto indices_attr = subset.GetIndicesAttr();
                    indices_attr.Get(&new_indices);
                    for (auto index : vt_indices)
                    {
                        if (!(std::find(new_indices.begin(), new_indices.end(), index) != new_indices.end()))
                        {
                            new_indices.push_back(index);
                        }
                    }
                    indices_attr.Set(new_indices);
                    vt_indices = new_indices;
                }
            }

            if (!found_subset)
            {
                std::string subset_name = material_path.GetNameToken().GetString();
                subset_name = subset_name + "_subset";
                auto subset_token = commands::utils::get_new_name_for_prim(TfToken(subset_name), prim);

                auto subset = UsdGeomSubset::CreateGeomSubset(UsdGeomImageable(prim), subset_token, element_type, vt_indices, family_name);
#if PXR_VERSION > 1911
                subset.GetPrim().ApplyAPI(schema_type);
#endif
                UsdShadeMaterialBindingAPI(subset).Bind(material, UsdShadeTokens->fallbackStrength, purpose);
            }

            // cleanup
            for (auto subset : UsdGeomSubset::GetGeomSubsets(UsdGeomImageable(prim), element_type, family_name))
            {
                SdfPathVector binding_targets;
                UsdShadeMaterialBindingAPI(subset).GetDirectBindingRel().GetTargets(&binding_targets);

                bool found_material = false;
                for (auto target : binding_targets)
                {
                    if (target == material_path)
                    {
                        found_material = true;
                        break;
                    }
                }

                if (!found_material)
                {
#if PXR_VERSION > 1911
                    pxr::VtIntArray new_indices;
                    auto indices_attr = subset.GetIndicesAttr();
                    indices_attr.Get(&new_indices);
                    bool indices_changed = false;
                    for (auto index : vt_indices)
                    {
                        auto result = std::find(new_indices.begin(), new_indices.end(), index);
                        if (result != new_indices.end())
                        {
                            indices_changed = true;
                            new_indices.erase(result);
                        }
                    }
                    if (indices_changed)
                    {
                        indices_attr.Set(new_indices);
                    }
#else
                    pxr::VtIntArray new_indices;
                    auto indices_attr = subset.GetIndicesAttr();
                    indices_attr.Get(&new_indices);

                    std::vector<int> buffer;
                    for (const auto& index : new_indices)
                    {
                        buffer.push_back(index);
                    }

                    bool indices_changed = false;
                    for (auto index : buffer)
                    {
                        auto result = std::find(buffer.begin(), buffer.end(), index);
                        if (result != buffer.end())
                        {
                            indices_changed = true;
                            buffer.erase(result);
                        }
                    }
                    if (indices_changed)
                    {
                        pxr::VtIntArray result;
                        for (const auto& index : buffer)
                        {
                            result.push_back(index);
                        }
                        indices_attr.Set(result);
                    }
#endif
                }
            }
        }
    }

    m_inverse = change_block.take_edits();
    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::AssignMaterialCommand::undo()
{
    do_cmd();
}

void commands::AssignMaterialCommand::redo()
{
    do_cmd();
}

void commands::AssignMaterialCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}
OPENDCC_NAMESPACE_CLOSE
