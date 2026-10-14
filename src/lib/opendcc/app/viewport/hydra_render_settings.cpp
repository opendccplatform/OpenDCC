// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/hydra_render_settings.h"
#include "opendcc/base/logging/logger.h"
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/var.h>
#include <queue>
#include <algorithm>

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.render");

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(RenderSettingsTokens, ((aov_name, "driver:parameters:aov:name"))((multisampled, "driver:parameters:aov:multiSampled"))(
                                                   (clear_value, "driver:parameters:aov:clearValue"))((aov_format, "driver:parameters:aov:format")));

namespace
{
    struct FormatSpec
    {
        template <class T>
        FormatSpec(HdFormat format, const T& val)
            : format(format)
            , clear_value(val)
        {
        }
        HdFormat format;
        VtValue clear_value;
    };
    static const std::unordered_map<TfToken, FormatSpec, TfToken::HashFunctor> s_format_specs(
        { { TfToken("float"), { HdFormatFloat32, 0.0f } },
          { TfToken("color2f"), { HdFormatFloat32Vec2, GfVec2f(0) } },
          { TfToken("color3f"), { HdFormatFloat32Vec3, GfVec3f(0) } },
          { TfToken("color4f"), { HdFormatFloat32Vec4, GfVec4f(0) } },
          { TfToken("float2"), { HdFormatFloat32Vec2, GfVec2f(0) } },
          { TfToken("float3"), { HdFormatFloat32Vec3, GfVec3f(0) } },
          { TfToken("float4"), { HdFormatFloat32Vec4, GfVec4f(0) } },

          { TfToken("half"), { HdFormatFloat16, GfHalf(0) } },
          { TfToken("float16"), { HdFormatFloat16, GfHalf(0) } },
          { TfToken("color2h"), { HdFormatFloat16Vec2, GfVec2h(0) } },
          { TfToken("color3h"), { HdFormatFloat16Vec3, GfVec3h(0) } },
          { TfToken("color4h"), { HdFormatFloat16Vec4, GfVec4h(0) } },
          { TfToken("half2"), { HdFormatFloat16Vec2, GfVec2h(0) } },
          { TfToken("half3"), { HdFormatFloat16Vec3, GfVec3h(0) } },
          { TfToken("half4"), { HdFormatFloat16Vec4, GfVec4h(0) } },

          { TfToken("u8"), { HdFormatUNorm8, uint8_t(0) } },
          { TfToken("uint8"), { HdFormatUNorm8, uint8_t(0) } },
          /* todo: { TfToken("color2u8"),  HdFormatUNorm8Vec2, },
          { TfToken("color3u8"),  HdFormatUNorm8Vec3 },
          { TfToken("color4u8"),  HdFormatUNorm8Vec4 },*/

          { TfToken("i8"), { HdFormatSNorm8, int8_t(0) } },
          { TfToken("int8"), { HdFormatSNorm8, int8_t(0) } },
          /* todo: { TfToken("color2i8"),  HdFormatSNorm8Vec2 },
           { TfToken("color3i8"),  HdFormatSNorm8Vec3 },
           { TfToken("color4i8"),  HdFormatSNorm8Vec4 },*/

          { TfToken("int"), { HdFormatInt32, int(0) } },
          { TfToken("int2"), { HdFormatInt32Vec2, GfVec2i(0) } },
          { TfToken("int3"), { HdFormatInt32Vec3, GfVec3i(0) } },
          { TfToken("int4"), { HdFormatInt32Vec4, GfVec4i(0) } },
          { TfToken("uint"), { HdFormatInt32, int(0) } },
          { TfToken("uint2"), { HdFormatInt32Vec2, GfVec2i(0) } },
          { TfToken("uint3"), { HdFormatInt32Vec3, GfVec3i(0) } },
          { TfToken("uint4"), { HdFormatInt32Vec4, GfVec4i(0) } } });

