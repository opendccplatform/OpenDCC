// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <usd_fallback_proxy/core/source_registry.h>
#include <usd_fallback_proxy/arnold_usd/arnold_usd_property_factory.h>
#include <usd_fallback_proxy/core/property_gatherer.h>
#include <ndr/parser.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/ndr/node.h>
#include <pxr/usd/ndr/property.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdShade/shader.h>
#include <ai.h>
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usd/tokens.h"
#include <pxr/usd/usdRender/var.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/sdr/registry.h>
#include "metadata_cache.h"
#include "usdUIExt/tokens.h"

#include "usd/usd_fallback_proxy/utils/utils.h"
#include "usd/usd_fallback_proxy/arnold_utils/utils.h"
#include <usd_fallback_proxy/core/utils.h>
#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<ArnoldUsdPropertyFactory, TfType::Bases<PropertyFactory>>();
    SourceRegistry::register_source(std::make_unique<ArnoldUsdPropertyFactory>());
}

TF_DEFINE_PRIVATE_TOKENS(ai_attribute_tokens,
                         (arnold)(shader)((built_in, "<built-in>"))((outputsOut, "outputs:out"))((outputsSurface, "outputs:arnold:surface"))(
                             (outputsDisplacement, "outputs:arnold:displacement"))((outputsVolume, "outputs:arnold:volume")));

static const std::string s_input_prefix = std::string("inputs:");
static const std::string s_output_prefix = std::string("outputs:");

static UsdMetadataValueMap get_outputs_metadata()
{
    static const UsdMetadataValueMap outputs_metadata = { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
                                                          { SdfFieldKeys->Default, VtValue(TfToken()) } };
    return outputs_metadata;
}

static const usd_fallback_proxy::utils::PropertyMap& get_arnold_properties()
{
    // arnold-usd/render_delegate/render_pass.cpp:731
    static const usd_fallback_proxy::utils::PropertyMap property_map = {
        { TfToken("arnold:layer_tolerance"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Float.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(0.01f) } } } },
        { TfToken("arnold:layer_enable_filtering"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Bool.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(true) } } } },
        { TfToken("arnold:layer_half_precision"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Bool.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(false) } } } }
    };

    return property_map;
}

void create_property_proxy(const UsdPrim& prim, const TfToken& name, SdrShaderPropertyConstPtr prop, UsdAttribute metadata_src,
                           PropertyGatherer& property_gatherer)
{
    UsdMetadataValueMap metadata;
    auto try_insert = [&metadata](const TfToken& key, const VtValue& value) {
        if (key == SdfFieldKeys->Custom || key == SdfFieldKeys->Variability)
            return;
        metadata.insert({ key, value });
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
        try_insert(SdfFieldKeys->AllowedTokens, VtValue(options));

    const auto& connectability = prop->IsConnectable();
    try_insert(SdrPropertyMetadata->Connectable, VtValue(connectability));

    if (metadata_src)
    {
        /* VtDictionary hints;
         metadata_src.GetMetadata(UsdUIExtTokens->displayWidgetHints, &hints);
         if (!hints.empty())
             try_insert(UsdUIExtTokens->displayWidgetHints, VtValue(hints));*/

        for (const auto& meta : metadata_src.GetAllAuthoredMetadata())
            try_insert(meta.first, meta.second);
    }

    property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, name, prim, metadata,
                                          UsdPropertySource(TfToken(), TfType::Find<ArnoldUsdPropertyFactory>()));
};

static SdrShaderNodeConstPtr get_shader_node(const UsdPrim& prim)
{
    if (!prim)
        return nullptr;

    auto source_asset = prim.GetAttribute(UsdShadeTokens->infoId);
    TfToken shader_name;
    source_asset.Get(&shader_name);
    if (shader_name.IsEmpty())
        return nullptr;

    return SdrRegistry::GetInstance().GetShaderNodeByIdentifierAndType(shader_name, ai_attribute_tokens->arnold);
}

