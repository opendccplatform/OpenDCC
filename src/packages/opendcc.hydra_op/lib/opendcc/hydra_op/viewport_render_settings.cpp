// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/viewport_render_settings.h"
#include <pxr/imaging/hd/sceneGlobalsSchema.h>
#include <pxr/imaging/hd/renderSettingsSchema.h>
#include <pxr/imaging/hd/renderProductSchema.h>
#include <pxr/imaging/hd/renderVarSchema.h>
#include <pxr/imaging/hd/cameraSchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/hd/tokens.h>
#include "opendcc/app/viewport/hydra_ext_render_session_api_schema.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

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

    template <class THandle, class TReturn>
    bool get_val(const THandle& handle, TReturn& result)
    {
        if (handle)
        {
            result = handle->GetTypedValue(0);
            return true;
        }
        return false;
    }
    template <class THandle>
    bool get_setting_val(const THandle& handle, const TfToken& name, HdAovSettingsMap& settings)
    {
        if (handle)
        {
            settings[name] = handle->GetValue(0);
            return true;
        }
        return false;
    }

    GfCamera make_camera(const HdSceneIndexBaseRefPtr& si, const SdfPath& cam_path)
    {
        if (cam_path.IsEmpty())
        {
            return {};
        }

        const auto cam_prim = si->GetPrim(cam_path);
        if (cam_prim.primType != HdPrimTypeTokens->camera || !cam_prim.dataSource)
        {
            return {};
        }

        auto cam_schema = HdCameraSchema::GetFromParent(cam_prim.dataSource);
        auto xform_schema = HdXformSchema::GetFromParent(cam_prim.dataSource);
        if (!cam_schema || !xform_schema)
        {
            return {};
        }

        //  Only get the values needed to compute proj/view matrix.
        GfMatrix4d xform;
        TfToken proj;
        float hor_app = 0;
        float ver_app = 0;
        float hor_app_ofs = 0;
        float ver_app_ofs = 0;
        float foc_len = 0;
        GfVec2f clip_range;
        VtVec4dArray clip_planes_vtarr;
        float fstop = 0;
        float foc_dist = 0;

        if (get_val(xform_schema.GetMatrix(), xform) && get_val(cam_schema.GetProjection(), proj) &&
            get_val(cam_schema.GetHorizontalAperture(), hor_app) && get_val(cam_schema.GetVerticalAperture(), ver_app) &&
            get_val(cam_schema.GetHorizontalApertureOffset(), hor_app_ofs) && get_val(cam_schema.GetVerticalApertureOffset(), ver_app_ofs) &&
            get_val(cam_schema.GetFocalLength(), foc_len) && get_val(cam_schema.GetClippingRange(), clip_range) &&
            get_val(cam_schema.GetClippingPlanes(), clip_planes_vtarr) && get_val(cam_schema.GetFStop(), fstop) &&
            get_val(cam_schema.GetFocusDistance(), foc_dist))
        {
            std::vector<GfVec4f> clip_planes;
            std::transform(clip_planes_vtarr.begin(), clip_planes_vtarr.end(), std::back_inserter(clip_planes),
                           [](const auto& v) { return GfVec4f(v); });
            GfCamera::Projection proj_enum;
            if (proj == HdCameraSchemaTokens->perspective)
            {
                proj_enum = GfCamera::Projection::Perspective;
            }
            else if (proj == HdCameraSchemaTokens->orthographic)
            {
                proj_enum = GfCamera::Projection::Orthographic;
            }
            else
            {
                return {};
            }

            return GfCamera { xform,       proj_enum, hor_app, ver_app, hor_app_ofs, ver_app_ofs, foc_len, GfRange1f(clip_range[0], clip_range[1]),
                              clip_planes, fstop,     foc_dist };
        }
        return {};
    }
};