    std::optional<HydraRenderSettings::RenderVar> make_render_var(const UsdRenderVar& render_var, UsdTimeCode time)
    {
        if (!TF_VERIFY(render_var, "Failed to initialize render var from an invalid prim '%s'", render_var.GetPath().GetText()))
            return {};

        auto attr = render_var.GetPrim().GetAttributes();
        auto aov_name = render_var.GetPrim().GetAttribute(RenderSettingsTokens->aov_name);
        VtValue aov_name_val;
        if (!aov_name)
        {
            aov_name_val = render_var.GetPath().GetNameToken();
        }
        else if (!aov_name.Get(&aov_name_val, time))
        {
            OPENDCC_WARN("Failed to get '{}' from render var '{}'.", RenderSettingsTokens->aov_name.GetText(), render_var.GetPath().GetText());
            return {};
        }

        HydraRenderSettings::RenderVar result;
        if (aov_name_val.IsHolding<TfToken>())
            result.name = aov_name_val.UncheckedGet<TfToken>();
        else if (aov_name_val.IsHolding<std::string>())
            result.name = TfToken(aov_name_val.UncheckedGet<std::string>());
        else
        {
            OPENDCC_WARN("Failed to extract '{}' from render var '{}'. The attribute has an incorrect type, expected 'string' or 'token'.",
                         RenderSettingsTokens->aov_name.GetText(), render_var.GetPath().GetText());
            return {};
        }
        result.descriptor = HdAovDescriptor(HdFormatFloat32Vec4, true, VtValue(GfVec4f(0.0)));

        for (const auto& attribute : render_var.GetPrim().GetAttributes())
        {
            VtValue value;
            if (attribute.Get(&value, time))
            {
                result.descriptor.aovSettings[attribute.GetName()] = value;
            }
        }

        if (result.descriptor.aovSettings[UsdRenderTokens->sourceName] == VtValue(std::string("")))
        {
            result.descriptor.aovSettings[UsdRenderTokens->sourceName] = VtValue(std::string("C.*"));
        }

        if (auto multisampled = render_var.GetPrim().GetAttribute(RenderSettingsTokens->multisampled))
        {
            if (multisampled.GetTypeName() == SdfValueTypeNames->Bool)
            {
                multisampled.Get(&result.descriptor.multiSampled, time);
            }
            else if (multisampled.GetTypeName() == SdfValueTypeNames->Int)
            {
                int ms;
                multisampled.Get(&ms, time);
                result.descriptor.multiSampled = static_cast<bool>(ms);
            }
            else if (multisampled.GetTypeName() == SdfValueTypeNames->Int64)
            {
                int64_t ms;
                multisampled.Get(&ms, time);
                result.descriptor.multiSampled = static_cast<bool>(ms);
            }
        }

        auto extract_format = [](const TfToken& token, HdFormat& format, VtValue& clear_value) {
            auto it = s_format_specs.find(token);
            if (it == s_format_specs.end())
                return false;
            format = it->second.format;
            clear_value = it->second.clear_value;
            return true;
        };

        auto get_data_type = [&result] {
            auto iter = result.descriptor.aovSettings.find(UsdRenderTokens->dataType);
            return iter->second.UncheckedGet<TfToken>();
        };

        if (!extract_format(get_data_type(), result.descriptor.format, result.descriptor.clearValue))
        {
            OPENDCC_WARN("Unknown data format '{}' in render var '{}'.", get_data_type().GetText(), render_var.GetPrim().GetPath().GetText());
            return {};
        }

        if (auto clear_val_attr = render_var.GetPrim().GetAttribute(RenderSettingsTokens->clear_value))
            clear_val_attr.Get(&result.descriptor.clearValue, time);

        return result;
    }

    std::optional<HydraRenderSettings::RenderProduct> make_render_product(const UsdRenderProduct& render_product, UsdTimeCode time)
    {
        if (!TF_VERIFY(render_product, "Failed to initialize render product from an invalid prim '%s'", render_product.GetPath().GetText()))
            return {};

        HydraRenderSettings::RenderProduct result;
        if (auto vars = render_product.GetOrderedVarsRel())
        {
            auto stage = render_product.GetPrim().GetStage();
            SdfPathVector paths;
            vars.GetTargets(&paths);
            for (const auto& path : paths)
            {
                if (auto usd_render_var = UsdRenderVar::Get(stage, path))
                {
                    if (auto render_var = make_render_var(usd_render_var, time))
                    {
                        result.render_vars.emplace_back(std::move(*render_var));
                    }
                    else
                    {
                        return {};
                    }
                }
                else
                {
                    OPENDCC_WARN("Failed to find UsdRenderVar at path '{}'", path.GetText());
                    return {};
                }
            }
        }

        for (const auto& attribute : render_product.GetPrim().GetAttributes())
        {
            VtValue value;
            if (attribute.Get(&value, time))
            {
                result.settings[attribute.GetName()] = value;
            }
        }

        if (auto name_attr = render_product.GetProductNameAttr())
        {
            name_attr.Get(&result.name, time);
        }

        return result;
    }
};

UsdHydraRenderSettings::UsdHydraRenderSettings() {}

std::shared_ptr<UsdHydraRenderSettings> UsdHydraRenderSettings::create(UsdStageRefPtr stage, UsdTimeCode time,
                                                                       SdfPath settings_path /*= PXR_NS::SdfPath()*/)
{
    if (!stage)
        return nullptr;

    UsdRenderSettings settings;
    if (settings_path.IsEmpty())
        settings = UsdRenderSettings::GetStageRenderSettings(stage);
    else
        settings = UsdRenderSettings::Get(stage, settings_path);

    auto result = std::shared_ptr<UsdHydraRenderSettings>(new UsdHydraRenderSettings);
    return result->build_render_settings(settings, time) ? result : nullptr;
}