void ArnoldUsdPropertyFactory::get_properties(const UsdPrim& prim, PropertyGatherer& property_gatherer)
{
    const auto source = UsdPropertySource(TfToken(), TfType::Find<ArnoldUsdPropertyFactory>());

    if (auto product = UsdRenderProduct(prim))
    {
        const auto stage = prim.GetStage();
        const std::string render_delegate = usd_fallback_proxy::utils::get_current_render_delegate_name(stage);

        TfToken name;
        if (render_delegate != "Arnold" || !usd_fallback_proxy::utils::is_connect_to_render_settings(product) ||
            !product.GetProductNameAttr().Get(&name))
        {
            return;
        }

        static const auto filter = TfToken("arnold:filter");
        std::string current_filter;
        if (prim.GetAttribute(filter).Get(&current_filter))
        {
            auto node_entry_layer = get_arnold_entry_map(AI_NODE_FILTER, current_filter);
            if (!node_entry_layer)
            {
                return;
            }

            UsdMetadataValueMap meta;
            meta[SdfFieldKeys->DisplayGroup] = VtValue(TfToken("filter"));

            for (const auto& entry : node_entry_layer->GetPrimAtPath(SdfPath("/temp_prim"))->GetAttributes())
            {
                if (entry->GetNameToken() == "name")
                {
                    continue;
                }

                property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, TfToken("arnold:" + entry->GetNameToken().GetString()), prim,
                                                      meta, source, entry);
            }
        }

        UsdMetadataValueMap meta;
        meta[SdfFieldKeys->TypeName] = VtValue(SdfValueTypeNames->String.GetAsToken());
        meta[SdfFieldKeys->AllowedTokens] = get_nodes_by_type(AI_NODE_FILTER);
        meta[SdfFieldKeys->DisplayGroup] = VtValue(TfToken("filter"));
        if (property_gatherer.contains(filter))
        {
            property_gatherer.update_metadata(filter, meta);
        }
        else
        {
            property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, filter, prim, meta, source);
        }

        auto node_entry_layer = get_arnold_entry_map(AI_NODE_DRIVER, name);
        if (!node_entry_layer)
        {
            return;
        }

        for (const auto& entry : node_entry_layer->GetPrimAtPath(SdfPath("/temp_prim"))->GetAttributes())
        {
            if (entry->GetNameToken() == "name")
            {
                continue;
            }

            property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, TfToken("arnold:" + entry->GetNameToken().GetString()), prim,
                                                  UsdMetadataValueMap(), source, entry);
        }

        return;
    }

    if (auto var = UsdRenderVar(prim))
    {
        const auto stage = prim.GetStage();
        const std::string render_delegate = usd_fallback_proxy::utils::get_current_render_delegate_name(stage);

        if (render_delegate != "Arnold" || !usd_fallback_proxy::utils::is_connect_to_render_settings(var))
        {
            return;
        }

        const auto& properties = get_arnold_properties();
        for (const auto& property : properties)
        {
            try_insert_property_pair(property, prim, property_gatherer, source);
        }

        return;
    }

    if (UsdShadeMaterial(prim))
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsSurface, prim, get_outputs_metadata(), source);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsDisplacement, prim, get_outputs_metadata(), source);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsVolume, prim, get_outputs_metadata(), source);
        return;
    }

    auto ai_shader = UsdShadeShader(prim);
    if (!ai_shader || ai_shader.GetImplementationSource() != UsdShadeTokens->id)
        return;

    auto sdr_node = get_shader_node(prim);
    if (!sdr_node)
        return;

    auto stage_def = get_arnold_metadata();
    auto metadata_src = stage_def->GetPrimAtPath(SdfPath(TfStringPrintf("/%s", sdr_node->GetName().c_str())));
    if (!metadata_src.IsValid())
    {
        return;
    }

    for (const auto& input_name : sdr_node->GetInputNames())
    {
        auto input = sdr_node->GetShaderInput(input_name);
        auto property_name = TfToken(s_input_prefix + input->GetName().GetString());
        auto usd_metadata = metadata_src.GetAttribute(input_name);
        create_property_proxy(prim, property_name, input, usd_metadata, property_gatherer);
    }
    for (const auto& output_name : sdr_node->GetOutputNames())
    {
        auto output = sdr_node->GetShaderOutput(output_name);
        auto property_name = TfToken(s_output_prefix + output->GetName().GetString());
        auto usd_metadata = metadata_src.GetAttribute(output_name);
        create_property_proxy(prim, property_name, output, usd_metadata, property_gatherer);
    }

    auto shader = UsdShadeShader(metadata_src);
    auto shader_outputs = shader.GetOutputs();

    std::sort(shader_outputs.begin(), shader_outputs.end(), [](const UsdShadeOutput& left, const UsdShadeOutput& right) -> bool {
        auto left_base_name = left.GetBaseName().GetString();
        auto right_base_name = right.GetBaseName().GetString();
        if (left_base_name == "x" || left_base_name == "y" || left_base_name == "z")
        {
            return left_base_name < right_base_name;
        }
        return left_base_name > right_base_name;
    });

    for (const auto& output : shader_outputs)
    {
        auto property_name = TfToken(s_output_prefix + output.GetBaseName().GetString());
        auto output_attr = metadata_src.GetAttribute(property_name);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, property_name, prim, output_attr.GetAllMetadata(), source);
    }

    if (auto output = metadata_src.GetAttribute(ai_attribute_tokens->outputsOut))
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsOut, prim, output.GetAllAuthoredMetadata(), source);
    }
}

