// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>
#if PXR_VERSION < 2108
#include <pxr/imaging/glf/textureRegistry.h>
#include <pxr/imaging/hdSt/textureResource.h>
#endif
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdVol/volume.h>
#include "opendcc/app/viewport/viewport_camera_locator.h"
#include "opendcc/app/viewport/viewport_light_locators.h"
#include "opendcc/app/viewport/viewport_locator_delegate.h"
#include "opendcc/app/viewport/viewport_usd_locator.h"
#include "opendcc/app/viewport/viewport_usd_locator_registry.h"
#include "opendcc/app/viewport/viewport_volume_locator.h"
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/light.h>
#if PXR_VERSION >= 2002
#include <pxr/usd/sdr/registry.h>
#endif

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

extern const std::string usd_locator_unlit_source;
extern const std::string usd_locator_domelight_source;

namespace
{
#if PXR_VERSION >= 2002
    HdMaterialNetworkMap get_unlit_material_network(SdfPath prim_path)
    {
        const auto unlit_node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(usd_locator_unlit_source, TfToken("glslfx"), {});
        const auto unlit_source_id = unlit_node->GetIdentifier();

        HdMaterialNetworkMap material_network_map;
        material_network_map.terminals.push_back(prim_path);
        HdMaterialNetwork& material_network = material_network_map.map[HdMaterialTerminalTokens->surface];
        HdMaterialNode unlit_shader_node = { prim_path, unlit_source_id };
        material_network.nodes.push_back(unlit_shader_node);
        return material_network_map;
    }
#endif

    class DomeLightUsdLocator : public ViewportUsdLightLocator
    {
    public:
        DomeLightUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdLightLocator(scene_delegate, prim, HdPrimTypeTokens->domeLight)
        {
            m_locator_item = std::make_shared<DomeLightLocatorRenderData>();
        }

        void initialize(UsdTimeCode time /*= UsdTimeCode::Default()*/) override
        {
            if (!m_locator_item)
                return;
            ViewportUsdLightLocator::initialize(time);
#if PXR_VERSION < 2008
            m_scene_delegate->GetRenderIndex().InsertBprim(HdPrimTypeTokens->texture, m_scene_delegate, get_texture_path());
#endif
        }

        void update(UsdTimeCode time /*= UsdTimeCode::Default()*/) override
        {
            ViewportUsdLightLocator::update(time);
            if (auto dome_light = UsdLuxDomeLight(m_prim))
            {
                std::unordered_map<std::string, VtValue> data;
                SdfAssetPath texture_path;
                dome_light.GetTextureFileAttr().Get(&texture_path, time);
                data["texture_path"] = texture_path.GetResolvedPath();
                m_texture_path = texture_path.GetResolvedPath();
#if PXR_VERSION < 2008
                m_texture_resource = nullptr;
#endif
                m_locator_item->update(data);
            }
            else
            {
                TF_RUNTIME_ERROR("USD locator has invalid prim type: expected DomeLight, got '%s'.", m_prim.GetTypeName().GetText());
            }
        }

