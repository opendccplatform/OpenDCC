// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/usd_viewport_locator_data.h"
#include "opendcc/app/viewport/viewport_locator_delegate.h"
#include "opendcc/app/viewport/viewport_light_locators.h"
#include "opendcc/app/viewport/viewport_camera_locator.h"
#include "opendcc/app/viewport/viewport_volume_locator.h"

#include <pxr/imaging/pxOsd/tokens.h>
#include <pxr/imaging/hd/primGather.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/base/gf/frustum.h>
#include "pxr/usd/usdVol/volume.h"
#include "opendcc/app/viewport/viewport_usd_locator_registry.h"
#if PXR_VERSION >= 2002
#include <pxr/usd/sdr/registry.h>
#endif

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    bool is_time_varying(const UsdPrim& prim)
    {
        for (const auto& attribute : prim.GetAttributes())
        {
            if (attribute.ValueMightBeTimeVarying())
            {
                return true;
            }
        }

        auto current_prim = prim;
        do
        {
            UsdGeomXformable xform(current_prim);
            if (xform)
            {
                if (xform.TransformMightBeTimeVarying())
                {
                    // early out if found any animated transform
                    return true;
                }
                // If the xformable prim resets the transform stack, then
                // we dont have to check parents
                if (xform.GetResetXformStack())
                {
                    return false;
                }
            }

            current_prim = current_prim.GetParent();
        } while (current_prim.GetPath() != SdfPath::AbsoluteRootPath());

        return false;
    }
}

UsdViewportLocatorData::UsdViewportLocatorData(ViewportLocatorDelegate* scene_delegate)
    : m_delegate(scene_delegate)
{
    assert(m_delegate);
}

bool UsdViewportLocatorData::is_locator(const SdfPath& path) const
{
    auto prim = m_delegate->m_cur_stage->GetPrimAtPath(path);
    return ViewportUsdLocatorRegistry::has_factory(prim.GetTypeName());
}

bool UsdViewportLocatorData::insert_locator(const SdfPath& path, const UsdTimeCode& time)
{
    auto locator_iter = m_delegate->m_locators.find(path);
    if (locator_iter == m_delegate->m_locators.end())
    {
        auto locator = ViewportUsdLocatorRegistry::create_locator(m_delegate, m_delegate->m_cur_stage->GetPrimAtPath(path));
        if (!locator)
        {
            TF_RUNTIME_ERROR("Failed to create locator for prim '%s'.", path.GetText());
            return false;
        }
        m_delegate->m_tasks.push([this, path]() { insert_locator(path); });
        m_delegate->m_locators.emplace(path, locator);
        if (is_time_varying(m_delegate->m_cur_stage->GetPrimAtPath(path)))
        {
            m_delegate->m_time_varying_locators.insert(path);
        }
        return true;
    }

    const auto& locator = locator_iter->second;
    locator->initialize(time);
    m_delegate->m_prim_ids.Insert(path);
    locator->update(time);
    return true;
}

void UsdViewportLocatorData::mark_locator_dirty(const SdfPath& dirty_path, HdDirtyBits bits)
{
    auto iter = m_delegate->m_locators.find(dirty_path);
    if (iter == m_delegate->m_locators.end())
        return;
    iter->second->mark_dirty(bits);
}

void UsdViewportLocatorData::update(const SdfPath& path, const UsdTimeCode& time)
{
    auto locator_iter = m_delegate->m_locators.find(path);
    if (locator_iter == m_delegate->m_locators.end())
    {
        return;
    }
    locator_iter->second->update(time);
}

void UsdViewportLocatorData::initialize_locators(const UsdTimeCode& time)
{
    std::unordered_set<SdfPath, SdfPath::Hash> inserted_locators;
    for (auto item : m_delegate->m_cur_stage->TraverseAll())
    {
        if (is_locator(item.GetPrimPath()))
        {
            if (m_delegate->m_locators.find(item.GetPrimPath()) == m_delegate->m_locators.end())
            {
                if (insert_locator(item.GetPrimPath(), time))
                    inserted_locators.emplace(item.GetPrimPath());
            }
        }
    }
}

bool UsdViewportLocatorData::contains_light(const SdfPath& path) const
{
    auto iter = m_delegate->m_locators.find(path);
    return iter != m_delegate->m_locators.end() && std::dynamic_pointer_cast<ViewportUsdLightLocator>(iter->second);
}

bool UsdViewportLocatorData::contains_texture(const SdfPath& path) const
{
    auto iter = m_delegate->m_locators.find(path);
    if (iter == m_delegate->m_locators.end())
        return false;
#if PXR_VERSION >= 2002
    auto mat_network = iter->second->get_material_resource();
    auto mat_map_iter = mat_network.map.find(HdMaterialTerminalTokens->surface);
    if (mat_map_iter == mat_network.map.end())
        return false;

    static const auto texture_identifier = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdUVTexture"))->GetIdentifier();
    for (const auto& node : mat_map_iter->second.nodes)
    {
        if (node.identifier == texture_identifier)
            return true;
    }

#else
    auto params = iter->second->get_material_params();
    for (const auto& param : params)
    {
        if (param.IsTexture())
            return true;
    }
#endif
    return false;
}

void UsdViewportLocatorData::remove_locator(const SdfPath& path)
{
    m_delegate->m_time_varying_locators.erase(path);
    m_delegate->m_locators.erase(path);
}

OPENDCC_NAMESPACE_CLOSE
