/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/usd_editor/common_cmds/prim_properties.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logger.h"

#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/stageLoadRules.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <algorithm>

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::CommandRegistry;
using base::commands::CommandResult;
using base::commands::CommandSyntax;
using base::commands::UndoCommand;

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    enum class Property
    {
        VISIBILITY,
        ACTIVE,
        TYPE,
        KIND,
        DRAW_MODE,
        VARIANT,
        PAYLOAD
    };

    struct PropertyCommand
    {
        const char* name;
        Property property;
        const char* description;
    };

    const PropertyCommand property_commands[] = {
        { "set_prim_visibility", Property::VISIBILITY, "Makes imageable prims visible or invisible." },
        { "set_prim_active", Property::ACTIVE, "Activates or deactivates prims." },
        { "set_prim_type", Property::TYPE, "Sets a prim type; an empty token clears the opinion." },
        { "set_prim_kind", Property::KIND, "Sets a prim kind; an empty token clears the opinion." },
        { "set_prim_draw_mode", Property::DRAW_MODE, "Sets model draw mode, applying the model API if necessary." },
        { "set_variant_selection", Property::VARIANT, "Selects a variant; an empty string clears the selection." },
        { "set_payload_loaded", Property::PAYLOAD, "Loads or unloads prim payloads, preserving load rules for undo." }
    };

    bool has_multiple_paths(Property property)
    {
        return property == Property::VISIBILITY || property == Property::ACTIVE || property == Property::PAYLOAD;
    }

    UsdTimeCode visibility_time(const UsdPrim& prim)
    {
        for (auto current = prim; current && !current.IsPseudoRoot(); current = current.GetParent())
        {
            const UsdGeomImageable imageable(current);
            if (imageable && imageable.GetVisibilityAttr().ValueMightBeTimeVarying())
                return UsdTimeCode(Application::instance().get_current_time());
        }
        return UsdTimeCode::Default();
    }

    CommandSyntax property_syntax(const PropertyCommand& descriptor)
    {
        CommandSyntax syntax;
        if (has_multiple_paths(descriptor.property))
            syntax.arg<SdfPathVector>("paths", "Absolute paths of the target prims").arg<bool>("value", "Requested state");
        else
        {
            syntax.arg<SdfPath>("path", "Absolute path of the target prim");
            if (descriptor.property == Property::VARIANT)
                syntax.arg<std::string>("variant_set", "Existing variant set").arg<std::string>("value", "Variant selection");
            else
                syntax.arg<TfToken>("value", "Requested value");
        }
        return syntax.kwarg<UsdStageWeakPtr>("stage", "Target stage; defaults to the current stage").description(descriptor.description);
    }

    class PrimPropertyCommand : public UndoCommand
    {
    public:
        explicit PrimPropertyCommand(Property property)
            : m_property(property)
        {
        }

        CommandResult execute(const CommandArgs& args) override
        {
            if (auto stage = args.get_kwarg<UsdStageWeakPtr>("stage"))
                m_stage = stage->get_value();
            else if (auto session = Application::instance().get_session())
                m_stage = session->get_current_stage();
            if (!m_stage)
                return invalid("stage does not exist");

            SdfPathVector paths;
            bool state = false;
            TfToken value;
            std::string variant_set;
            std::string variant;
            if (has_multiple_paths(m_property))
            {
                auto paths_arg = args.get_arg<SdfPathVector>(0);
                auto state_arg = args.get_arg<bool>(1);
                if (!paths_arg || !state_arg || paths_arg->get_value().empty())
                    return invalid("paths and state are required");
                paths = paths_arg->get_value();
                state = state_arg->get_value();
            }
            else
            {
                auto path = args.get_arg<SdfPath>(0);
                if (!path)
                    return invalid("a prim path is required");
                paths.push_back(path->get_value());
                if (m_property == Property::VARIANT)
                {
                    auto set_arg = args.get_arg<std::string>(1);
                    auto value_arg = args.get_arg<std::string>(2);
                    if (!set_arg || !value_arg)
                        return invalid("variant set and selection are required");
                    variant_set = set_arg->get_value();
                    variant = value_arg->get_value();
                }
                else
                {
                    auto value_arg = args.get_arg<TfToken>(1);
                    if (!value_arg)
                        return invalid("a value is required");
                    value = value_arg->get_value();
                }
            }

            for (const auto& path : paths)
            {
                if (!path.IsAbsolutePath() || !path.IsPrimPath())
                    return invalid("target must be an absolute prim path");
                const auto prim = m_stage->GetPrimAtPath(path);
                if (!prim || prim.IsInstanceProxy() || prim.IsInPrototype())
                    return invalid("target is missing or cannot be edited");
                if (m_property != Property::PAYLOAD &&
                    (!m_stage->GetEditTarget().GetLayer()->PermissionToEdit() || m_stage->GetEditTarget().MapToSpecPath(path).IsEmpty()))
                    return invalid("edit target is not writable for this prim");
                if (m_property == Property::VISIBILITY && !UsdGeomImageable(prim))
                    return invalid("target is not imageable");
                if (m_property == Property::PAYLOAD && !prim.HasAuthoredPayloads())
                    return invalid("target has no authored payload");
                if (m_property == Property::VARIANT)
                {
                    if (!prim.GetVariantSets().HasVariantSet(variant_set))
                        return invalid("variant set does not exist");
                    const auto names = prim.GetVariantSet(variant_set).GetVariantNames();
                    if (!variant.empty() && std::find(names.begin(), names.end(), variant) == names.end())
                        return invalid("variant does not exist");
                }
            }
            if (m_property == Property::DRAW_MODE && value != UsdGeomTokens->inherited && value != UsdGeomTokens->default_ &&
                value != UsdGeomTokens->cards && value != UsdGeomTokens->bounds && value != UsdGeomTokens->origin)
                return invalid("unknown draw mode");

            // Deactivating an ancestor removes its descendants from the composed stage.
            if (m_property == Property::ACTIVE && !state)
                SdfPath::RemoveDescendentPaths(&paths);

            if (m_property != Property::PAYLOAD &&
                (!Application::instance().is_ui_available() || m_stage != Application::instance().get_session()->get_current_stage()))
            {
                // Script mode does not install the session's UI undo watcher.
                auto layer_tree = std::make_shared<LayerTreeWatcher>(m_stage);
                LayerStateDelegatesHolder delegates(layer_tree);
                delegates.add_delegate(TfToken("UndoStateDelegate"));
            }

            TfErrorMark errors;
            bool success = true;
            if (m_property == Property::PAYLOAD)
            {
                m_load_rules = m_stage->GetLoadRules();
                for (const auto& path : paths)
                {
                    if (state)
                        m_stage->Load(path);
                    else
                        m_stage->Unload(path);
                }
                success = errors.IsClean();
                if (!success)
                    m_stage->SetLoadRules(m_load_rules);
            }
            else
            {
                {
                    commands::UsdEditsBlock block;
                    for (const auto& path : paths)
                    {
                        const auto prim = m_stage->GetPrimAtPath(path);
                        if (!prim || !apply(prim, state, value, variant_set, variant))
                        {
                            success = false;
                            break;
                        }
                    }
                    m_inverse = block.take_edits();
                }
                success = success && errors.IsClean();
                if (!success && m_inverse)
                {
                    m_inverse->invert();
                    m_inverse.reset();
                }
            }
            if (!success)
            {
                OPENDCC_WARN("Failed to execute '{}': USD could not apply the requested edit.", get_command_name());
                return CommandResult(CommandResult::Status::FAIL);
            }
            return CommandResult(CommandResult::Status::SUCCESS);
        }

        void undo() override { invert(); }
        void redo() override { invert(); }

    private:
        CommandResult invalid(const char* reason) const
        {
            OPENDCC_WARN("Failed to execute '{}': {}.", get_command_name(), reason);
            return CommandResult(CommandResult::Status::INVALID_ARG);
        }

        bool apply(const UsdPrim& prim, bool state, const TfToken& value, const std::string& variant_set, const std::string& variant)
        {
            switch (m_property)
            {
            case Property::VISIBILITY:
            {
                const UsdGeomImageable imageable(prim);
                const auto time = visibility_time(prim);
                if (state)
                    imageable.MakeVisible(time);
                else
                    imageable.MakeInvisible(time);
                return true;
            }
            case Property::ACTIVE:
                return prim.SetActive(state);
            case Property::TYPE:
                return value.IsEmpty() ? prim.ClearTypeName() : prim.SetTypeName(value);
            case Property::KIND:
                return value.IsEmpty() ? prim.ClearMetadata(SdfFieldKeys->Kind) : prim.SetMetadata(SdfFieldKeys->Kind, value);
            case Property::DRAW_MODE:
            {
                const auto model = prim.HasAPI<UsdGeomModelAPI>() ? UsdGeomModelAPI(prim) : UsdGeomModelAPI::Apply(prim);
                return model && model.CreateModelDrawModeAttr().Set(value);
            }
            case Property::VARIANT:
            {
                auto set = prim.GetVariantSet(variant_set);
                return variant.empty() ? set.ClearVariantSelection() : set.SetVariantSelection(variant);
            }
            default:
                return false;
            }
        }

        void invert()
        {
            if (m_property == Property::PAYLOAD)
            {
                if (m_stage)
                {
                    const auto current = m_stage->GetLoadRules();
                    m_stage->SetLoadRules(m_load_rules);
                    m_load_rules = current;
                }
            }
            else if (m_inverse)
                m_inverse->invert();
        }

        Property m_property;
        UsdStageWeakPtr m_stage;
        UsdStageLoadRules m_load_rules;
        std::unique_ptr<commands::UndoInverse> m_inverse;
    };
}

void commands::register_prim_property_commands()
{
    for (const auto& descriptor : property_commands)
        CommandRegistry::register_command(descriptor.name, property_syntax(descriptor),
                                          [property = descriptor.property]() { return std::make_shared<PrimPropertyCommand>(property); });
}

void commands::unregister_prim_property_commands()
{
    for (const auto& descriptor : property_commands)
        CommandRegistry::unregister_command(descriptor.name);
}

OPENDCC_NAMESPACE_CLOSE