std::shared_ptr<HydraOpViewportRenderSettings> HydraOpViewportRenderSettings::create(HdSceneIndexBaseRefPtr si)
{
    if (!si)
    {
        return nullptr;
    }

    auto globals_prim = si->GetPrim(HdSceneGlobalsSchema::GetDefaultPrimPath());
    auto globals_schema = HdSceneGlobalsSchema::GetFromParent(globals_prim.dataSource);
    if (!globals_schema)
    {
        return nullptr;
    }
    auto rs_path_data = globals_schema.GetActiveRenderSettingsPrim();
    if (!rs_path_data)
    {
        return nullptr;
    }

    auto rs_path = rs_path_data->GetTypedValue(0);
    auto rs_prim = si->GetPrim(rs_path);
    if (rs_prim.primType != HdPrimTypeTokens->renderSettings)
    {
        return nullptr;
    }

    auto rs_schema = HdRenderSettingsSchema::GetFromParent(rs_prim.dataSource);
    if (!rs_schema)
    {
        return nullptr;
    }

    TfToken render_delegate;
    auto render_session_api_schema = HydraExtRenderSessionAPISchema::get_from_parent(rs_prim.dataSource);
    if (render_session_api_schema)
    {
        get_val(render_session_api_schema.get_render_delegate(), render_delegate);
    }

    const auto rp_schema = rs_schema.GetRenderProducts();
    if (!rp_schema)
    {
        return nullptr;
    }

    const auto rp_size = rp_schema.GetNumElements();
    if (rp_size == 0)
    {
        return nullptr;
    }

    const auto rp = rp_schema.GetElement(0);
    if (!rp)
    {
        return nullptr;
    }

    SdfPath camera_path;
    get_val(rp.GetCameraPrim(), camera_path);

    GfCamera camera = make_camera(si, camera_path);

    // For now make render settings convenient with USD-context render_settings, in Hydra v2.0 they have
    // a little different setup
    HdAovSettingsMap settings;
    get_setting_val(rs_schema.GetIncludedPurposes(), UsdRenderTokens->includedPurposes, settings);
    get_setting_val(rs_schema.GetMaterialBindingPurposes(), UsdRenderTokens->materialBindingPurposes, settings);
    get_setting_val(rs_schema.GetRenderingColorSpace(), UsdRenderTokens->renderingColorSpace, settings);
    get_setting_val(rp.GetResolution(), UsdRenderTokens->resolution, settings);
    get_setting_val(rp.GetAspectRatioConformPolicy(), UsdRenderTokens->aspectRatioConformPolicy, settings);
    get_setting_val(rp.GetDataWindowNDC(), UsdRenderTokens->dataWindowNDC, settings);
    get_setting_val(rp.GetDisableDepthOfField(), UsdRenderTokens->disableDepthOfField, settings);
    get_setting_val(rp.GetDisableMotionBlur(), UsdRenderTokens->disableMotionBlur, settings);
    get_setting_val(rp.GetPixelAspectRatio(), UsdRenderTokens->pixelAspectRatio, settings);

    std::vector<AOV> aovs;
    std::vector<RenderProduct> render_products;
    for (int i = 0; i < rp_size; ++i)
    {
        const auto rp = rp_schema.GetElement(i);
        const auto rv_schema = rp.GetRenderVars();
        if (!rv_schema)
        {
            continue;
        }
        const auto rv_size = rv_schema.GetNumElements();

        RenderProduct product;
        get_val(rp.GetName(), product.name);

        SdfPath rp_path;
        get_val(rp.GetPath(), rp_path);
        if (render_session_api_schema)
        {
            auto raw_attrs = HdContainerDataSource::Get(render_session_api_schema.GetContainer(),
                                                        HdDataSourceLocator(TfToken("render_products"), rp_path.GetToken(), TfToken("settings")));

            if (auto raw_props = HdContainerDataSource::Cast(raw_attrs))
            {
                for (const auto& n : raw_props->GetNames())
                {
                    if (auto prop = raw_props->Get(n))
                    {
                        if (auto value_ds = HdSampledDataSource::Cast(prop))
                        {
                            product.settings[n] = value_ds->GetValue(0);
                        }
                    }
                }
            }
        }

        for (int j = 0; j < rv_size; ++j)
        {
            const auto rv = rv_schema.GetElement(j);
            if (!rv)
            {
                continue;
            }
            AOV aov;
            aov.product_name = product.name;
            SdfPath aov_path;
            get_val(rv.GetPath(), aov_path);
            aov.name = aov_path.GetNameToken();

            auto aov_descr = HdAovDescriptor(HdFormatFloat32Vec4, true, VtValue(GfVec4f(0.0)));

            if (render_session_api_schema)
            {
                SdfPath rv_path;
                get_val(rv.GetPath(), rv_path);
                auto raw_attrs = HdContainerDataSource::Get(
                    render_session_api_schema.GetContainer(),
                    HdDataSourceLocator(TfToken("render_products"), rp_path.GetToken(), TfToken("render_vars"), rv_path.GetToken()));
                if (auto raw_props = HdContainerDataSource::Cast(raw_attrs))
                {
                    for (const auto& n : raw_props->GetNames())
                    {
                        if (auto prop = raw_props->Get(n))
                        {
                            if (auto value_ds = HdSampledDataSource::Cast(prop))
                            {
                                aov_descr.aovSettings[n] = value_ds->GetValue(0);
                            }
                        }
                    }

                    // hardcode some values to AOV descriptor
                    //
                    // Comment out this part because some render delegates require sourceName to be a string, not token
                    // SourceName
                    // get_setting_val(rv.GetSourceName(), UsdRenderTokens->sourceName, aov_descr.aovSettings);
                    // if (aov_descr.aovSettings.find(UsdRenderTokens->sourceName) == aov_descr.aovSettings.end())
                    //{
                    //    aov_descr.aovSettings[UsdRenderTokens->sourceName] = VtValue(std::string("C.*"));
                    //}

                    // Multisampled
                    auto it = aov_descr.aovSettings.find(TfToken("driver:parameters:aov:multiSampled"));
                    if (it != aov_descr.aovSettings.end())
                    {
                        if (it->second.CanCast<int>() && it->second.Cast<int>().Get<int>() == 1)
                        {
                            aov_descr.multiSampled = true;
                        }
                    }

                    // format
                    auto extract_format = [](const TfToken& token, HdFormat& format, VtValue& clear_value) {
                        auto it = s_format_specs.find(token);
                        if (it != s_format_specs.end())
                        {
                            format = it->second.format;
                            clear_value = it->second.clear_value;
                        }
                    };

                    TfToken data_type;
                    get_val(rv.GetDataType(), data_type);
                    extract_format(data_type, aov_descr.format, aov_descr.clearValue);

                    // clear value
                    it = aov_descr.aovSettings.find(TfToken("driver:parameters:aov:clearValue"));
                    if (it != aov_descr.aovSettings.end())
                    {
                        aov_descr.clearValue = it->second;
                    }
                }
            }

            aov.descriptor = aov_descr;

            RenderVar render_var;
            render_var.name = aov.name;
            render_var.descriptor = aov_descr;

            product.render_vars.emplace_back(std::move(render_var));
            aovs.emplace_back(std::move(aov));
        }

        render_products.emplace_back(std::move(product));
    }

    return std::shared_ptr<HydraOpViewportRenderSettings>(
        new HydraOpViewportRenderSettings(rs_path, settings, aovs, render_products, camera_path, camera, render_delegate));
}

