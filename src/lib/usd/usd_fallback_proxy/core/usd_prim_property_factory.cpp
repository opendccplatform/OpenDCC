// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <usd_fallback_proxy/core/usd_prim_property_factory.h>
#include <usd_fallback_proxy/core/source_registry.h>
#include <usd_fallback_proxy/core/usd_property_proxy.h>
#include <usd_fallback_proxy/core/property_gatherer.h>
#include <usd_fallback_proxy/core/utils.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include "usdUIExt/tokens.h"
#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdPrimPropertyFactory, TfType::Bases<PropertyFactory>>();
}

namespace
{
    const std::string s_input_prefix = "inputs:";
    const std::string s_output_prefix = "outputs:";
    void create_property_proxy(const UsdPrim& prim, const TfToken& name, SdrShaderPropertyConstPtr prop, PropertyGatherer& property_gatherer)
    {
        UsdMetadataValueMap metadata;
        auto try_insert = [&metadata](const TfToken& key, const VtValue& value) {
            metadata[key] = value;
        };

        const auto& default_value = prop->GetDefaultValue();
        if (!default_value.IsEmpty())
            try_insert(SdfFieldKeys->Default, default_value);

        const TfToken type_name = resolve_typename(prop);
        if (!type_name.IsEmpty())
            try_insert(SdfFieldKeys->TypeName, VtValue(type_name));

        const auto& display_name = prop->GetLabel();
        if (!display_name.IsEmpty())
            try_insert(SdfFieldKeys->DisplayName, VtValue(display_name.GetString()));

        const auto& display_group = prop->GetPage();
        if (!display_group.IsEmpty())
            try_insert(SdfFieldKeys->DisplayGroup, VtValue(display_group.GetString()));

        const auto& documentation = prop->GetHelp();
        if (!documentation.empty())
            try_insert(SdfFieldKeys->Documentation, VtValue(documentation));

        const auto& options = prop->GetOptions();
        if (!options.empty())
        {
            if (VtValue(options).IsArrayValued())
            {
                if (type_name == "token")
                {
                    try_insert(SdfFieldKeys->AllowedTokens, VtValue(options));
                }
                else
                {
                    VtDictionary sdr_options;
                    sdr_options[SdrPropertyMetadata->Options] = VtValue(options);
                    try_insert(TfToken("sdrMetadata"), VtValue(sdr_options));
                }
            }
            VtArray<TfToken> options_array;
            for (auto option : options)
                options_array.push_back(option.first);
            if (type_name == "token")
            {
                try_insert(SdfFieldKeys->AllowedTokens, VtValue(options));
            }
            else
            {
                std::string options_str;
                for (auto option : options_array)
                {
                    options_str += option;
                    options_str += "|";
                }
                options_str.pop_back();
                VtDictionary sdr_options;
                sdr_options[SdrPropertyMetadata->Options] = VtValue(options_str);
                try_insert(TfToken("sdrMetadata"), VtValue(sdr_options));
            }
        }
        if (!prop->GetWidget().IsEmpty())
            try_insert(UsdUIExtTokens->displayWidget, VtValue(TfToken(prop->GetWidget())));

        const auto hints = prop->GetHints();
        if (!hints.empty())
        {
            VtDictionary usd_hints;
            for (const auto& hint : hints)
                usd_hints[hint.first] = VtValue(hint.second);
            try_insert(UsdUIExtTokens->displayWidgetHints, VtValue(usd_hints));
        }
        property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, name, prim, metadata,
                                              UsdPropertySource(TfToken(), TfType::Find<UsdPrimPropertyFactory>()));
    };
};

void UsdPrimPropertyFactory::get_properties(const UsdPrim& prim, PropertyGatherer& property_gatherer)
{
    if (!prim)
        return;

#if PXR_VERSION >= 2005
    for (const auto schema_def : get_schemas_definitions(prim))
    {
        if (!schema_def)
            continue;
        for (const auto property_name : schema_def->GetPropertyNames())
        {
            if (auto prop_spec = schema_def->GetSchemaPropertySpec(property_name))
            {
                const auto source = UsdPropertySource(prop_spec->GetPath().GetPrimPath().GetNameToken(), get_type());
                property_gatherer.try_insert_property(prop_spec->GetSpecType(), property_name, prim, UsdMetadataValueMap(), source, prop_spec);
            }
        }
    }
#else
    for (const auto& schema_name : get_prim_schemas(prim))
    {
        auto schema_spec = UsdSchemaRegistry::GetPrimDefinition(schema_name);
        if (!schema_spec)
            continue;
        for (auto prop_spec : schema_spec->GetProperties())
        {
            const auto source = UsdPropertySource(schema_name, get_type());
            property_gatherer.try_insert_property(prop_spec->GetSpecType(), prop_spec->GetNameToken(), prim, UsdMetadataValueMap(), source,
                                                  prop_spec);
        }
    }
#endif

    auto shader = UsdShadeShader(prim);
    if (!shader)
        return;
    if (shader.GetImplementationSource() != UsdShadeTokens->id)
        return;
    TfToken shader_id;
    if (!shader.GetShaderId(&shader_id))
        return;

    auto shader_node = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(shader_id);
    if (!shader_node)
        return;

#if PXR_VERSION >= 2508
    for (const auto& input_name : shader_node->GetShaderInputNames())
#else
    for (const auto& input_name : shader_node->GetInputNames())
#endif
    {
        auto input = shader_node->GetShaderInput(input_name);
        auto property_name = TfToken(s_input_prefix + input->GetName().GetString());
        create_property_proxy(prim, property_name, input, property_gatherer);
    }

#if PXR_VERSION >= 2508
    for (const auto& output_name : shader_node->GetShaderOutputNames())
#else
    for (const auto& output_name : shader_node->GetOutputNames())
#endif
    {
        auto output = shader_node->GetShaderOutput(output_name);
        auto property_name = TfToken(s_output_prefix + output->GetName().GetString());
        create_property_proxy(prim, property_name, output, property_gatherer);
    }
}