        void mark_dirty(HdDirtyBits bits) override
        {
            ViewportUsdLightLocator::mark_dirty(bits);
#if PXR_VERSION < 2008
            if (!m_texture_path.empty())
            {
                m_scene_delegate->GetRenderIndex().GetChangeTracker().MarkBprimDirty(get_texture_path(), HdChangeTracker::AllDirty);
            }
#endif
        }
#if PXR_VERSION >= 2002
        HdMaterialNetworkMap get_material_resource() const override
        {
            if (m_texture_path.empty())
                return ViewportUsdLightLocator::get_material_resource();

            const auto domelight_node =
                SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(usd_locator_domelight_source, TfToken("glslfx"), {});
            const auto& domelight_source_id = domelight_node->GetIdentifier();

            const auto prim_path = get_index_prim_path();
            HdMaterialNetworkMap material_network_map;
            material_network_map.terminals.push_back(prim_path);
            HdMaterialNetwork& material_network = material_network_map.map[HdMaterialTerminalTokens->surface];
            const auto texture_path = prim_path.AppendProperty(TfToken("texture"));
            HdMaterialNode domelight_shader_node = { prim_path, domelight_source_id };
            domelight_shader_node.parameters[TfToken("texture")] = VtValue(GfVec3f(1, 1, 1));
            HdMaterialNode texture_sampler = { texture_path,
                                               SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdUVTexture"))->GetIdentifier() };
            texture_sampler.parameters[TfToken("file")] = VtValue(SdfAssetPath(m_texture_path));
            HdMaterialRelationship rel;
            rel.inputId = texture_path;
            rel.inputName = TfToken("rgb");
            rel.outputId = prim_path;
            rel.outputName = TfToken("texture");

            material_network.nodes = { texture_sampler, domelight_shader_node };
            material_network.relationships = { rel };

            return material_network_map;
        }
#else
        HdMaterialParamVector get_material_params() const override
        {
            if (m_texture_path.empty())
                return HdMaterialParamVector();

            HdMaterialParamVector params;
            params.push_back(
                HdMaterialParam(HdMaterialParam::ParamTypeTexture, TfToken("texture"), VtValue(GfVec4f(0, 0, 0, 1)), get_texture_path()));
            return params;
        }

        std::string get_surface_shader_source() const override
        {
            return m_texture_path.empty() ? usd_locator_unlit_source : usd_locator_domelight_source;
        }
#endif
#if PXR_VERSION < 2108
        HdTextureResource::ID get_texture_resource_id(const SdfPath& texture_id) const override
        {
#if PXR_VERSION < 2008
            return m_texture_path.empty() ? HdTextureResource::ID() : HdTextureResource::ComputeHash(TfToken(m_texture_path));
#else
            return HdTextureResource::ID();
#endif
        };
        HdTextureResourceSharedPtr get_texture_resource(const SdfPath& texture_id) const override
        {
#if PXR_VERSION >= 2008
            return HdTextureResourceSharedPtr();
#else
            if (m_texture_path.empty())
            {
                return HdTextureResourceSharedPtr();
            }

            if (!m_texture_resource)
            {
                auto texture = GlfTextureRegistry::GetInstance().GetTextureHandle(TfToken(m_texture_path));
                m_texture_resource = HdTextureResourceSharedPtr(new HdStSimpleTextureResource(texture, HdTextureType::Uv, HdWrapClamp, HdWrapClamp,
                                                                                              HdWrapClamp, HdMinFilterLinear, HdMagFilterLinear));
            }

            return m_texture_resource;
#endif
        };

        ~DomeLightUsdLocator()
        {
#if PXR_VERSION < 2008
            m_scene_delegate->GetRenderIndex().RemoveBprim(HdPrimTypeTokens->texture, get_texture_path());
#endif
        }
#endif

    private:
#if PXR_VERSION < 2008
        SdfPath get_texture_path() const { return get_index_prim_path().AppendProperty(HdPrimTypeTokens->texture); }
#endif
        std::string m_texture_path;
#if PXR_VERSION < 2008
        mutable HdTextureResourceSharedPtr m_texture_resource;
#endif
    };

    class CameraUsdLocator : public ViewportUsdGeometryLocator
    {
    public:
        CameraUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdGeometryLocator(scene_delegate, prim)
        {
            m_locator_item = std::make_shared<CameraLocatorRenderData>();
        }
        void update(UsdTimeCode time = UsdTimeCode::Default()) override
        {
            std::unordered_map<std::string, VtValue> data;
            if (auto camera = UsdGeomCamera(m_prim))
            {
                float h_aperture, v_aperture;
                camera.GetHorizontalApertureAttr().Get(&h_aperture, time);
                camera.GetVerticalApertureAttr().Get(&v_aperture, time);

                float h_aperture_offset, v_aperture_offset;
                camera.GetHorizontalApertureOffsetAttr().Get(&h_aperture_offset, time);
                camera.GetVerticalApertureOffsetAttr().Get(&v_aperture_offset, time);

                TfToken proj;
                camera.GetProjectionAttr().Get(&proj, time);

                float focal_length;
                camera.GetFocalLengthAttr().Get(&focal_length, time);

                GfVec2f clipping_range_vec;
                camera.GetClippingRangeAttr().Get(&clipping_range_vec, time);

                const GfRange1f clipping_range = clipping_range_vec[0] < clipping_range_vec[1]
                                                     ? GfRange1f(clipping_range_vec[0], clipping_range_vec[1])
                                                     : GfRange1f(clipping_range_vec[1], clipping_range_vec[0]);

                const GfCamera::Projection proj_type = proj.GetString() == "orthographic" ? GfCamera::Orthographic : GfCamera::Perspective;
                const GfCamera gfcamera(GfMatrix4d(1.0), proj_type, h_aperture, v_aperture, h_aperture_offset, v_aperture_offset, focal_length,
                                        clipping_range);

                data["frustum"] = gfcamera.GetFrustum();
            }
            m_locator_item->update(data);
        }
    };