void ArnoldUsdPropertyFactory::get_property(const UsdPrim& prim, const TfToken& property_name, PropertyGatherer& property_gatherer)
{
    const auto source = UsdPropertySource(TfToken(), TfType::Find<ArnoldUsdPropertyFactory>());

    if (auto product = UsdRenderProduct(prim))
    {
        const auto stage = prim.GetStage();
        const std::string render_delegate = usd_fallback_proxy::utils::get_current_render_delegate_name(stage);

        TfToken name;
        if (render_delegate != "Arnold" || !usd_fallback_proxy::utils::is_connect_to_render_settings(product) ||
            !product.GetProductNameAttr().Get(&name))
        {
            return;
        }

        static const auto filter = TfToken("arnold:filter");
        std::string current_filter;
        if (prim.GetAttribute(filter).Get(&current_filter))
        {
            if (auto node_entry_layer = get_arnold_entry_map(AI_NODE_FILTER, current_filter))
            {
                auto attribute_spec = node_entry_layer->GetAttributeAtPath(SdfPath("/temp_prim").AppendProperty(property_name));
                if (attribute_spec)
                {
                    UsdMetadataValueMap meta;
                    meta[SdfFieldKeys->DisplayGroup] = VtValue(TfToken("filter"));

                    property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute,
                                                          base::utils::starts_with(property_name.GetString(), "arnold:")
                                                              ? property_name
                                                              : TfToken("arnold:" + property_name.GetString()),
                                                          prim, meta, source);

                    return;
                }
            }
        }

        UsdMetadataValueMap meta;
        meta[SdfFieldKeys->TypeName] = VtValue(SdfValueTypeNames->String.GetAsToken());
        meta[SdfFieldKeys->AllowedTokens] = get_nodes_by_type(AI_NODE_FILTER);
        meta[SdfFieldKeys->DisplayGroup] = VtValue(TfToken("filter"));
        if (property_gatherer.contains(filter))
        {
            property_gatherer.update_metadata(filter, meta);
        }
        else
        {
            property_gatherer.try_insert_property(SdfSpecType::SdfSpecTypeAttribute, filter, prim, meta, source);
        }

        auto node_entry_layer = get_arnold_entry_map(AI_NODE_DRIVER, name);
        if (!node_entry_layer)
        {
            return;
        }

        auto attribute_spec = node_entry_layer->GetAttributeAtPath(SdfPath("/temp_prim").AppendProperty(property_name));
        if (attribute_spec)
        {
            property_gatherer.try_insert_property(
                SdfSpecType::SdfSpecTypeAttribute,
                base::utils::starts_with(property_name.GetString(), "arnold:") ? property_name : TfToken("arnold:" + property_name.GetString()), prim,
                UsdMetadataValueMap(), source);
        }

        return;
    }

    if (auto var = UsdRenderVar(prim))
    {
        const auto stage = prim.GetStage();
        const std::string render_delegate = usd_fallback_proxy::utils::get_current_render_delegate_name(stage);

        if (render_delegate == "Arnold" && usd_fallback_proxy::utils::is_connect_to_render_settings(var))
        {
            const auto& properties = get_arnold_properties();
            const auto find = properties.find(property_name);
            if (find != properties.end())
            {
                usd_fallback_proxy::utils::try_insert_property_pair(*find, prim, property_gatherer, source);
            }
        }

        return;
    }

    if (UsdShadeMaterial(prim))
    {
        if (property_name == ai_attribute_tokens->outputsSurface)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsSurface, prim, get_outputs_metadata(), source);
        }
        else if (property_name == ai_attribute_tokens->outputsDisplacement)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsDisplacement, prim, get_outputs_metadata(),
                                                  source);
        }
        else if (property_name == ai_attribute_tokens->outputsVolume)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsVolume, prim, get_outputs_metadata(), source);
        }
        return;
    }

    auto ai_shader = UsdShadeShader(prim);
    if (!ai_shader || ai_shader.GetImplementationSource() != UsdShadeTokens->id)
        return;

    auto sdr_node = get_shader_node(prim);
    if (!sdr_node)
        return;

    auto stage_def = get_arnold_metadata();
    auto metadata_src = stage_def->GetPrimAtPath(SdfPath(TfStringPrintf("/%s", sdr_node->GetName().c_str())));
    if (base::utils::starts_with(property_name.GetString(), s_input_prefix))
    {
        auto input_name = TfToken(property_name.GetText() + s_input_prefix.size());
        auto usd_metadata = metadata_src.GetAttribute(input_name);
        if (auto prop = sdr_node->GetShaderInput(input_name))
            create_property_proxy(prim, property_name, prop, usd_metadata, property_gatherer);
    }
    else if (base::utils::starts_with(property_name.GetString(), s_output_prefix))
    {
        auto output_name = TfToken(property_name.GetText() + s_output_prefix.size());
        auto usd_metadata = metadata_src.GetAttribute(output_name);
        if (auto prop = sdr_node->GetShaderOutput(output_name))
        {
            create_property_proxy(prim, property_name, prop, usd_metadata, property_gatherer);
        }
        else if (property_name == ai_attribute_tokens->outputsOut)
        {
            if (auto output = metadata_src.GetAttribute(ai_attribute_tokens->outputsOut))
            {
                property_gatherer.try_insert_property(SdfSpecTypeAttribute, ai_attribute_tokens->outputsOut, prim, output.GetAllAuthoredMetadata(),
                                                      source);
            }
        }
    }

    auto shader = UsdShadeShader(metadata_src);
    if (base::utils::starts_with(property_name.GetString(), s_output_prefix))
    {
        auto output_name = TfToken(property_name.GetText() + s_output_prefix.size());
        auto output_attr = metadata_src.GetAttribute(property_name);
        if (auto prop = shader.GetOutput(output_name))
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, property_name, prim, output_attr.GetAllMetadata(), source);
        }
    }
}

bool ArnoldUsdPropertyFactory::is_prim_proxy_outdated(const UsdPrim& prim, const TfTokenVector& resynced_property_names,
                                                      const TfTokenVector& changed_property_names) const
{
    if (std::find(changed_property_names.begin(), changed_property_names.end(), UsdRenderTokens->productName) != changed_property_names.end() ||
        std::find(changed_property_names.begin(), changed_property_names.end(), TfToken("arnold:filter")) != changed_property_names.end())
    {
        return true;
    }

    auto ai_shader = UsdShadeShader(prim);
    if (!ai_shader)
        return false;

    static TfTokenVector key_attributes = { UsdShadeTokens->infoImplementationSource, UsdShadeTokens->infoId };

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

TfType ArnoldUsdPropertyFactory::get_type() const
{
    return TfType::Find<ArnoldUsdPropertyFactory>();
}

OPENDCC_NAMESPACE_CLOSE
