// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <usd_fallback_proxy/moonray/moonray_property_factory.h>
#include <usd_fallback_proxy/core/source_registry.h>
#include <usd_fallback_proxy/core/property_gatherer.h>
#include <usd/usd_fallback_proxy/utils/utils.h>

#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdRender/var.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<MoonrayPropertyFactory, TfType::Bases<PropertyFactory>>();
    SourceRegistry::register_source(std::make_unique<MoonrayPropertyFactory>());
}

TF_DEFINE_PRIVATE_TOKENS(moonray_attribute_tokens,
                         ((outputsOut, "outputs:out"))((outputsSurface, "outputs:moonray:surface"))(
                             (outputsDisplacement, "outputs:moonray:displacement"))((outputsVolume, "outputs:moonray:volume")));

static const usd_fallback_proxy::utils::PropertyMap& get_moonray_properties()
{
    // https://docs.openmoonray.org/user-reference/scene-objects/render-output/RenderOutput/

    static const usd_fallback_proxy::utils::PropertyMap properties = {
        { TfToken("active"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Bool.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(true) } } } },
        { TfToken("camera"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue() } } } },
        { TfToken("channel_format"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("half")) },
              { SdfFieldKeys->AllowedTokens, VtValue(VtArray<TfToken>({ TfToken("float"), TfToken("half") })) } } } },
        { TfToken("channel_name"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue() } } } },
        { TfToken("channel_suffix_mode "),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("auto")) },
              { SdfFieldKeys->AllowedTokens, VtValue(VtArray<TfToken>({ TfToken("auto"), TfToken("rgb"), TfToken("xyz"), TfToken("uvw") })) } } } },
        { TfToken("checkpoint_file_name"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("checkpoint.exr")) } } } },
        { TfToken("checkpoint_multi_version_file_name"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue() } } } },
        { TfToken("compression"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("zip")) },
              { SdfFieldKeys->AllowedTokens,
                VtValue(VtArray<TfToken>({ TfToken("none"), TfToken("zip"), TfToken("rle"), TfToken("zips"), TfToken("piz"), TfToken("pxr24"),
                                           TfToken("b44"), TfToken("b44a"), TfToken("dwaa"), TfToken("dwab") })) } } } },
        { TfToken("cryptomatte_depth"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Int.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(6) } } } },
        { TfToken("denoise"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Bool.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(false) } } } },
        { TfToken("denoiser_input"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("not an input")) },
              { SdfFieldKeys->AllowedTokens,
                VtValue(VtArray<TfToken>({ TfToken("not an input"), TfToken("as albedo"), TfToken("as normal") })) } } } },
        { TfToken("display_filter"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("exr_dwa_compression_level"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("file_name"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("file_part"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("ipe"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("material_aov"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("math_filter"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("average")) },
              { SdfFieldKeys->AllowedTokens, VtValue(VtArray<TfToken>({ TfToken("average"), TfToken("sum"), TfToken("min"), TfToken("max"),
                                                                        TfToken("force_consistent_sampling"), TfToken("closest") })) } } } },
        { TfToken("output_type"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("flat")) } } } },
        { TfToken("primitive_attribute"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("primitive_attribute_type"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("FLOAT")) },
              { SdfFieldKeys->AllowedTokens,
                VtValue(VtArray<TfToken>({ TfToken("FLOAT"), TfToken("VEC2F"), TfToken("VEC3F"), TfToken("RGB") })) } } } },
        { TfToken("reference_render_output"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("result"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("beauty")) },
              { SdfFieldKeys->AllowedTokens,
                VtValue(VtArray<TfToken>({ TfToken("beauty"), TfToken("alpha"), TfToken("depth"), TfToken("state variable"),
                                           TfToken("primitive attribute"), TfToken("time per pixel"), TfToken("wireframe"), TfToken("material aov"),
                                           TfToken("light aov"), TfToken("visibility aov"), TfToken("variance aov"), TfToken("weight"),
                                           TfToken("beauty aux"), TfToken("cryptomatte"), TfToken("alpha aux"), TfToken("display filter") })) } } } },
        { TfToken("resume_file_name"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } },
        { TfToken("state_variable"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
              { SdfFieldKeys->Default, VtValue(std::string("N")) },
              { SdfFieldKeys->AllowedTokens, VtValue(VtArray<TfToken>({
                                                 TfToken("P"),
                                                 TfToken("Ng"),
                                                 TfToken("N"),
                                                 TfToken("St"),
                                                 TfToken("dPds"),
                                                 TfToken("dPdt"),
                                                 TfToken("dSdx"),
                                                 TfToken("dSdy"),
                                                 TfToken("dTdx"),
                                                 TfToken("dTdy"),
                                                 TfToken("Wp"),
                                                 TfToken("depth"),
                                                 TfToken("motionvec"),
                                             })) } } } },
        { TfToken("visibility_aov"),
          { SdfSpecType::SdfSpecTypeAttribute,
            { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->String.GetAsToken()) }, { SdfFieldKeys->Default, VtValue(std::string("")) } } } }
    };

    return properties;
}