void UsdPrimPropertyFactory::get_property(const UsdPrim& prim, const TfToken& property_name, PropertyGatherer& property_gatherer)
{
    if (!prim)
        return;

#if PXR_VERSION >= 2005
    for (const auto schema_def : get_schemas_definitions(prim))
    {
        if (!schema_def)
            continue;

        if (auto prop_spec = schema_def->GetSchemaPropertySpec(property_name))
        {
            property_gatherer.try_insert_property(prop_spec->GetSpecType(), property_name, prim, UsdMetadataValueMap(),
                                                  UsdPropertySource(prop_spec->GetPath().GetPrimPath().GetNameToken(), get_type()), prop_spec);
        }
    }
#else
    for (const auto& schema_name : get_prim_schemas(prim))
    {
        auto type = UsdSchemaRegistry::GetTypeFromName(schema_name);
        if (auto prim_def = UsdSchemaRegistry::GetPrimDefinition(type))
        {
            const auto path = SdfPath::AbsoluteRootPath().AppendChild(schema_name).AppendProperty(property_name);
            if (auto property_spec = prim_def->GetPropertyAtPath(path))
            {
                property_gatherer.try_insert_property(property_spec->GetSpecType(), property_spec->GetNameToken(), prim, UsdMetadataValueMap(),
                                                      UsdPropertySource(schema_name, get_type()), property_spec);
            }
        }
    }
#endif

    auto shader = UsdShadeShader(prim);
    if (!shader)
        return;
    if (shader.GetImplementationSource() != UsdShadeTokens->id)
        return;
    TfToken shader_id;
    if (!shader.GetShaderId(&shader_id))
        return;

    auto shader_node = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(shader_id);
    if (!shader_node)
        return;

    if (starts_with(property_name.GetString(), s_input_prefix))
    {
        const auto name = TfToken(property_name.GetText() + s_input_prefix.size());
        if (auto prop = shader_node->GetShaderInput(name))
            create_property_proxy(prim, property_name, prop, property_gatherer);
    }
    else if (starts_with(property_name.GetString(), s_output_prefix))
    {
        const auto name = TfToken(property_name.GetText() + s_output_prefix.size());
        if (auto prop = shader_node->GetShaderOutput(name))
            create_property_proxy(prim, property_name, prop, property_gatherer);
    }
}

bool UsdPrimPropertyFactory::is_prim_proxy_outdated(const UsdPrim& prim, const TfTokenVector& resynced_property_names,
                                                    const TfTokenVector& changed_property_names) const
{
    auto shader = UsdShadeShader(prim);
    if (!shader)
        return false;

    auto contains_name = [&resynced_property_names, &changed_property_names](const TfToken& name) {
        if (std::find(changed_property_names.begin(), changed_property_names.end(), name) != changed_property_names.end())
            return true;
        if (std::find(resynced_property_names.begin(), resynced_property_names.end(), name) != resynced_property_names.end())
            return true;
        return false;
    };

    if (contains_name(UsdShadeTokens->infoImplementationSource))
        return true;

    if (shader.GetImplementationSource() == UsdShadeTokens->id)
    {
        if (contains_name(UsdShadeTokens->infoId))
            return true;
    }
    else if (shader.GetImplementationSource() == UsdShadeTokens->sourceAsset)
    {
        if (contains_name(TfToken("info:sourceAsset")))
            return true;
    }
    else if (shader.GetImplementationSource() == UsdShadeTokens->sourceCode)
    {
        if (contains_name(TfToken("info:sourceCode")))
            return true;
    }

    return false;
}

TfType UsdPrimPropertyFactory::get_type() const
{
    return TfType::Find<UsdPrimPropertyFactory>();
}

#if PXR_VERSION >= 2005
std::vector<const UsdPrimDefinition*> UsdPrimPropertyFactory::get_schemas_definitions(const PXR_NS::UsdPrim& prim) const
{
    const auto applied_schemas = prim.GetPrimDefinition().GetAppliedAPISchemas();
    std::vector<const UsdPrimDefinition*> result(applied_schemas.size() + 1);
    result[0] = &prim.GetPrimDefinition();
    std::transform(applied_schemas.begin(), applied_schemas.end(), result.begin() + 1,
                   [](const TfToken& schema_name) { return UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(schema_name); });
    return result;
}
#else
TfTokenVector UsdPrimPropertyFactory::get_prim_schemas(const UsdPrim& prim) const
{
    auto applied_schemas = prim.GetAppliedSchemas();
    TfTokenVector schema_names(applied_schemas.size() + 1);
    schema_names[0] = prim.GetTypeName();
    std::move(applied_schemas.begin(), applied_schemas.end(), schema_names.begin() + 1);
    return schema_names;
}
#endif
OPENDCC_NAMESPACE_CLOSE
