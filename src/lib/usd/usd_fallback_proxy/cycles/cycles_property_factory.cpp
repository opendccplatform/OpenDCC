// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "usd/usd_fallback_proxy/cycles/cycles_property_factory.h"
#include <usd_fallback_proxy/core/source_registry.h>
#include <usd_fallback_proxy/core/property_gatherer.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/usd/usdShade/shader.h>
#include "pxr/usd/usdShade/material.h"
#include "ndrCycles/node_definitions.h"
#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(cycles_attribute_tokens, ((outputsOut, "outputs:out"))((outputsSurface, "outputs:cycles:surface"))((
                                                      outputsDisplacement, "outputs:cycles:displacement"))((outputsVolume, "outputs:cycles:volume")));
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<CyclesPropertyFactory, TfType::Bases<PropertyFactory>>();
    SourceRegistry::register_source(std::make_unique<CyclesPropertyFactory>());
}

static UsdMetadataValueMap get_outputs_metadata()
{
    static const UsdMetadataValueMap outputs_metadata = { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
                                                          { SdfFieldKeys->Default, VtValue(TfToken()) } };
    return outputs_metadata;
}

void CyclesPropertyFactory::get_properties(const UsdPrim& prim, PropertyGatherer& property_gatherer)
{
    if (!prim)
        return;
    const auto source = UsdPropertySource(TfToken(), TfType::Find<CyclesPropertyFactory>());
    if (UsdShadeMaterial(prim))
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsSurface, prim, get_outputs_metadata(), source);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsDisplacement, prim, get_outputs_metadata(),
                                              source);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsVolume, prim, get_outputs_metadata(), source);
        return;
    }
    auto shader = UsdShadeShader(prim);
    if (!shader)
        return;

    static const std::string cycles_prefix = "cycles:";
    TfToken shader_id;
    if (!shader.GetShaderId(&shader_id) || !base::utils::starts_with(shader_id.GetString(), cycles_prefix) ||
        shader.GetImplementationSource() != UsdShadeTokens->id)
        return;

    const auto node_definitions = get_node_definitions();
    if (!node_definitions)
        return;

    auto shader_def = node_definitions->GetPrimAtPath(SdfPath::AbsoluteRootPath().AppendChild(TfToken(shader_id.GetText() + cycles_prefix.size())));
    if (!shader_def)
        return;

    for (const auto& prop : shader_def.GetAttributes())
    {
        VtValue val;
        auto meta = prop.GetAllMetadata();
        if (auto attr = prop.As<UsdAttribute>())
            if (attr.Get(&val))
                meta[SdfFieldKeys->Default] = val;
        property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, prop.GetName(), prim, meta, source);
    }

    const auto defined_outputs = shader_def.GetPropertiesInNamespace("outputs");
    if (defined_outputs.empty())
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsOut, prim, get_outputs_metadata(), source);
    }
}

void CyclesPropertyFactory::get_property(const UsdPrim& prim, const TfToken& property_name, PropertyGatherer& property_gatherer)
{
    if (!prim)
        return;
    const auto source = UsdPropertySource(TfToken(), TfType::Find<CyclesPropertyFactory>());
    if (UsdShadeMaterial(prim))
    {
        if (property_name == cycles_attribute_tokens->outputsSurface)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsSurface, prim, get_outputs_metadata(),
                                                  source);
        }
        else if (property_name == cycles_attribute_tokens->outputsDisplacement)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsDisplacement, prim, get_outputs_metadata(),
                                                  source);
        }
        else if (property_name == cycles_attribute_tokens->outputsVolume)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsVolume, prim, get_outputs_metadata(), source);
        }
        return;
    }

    auto shader = UsdShadeShader(prim);
    if (!shader)
        return;
    static const std::string cycles_prefix = "cycles:";
    TfToken shader_id;
    if (!shader.GetShaderId(&shader_id) || !base::utils::starts_with(shader_id.GetString(), cycles_prefix) ||
        shader.GetImplementationSource() != UsdShadeTokens->id)
        return;

    const auto node_definitions = get_node_definitions();
    if (!node_definitions)
        return;

    auto shader_def = node_definitions->GetPrimAtPath(SdfPath::AbsoluteRootPath().AppendChild(TfToken(shader_id.GetText() + cycles_prefix.size())));
    if (!shader_def)
        return;

    if (auto prop = shader_def.GetProperty(property_name))
    {
        VtValue val;
        auto meta = prop.GetAllMetadata();
        if (auto attr = prop.As<UsdAttribute>())
            if (attr.Get(&val))
                meta[SdfFieldKeys->Default] = val;
        property_gatherer.try_insert_property(prop.Is<UsdAttribute>() ? SdfSpecType::SdfSpecTypeAttribute : SdfSpecType::SdfSpecTypeRelationship,
                                              prop.GetName(), prim, meta, source);
    }
    else if (property_name == cycles_attribute_tokens->outputsOut && shader_def.GetPropertiesInNamespace("outputs").empty())
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, cycles_attribute_tokens->outputsOut, prim, get_outputs_metadata(), source);
    }
}

bool CyclesPropertyFactory::is_prim_proxy_outdated(const UsdPrim& prim, const TfTokenVector& resynced_property_names,
                                                   const TfTokenVector& changed_property_names) const
{
    auto shade_shader = UsdShadeShader(prim);
    if (!shade_shader)
        return false;

    static TfTokenVector key_attributes = {
        TfToken("info:id"),
        UsdShadeTokens->infoImplementationSource,
    };

    if (std::find_first_of(resynced_property_names.begin(), resynced_property_names.end(), key_attributes.begin(), key_attributes.end()) !=
        resynced_property_names.end())
    {
        return true;
    }

    if (std::find_first_of(changed_property_names.begin(), changed_property_names.end(), key_attributes.begin(), key_attributes.end()) !=
        changed_property_names.end())
    {
        return true;
    }

    return false;
}

TfType CyclesPropertyFactory::get_type() const
{
    return TfType::Find<CyclesPropertyFactory>();
}

OPENDCC_NAMESPACE_CLOSE