PXR_NS::GfVec2i HydraOpViewportRenderSettings::get_resolution() const
{
    auto it = m_settings.find(UsdRenderTokens->resolution);
    return it != m_settings.end() ? it->second.Get<GfVec2i>() : GfVec2i { 0, 0 };
}

PXR_NS::SdfPath HydraOpViewportRenderSettings::get_camera_path() const
{
    return m_camera_path;
}

PXR_NS::GfCamera HydraOpViewportRenderSettings::get_camera() const
{
    return m_camera;
}

std::vector<HydraRenderSettings::AOV> HydraOpViewportRenderSettings::get_aovs() const
{
    return m_aovs;
}

PXR_NS::HdAovSettingsMap HydraOpViewportRenderSettings::get_settings() const
{
    return m_settings;
}

PXR_NS::TfToken HydraOpViewportRenderSettings::get_render_delegate() const
{
    return m_render_delegate;
}

HydraOpViewportRenderSettings::HydraOpViewportRenderSettings(const PXR_NS::SdfPath& settings_path, const HdAovSettingsMap& settings,
                                                             const std::vector<AOV>& aovs, const std::vector<RenderProduct>& render_products,
                                                             const PXR_NS::SdfPath& camera_path, const GfCamera& cam, const TfToken& render_delegate)
    : m_settings_path(settings_path)
    , m_settings(settings)
    , m_render_products(render_products)
    , m_aovs(aovs)
    , m_camera_path(camera_path)
    , m_camera(cam)
    , m_render_delegate(render_delegate)
{
}

std::vector<HydraOpViewportRenderSettings::RenderProduct> HydraOpViewportRenderSettings::get_render_products() const
{
    return m_render_products;
}

const PXR_NS::SdfPath HydraOpViewportRenderSettings::get_settings_path() const
{
    return m_settings_path;
}

OPENDCC_NAMESPACE_CLOSE