static UsdMetadataValueMap get_outputs_metadata()
{
    static const UsdMetadataValueMap outputs_metadata = { { SdfFieldKeys->TypeName, VtValue(SdfValueTypeNames->Token.GetAsToken()) },
                                                          { SdfFieldKeys->Default, VtValue(TfToken()) } };
    return outputs_metadata;
}

static SdrShaderNodeConstPtr get_shader_node(const UsdPrim& prim)
{
    if (!prim)
        return nullptr;

    auto source_asset = prim.GetAttribute(UsdShadeTokens->infoId);
    TfToken shader_name;
    source_asset.Get(&shader_name);
    if (shader_name.IsEmpty())
        return nullptr;

    return SdrRegistry::GetInstance().GetShaderNodeByName(shader_name);
}

void MoonrayPropertyFactory::get_properties(const UsdPrim& prim, PropertyGatherer& property_gatherer)
{
    const auto source = UsdPropertySource(TfToken(), get_type());

    if (auto var = UsdRenderVar(prim))
    {
        const auto stage = prim.GetStage();
        const std::string render_delegate = usd_fallback_proxy::utils::get_current_render_delegate_name(stage);

        if (render_delegate == "Moonray" && usd_fallback_proxy::utils::is_connect_to_render_settings(var))
        {
            const auto& properties = get_moonray_properties();
            for (const auto& property : properties)
            {
                try_insert_property_pair(property, prim, property_gatherer, source);
            }
        }

        return;
    }

    if (UsdShadeMaterial(prim))
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsSurface, prim, get_outputs_metadata(), source);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsDisplacement, prim, get_outputs_metadata(),
                                              source);
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsVolume, prim, get_outputs_metadata(), source);
        return;
    }

    auto moonray_shader = UsdShadeShader(prim);
    if (!moonray_shader || moonray_shader.GetImplementationSource() != UsdShadeTokens->id)
        return;

    auto sdr_node = get_shader_node(prim);
    if (!sdr_node)
        return;

#if PXR_VERSION >= 2508
    const auto outputs = sdr_node->GetShaderOutputNames();
#else
    const auto outputs = sdr_node->GetOutputNames();
#endif
    if (outputs.empty())
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsOut, prim, get_outputs_metadata(), source);
    }
}

void MoonrayPropertyFactory::get_property(const UsdPrim& prim, const TfToken& property_name, PropertyGatherer& property_gatherer)
{
    const auto source = UsdPropertySource(TfToken(), get_type());

    if (auto var = UsdRenderVar(prim))
    {
        const auto stage = prim.GetStage();
        const std::string render_delegate = usd_fallback_proxy::utils::get_current_render_delegate_name(stage);

        if (render_delegate == "Moonray" && usd_fallback_proxy::utils::is_connect_to_render_settings(var))
        {
            const auto& properties = get_moonray_properties();
            const auto find = properties.find(property_name);
            if (find != properties.end())
            {
                try_insert_property_pair(*find, prim, property_gatherer, source);
            }
        }

        return;
    }

    if (UsdShadeMaterial(prim))
    {
        if (property_name == moonray_attribute_tokens->outputsSurface)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsSurface, prim, get_outputs_metadata(),
                                                  source);
        }
        else if (property_name == moonray_attribute_tokens->outputsDisplacement)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsDisplacement, prim, get_outputs_metadata(),
                                                  source);
        }
        else if (property_name == moonray_attribute_tokens->outputsVolume)
        {
            property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsVolume, prim, get_outputs_metadata(),
                                                  source);
        }
        return;
    }

    auto moonray_shader = UsdShadeShader(prim);
    if (!moonray_shader || moonray_shader.GetImplementationSource() != UsdShadeTokens->id)
        return;

    auto sdr_node = get_shader_node(prim);
    if (!sdr_node)
        return;

#if PXR_VERSION >= 2508
    if (property_name == moonray_attribute_tokens->outputsOut && sdr_node->GetShaderOutputNames().empty())
#else
    if (property_name == moonray_attribute_tokens->outputsOut && sdr_node->GetOutputNames().empty())
#endif
    {
        property_gatherer.try_insert_property(SdfSpecTypeAttribute, moonray_attribute_tokens->outputsOut, prim, get_outputs_metadata(),
                                              UsdPropertySource(TfToken(), get_type()));
    }
}

bool MoonrayPropertyFactory::is_prim_proxy_outdated(const UsdPrim& prim, const TfTokenVector& resynced_property_names,
                                                    const TfTokenVector& changed_property_names) const
{
    return false;
}

TfType MoonrayPropertyFactory::get_type() const
{
    return TfType::Find<MoonrayPropertyFactory>();
}

OPENDCC_NAMESPACE_CLOSE