PXR_NS::GfVec2i UsdHydraRenderSettings::get_resolution() const
{
    return m_resolution;
}

PXR_NS::GfCamera UsdHydraRenderSettings::get_camera() const
{
    SdfPathVector targets;
    if (!m_camera_rel || !m_camera_rel.GetTargets(&targets) || targets.empty())
        return GfCamera();
    auto cam_prim = UsdGeomCamera(m_camera_rel.GetStage()->GetPrimAtPath(targets[0]));
    if (!cam_prim)
        return GfCamera();
    return cam_prim.GetCamera(get_time());
}

PXR_NS::SdfPath UsdHydraRenderSettings::get_camera_path() const
{
    SdfPathVector targets;
    if (!m_camera_rel || !m_camera_rel.GetTargets(&targets) || targets.empty())
        return SdfPath::EmptyPath();
    return targets[0];
}

std::vector<HydraRenderSettings::AOV> UsdHydraRenderSettings::get_aovs() const
{
    return m_aovs;
}

void UsdHydraRenderSettings::clear()
{
    m_aovs.clear();
    m_resolution = GfVec2i(800, 600);
    m_pixel_aspect_ratio = 1.0f;
    m_data_window_ndc = GfVec4f(0, 0, 1, 1);
    m_included_purposes.clear();
}

bool UsdHydraRenderSettings::has_setting(PXR_NS::SdfPath path) const
{
    return std::find(m_settings_paths.begin(), m_settings_paths.end(), path) != m_settings_paths.end();
}

HdAovSettingsMap UsdHydraRenderSettings::get_settings() const
{
    return m_settings;
}

std::vector<HydraRenderSettings::RenderProduct> UsdHydraRenderSettings::get_render_products() const
{
    return m_render_products;
}

bool UsdHydraRenderSettings::build_render_settings(PXR_NS::UsdRenderSettings render_settings, PXR_NS::UsdTimeCode time)
{
    if (!render_settings)
        return false;

    m_time = time;
    auto stage = render_settings.GetPrim().GetStage();
    if (auto products_rel = render_settings.GetProductsRel())
    {
        SdfPathVector paths;
        products_rel.GetTargets(&paths);
        for (const auto& path : paths)
        {
            if (auto usd_render_product = UsdRenderProduct::Get(stage, path))
            {

                if (const auto render_product = make_render_product(usd_render_product, time))
                {
                    m_render_products.push_back(std::move(*render_product));
                }
                else
                {
                    return false;
                }
            }
            else
            {
                OPENDCC_WARN("Failed to find UsdRenderProduct at path '{}'", path.GetText());
                return false;
            }
        }
    }

    if (auto render_delegate_attr = render_settings.GetPrim().GetAttribute(TfToken("render_delegate")))
    {
        render_delegate_attr.Get(&m_render_delegate, m_time);
    }

    for (const auto& product : m_render_products)
    {
        for (const auto& var : product.render_vars)
        {
            m_aovs.emplace_back(AOV { var.name, product.name, var.descriptor });
        }
    }

    render_settings.GetResolutionAttr().Get<GfVec2i>(&m_resolution, m_time);
    render_settings.GetPixelAspectRatioAttr().Get<float>(&m_pixel_aspect_ratio, m_time);
    render_settings.GetDataWindowNDCAttr().Get<GfVec4f>(&m_data_window_ndc, m_time);
    render_settings.GetIncludedPurposesAttr().Get<VtTokenArray>(&m_included_purposes, m_time);
    m_camera_rel = render_settings.GetCameraRel();

    for (const auto& attribute : render_settings.GetPrim().GetAttributes())
    {
        VtValue value;
        if (attribute.Get(&value, m_time))
        {
            m_settings[attribute.GetName()] = value;
        }
    }

    std::queue<UsdPrim> queue;
    queue.push(render_settings.GetPrim());
    auto add_setting_paths = [this, stage, &queue](const SdfPathVector& paths) mutable {
        for (const auto& p : paths)
        {
            if (auto ref_prim = stage->GetPrimAtPath(p))
                queue.push(ref_prim);
        }
    };
    while (!queue.empty())
    {
        auto prim = queue.front();
        queue.pop();
        m_settings_paths.insert(prim.GetPrimPath());
        SdfPathVector sources;
        for (const auto& authored_attr : prim.GetAuthoredAttributes())
        {
            if (authored_attr.GetConnections(&sources))
                add_setting_paths(sources);
        }
        for (const auto& authored_rel : prim.GetAuthoredRelationships())
        {
            if (authored_rel.GetTargets(&sources))
                add_setting_paths(sources);
        }
    }
    return true;
}

TfToken UsdHydraRenderSettings::get_render_delegate() const
{
    return m_render_delegate;
}

OPENDCC_NAMESPACE_CLOSE