    class VolumeUsdLocator : public ViewportUsdGeometryLocator
    {
    public:
        VolumeUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdGeometryLocator(scene_delegate, prim)
        {
            m_locator_item = std::make_shared<VolumeLocatorRenderData>();
        }
        void update(UsdTimeCode time = UsdTimeCode::Default()) override
        {
            std::unordered_map<std::string, VtValue> data;
            if (auto volume = UsdVolVolume(m_prim))
            {
                VtVec3fArray extent;
                if (volume.GetExtentAttr().Get<VtArray<GfVec3f>>(&extent, time) && extent.size() == 2)
                {
                    data["extent"] = extent;
                }
            }
            m_locator_item->update(data);
        }
    };

    class RectLightUsdLocator : public ViewportUsdLightLocator
    {
    public:
        RectLightUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdLightLocator(scene_delegate, prim, HdPrimTypeTokens->rectLight)
        {
            m_locator_item = std::make_shared<RectLightLocatorRenderData>();
        }
        void update(UsdTimeCode time = UsdTimeCode::Default()) override
        {
            ViewportUsdLightLocator::update(time);
            std::unordered_map<std::string, VtValue> data;
            if (auto light = UsdLuxRectLight(m_prim))
            {
                float width, height;
                light.GetWidthAttr().Get(&width, time);
                light.GetHeightAttr().Get(&height, time);

                data["width"] = width;
                data["height"] = height;
            }
            m_locator_item->update(data);
        }
    };

    class SphereLightUsdLocator : public ViewportUsdLightLocator
    {
    public:
        SphereLightUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdLightLocator(scene_delegate, prim, HdPrimTypeTokens->sphereLight)
        {
            m_locator_item = std::make_shared<SphereLightLocatorRenderData>();
        }
        void update(UsdTimeCode time = UsdTimeCode::Default()) override
        {
            ViewportUsdLightLocator::update(time);
            std::unordered_map<std::string, VtValue> data;
            if (auto light = UsdLuxSphereLight(m_prim))
            {
                float radius;
                light.GetRadiusAttr().Get(&radius, time);
                data["radius"] = radius;
            }
            m_locator_item->update(data);
        }
    };
    class DiskLightUsdLocator : public ViewportUsdLightLocator
    {
    public:
        DiskLightUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdLightLocator(scene_delegate, prim, HdPrimTypeTokens->diskLight)
        {
            m_locator_item = std::make_shared<DiskLightLocatorRenderData>();
        }
        void update(UsdTimeCode time = UsdTimeCode::Default()) override
        {
            ViewportUsdLightLocator::update(time);
            std::unordered_map<std::string, VtValue> data;
            if (auto light = UsdLuxDiskLight(m_prim))
            {
                float radius;
                light.GetRadiusAttr().Get(&radius, time);
                data["radius"] = radius;
            }
            m_locator_item->update(data);
        }
    };
    class CylinderLightUsdLocator : public ViewportUsdLightLocator
    {
    public:
        CylinderLightUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdLightLocator(scene_delegate, prim, HdPrimTypeTokens->cylinderLight)
        {
            m_locator_item = std::make_shared<CylinderLightLocatorRenderData>();
        }
        void update(UsdTimeCode time = UsdTimeCode::Default()) override
        {
            ViewportUsdLightLocator::update(time);
            std::unordered_map<std::string, VtValue> data;
            if (auto light = UsdLuxCylinderLight(m_prim))
            {
                float radius, length;
                light.GetRadiusAttr().Get(&radius, time);
                light.GetLengthAttr().Get(&length, time);

                data["radius"] = radius;
                data["length"] = length;
            }
            m_locator_item->update(data);
        }
    };
    class DistantLightUsdLocator : public ViewportUsdLightLocator
    {
    public:
        DistantLightUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
            : ViewportUsdLightLocator(scene_delegate, prim, HdPrimTypeTokens->distantLight)
        {
            m_locator_item = std::make_shared<DirectLightLocatorData>();
        }
    };
}; // namespace

OPENDCC_REGISTER_USD_LOCATOR(RectLightUsdLocator, TfToken("RectLight"));
OPENDCC_REGISTER_USD_LOCATOR(DistantLightUsdLocator, TfToken("DistantLight"));
OPENDCC_REGISTER_USD_LOCATOR(CylinderLightUsdLocator, TfToken("CylinderLight"));
OPENDCC_REGISTER_USD_LOCATOR(DiskLightUsdLocator, TfToken("DiskLight"));
OPENDCC_REGISTER_USD_LOCATOR(SphereLightUsdLocator, TfToken("SphereLight"));
OPENDCC_REGISTER_USD_LOCATOR(DomeLightUsdLocator, TfToken("DomeLight"));
OPENDCC_REGISTER_USD_LOCATOR(CameraUsdLocator, TfToken("Camera"));
OPENDCC_REGISTER_USD_LOCATOR(VolumeUsdLocator, TfToken("Volume"));

ViewportUsdLocator::ViewportUsdLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
    : m_scene_delegate(scene_delegate)
    , m_prim(prim)
{
}

SdfPath ViewportUsdLocator::get_index_prim_path() const
{
    return m_scene_delegate->GetDelegateID().AppendPath(m_prim.GetPrimPath().MakeRelativePath(SdfPath::AbsoluteRootPath()));
}

ViewportUsdGeometryLocator::ViewportUsdGeometryLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim)
    : ViewportUsdLocator(scene_delegate, prim)
{
}

ViewportUsdGeometryLocator::~ViewportUsdGeometryLocator()
{
    m_scene_delegate->GetRenderIndex().RemoveRprim(get_index_prim_path());
    m_scene_delegate->GetRenderIndex().RemoveSprim(HdPrimTypeTokens->material, get_material_id());
}

void ViewportUsdGeometryLocator::initialize(UsdTimeCode time)
{
    if (!m_locator_item)
        return;

    m_scene_delegate->GetRenderIndex().InsertRprim(m_locator_item->as_mesh() ? HdPrimTypeTokens->mesh : HdPrimTypeTokens->basisCurves,
                                                   m_scene_delegate, get_index_prim_path());
    m_scene_delegate->GetRenderIndex().InsertSprim(HdPrimTypeTokens->material, m_scene_delegate, get_material_id());
}
void ViewportUsdGeometryLocator::mark_dirty(HdDirtyBits bits)
{
    m_scene_delegate->GetRenderIndex().GetChangeTracker().MarkRprimDirty(get_index_prim_path(), bits);
}

SdfPath ViewportUsdGeometryLocator::get_material_id() const
{
    return get_index_prim_path().AppendProperty(HdPrimTypeTokens->material);
}

#if PXR_VERSION >= 2002
HdMaterialNetworkMap ViewportUsdGeometryLocator::get_material_resource() const
{
    return get_unlit_material_network(get_index_prim_path());
}

#else
std::string ViewportUsdGeometryLocator::get_surface_shader_source() const
{
    return usd_locator_unlit_source;
}
#endif

ViewportUsdLightLocator::ViewportUsdLightLocator(ViewportLocatorDelegate* scene_delegate, const UsdPrim& prim, const TfToken& light_type)
    : ViewportUsdLocator(scene_delegate, prim)
    , m_light_type(light_type)
{
}

ViewportUsdLightLocator::~ViewportUsdLightLocator()
{
    m_scene_delegate->GetRenderIndex().RemoveRprim(get_index_prim_path());
    m_scene_delegate->GetRenderIndex().RemoveSprim(HdPrimTypeTokens->material, get_material_id());
}

void ViewportUsdLightLocator::initialize(UsdTimeCode time)
{
    if (!m_locator_item)
        return;

    m_scene_delegate->GetRenderIndex().InsertRprim(m_locator_item->as_mesh() ? HdPrimTypeTokens->mesh : HdPrimTypeTokens->basisCurves,
                                                   m_scene_delegate, get_index_prim_path());
    m_scene_delegate->GetRenderIndex().InsertSprim(HdPrimTypeTokens->material, m_scene_delegate, get_material_id());
    auto main_render_index = m_scene_delegate->get_main_render_index();
    if (main_render_index.expired())
        return;
    if (!main_render_index.lock()->IsSprimTypeSupported(m_light_type))
        m_light_type = HdPrimTypeTokens->simpleLight;
}

void ViewportUsdLightLocator::mark_dirty(HdDirtyBits bits)
{
    m_scene_delegate->GetRenderIndex().GetChangeTracker().MarkRprimDirty(get_index_prim_path(), bits);
    m_scene_delegate->GetRenderIndex().GetChangeTracker().MarkSprimDirty(get_material_id(), HdMaterial::DirtyBits::AllDirty);
}

SdfPath ViewportUsdLightLocator::get_material_id() const
{
    return get_index_prim_path().AppendProperty(HdPrimTypeTokens->material);
}

#if PXR_VERSION >= 2002
HdMaterialNetworkMap ViewportUsdLightLocator::get_material_resource() const
{
    return get_unlit_material_network(get_index_prim_path());
}
#else
std::string ViewportUsdLightLocator::get_surface_shader_source() const
{
    return usd_locator_unlit_source;
}

#endif

void ViewportUsdLightLocator::update(PXR_NS::UsdTimeCode time) {}

OPENDCC_NAMESPACE_CLOSE
