// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_usd_delegate.h"
#include "opendcc/base/logging/logger.h"

#include "opendcc/app/ui/application_ui.h"

#include "opendcc/app/core/session.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/selection_list.h"

#include "opendcc/app/viewport/istage_resolver.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/app/viewport/persistent_material_override.h"

#include <pxr/base/tf/diagnosticMgr.h>

#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/primGather.h>

#include <pxr/usdImaging/usdImaging/tokens.h>

#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/basisCurves.h>

#if PXR_VERSION <= 2108
#include <pxr/usd/usdLux/light.h>
#else
#include <pxr/usd/usdLux/boundableLightBase.h>
#include <pxr/usd/usdLux/nonboundableLightBase.h>
#endif
#include "viewport_locator_delegate.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.usd");

PXR_NAMESPACE_USING_DIRECTIVE

static HdReprSelector get_repr_selector_from_pick_target(Application::SelectionMode selection_mode)
{
    if (Application::instance().is_soft_selection_enabled() && selection_mode != Application::SelectionMode::POINTS)
        return HdReprSelector(HdReprTokens->refinedWireOnSurf, TfToken(), HdReprTokens->points);

    HdReprSelector repr_selector;
    switch (selection_mode)
    {
    case Application::SelectionMode::POINTS:
    case Application::SelectionMode::UV:
        repr_selector = HdReprSelector(HdReprTokens->refinedWireOnSurf, TfToken(), HdReprTokens->points);
        break;
    case Application::SelectionMode::EDGES:
    case Application::SelectionMode::FACES:
        repr_selector = repr_selector.CompositeOver(HdReprSelector(HdReprTokens->refinedWireOnSurf));
        break;
    default:
        repr_selector = repr_selector.CompositeOver(HdReprSelector(HdReprTokens->refined));
    }
    return repr_selector;
}

//////////////////////////////////////////////////////////////////////////
// ViewportUsdDelegate
//////////////////////////////////////////////////////////////////////////

OPENDCC_REGISTER_SCENE_DELEGATE(ViewportUsdDelegate, TfToken("USD"))

static const SdfPath s_prefix_material_override_path = SdfPath("__UsdImagingDelegate/override_materials");

ViewportUsdDelegate::ViewportUsdDelegate(HdRenderIndex* render_index, const SdfPath& delegate_id)
    : ViewportSceneDelegate(render_index, delegate_id)
    , UsdImagingDelegate(render_index, delegate_id)
{
}

ViewportUsdDelegate::~ViewportUsdDelegate()
{
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED, m_tool_changed_handle);

    if (auto tool = ApplicationUI::instance().get_current_viewport_tool())
    {
        if (auto over = tool->get_prim_material_override())
        {
            over->unregister_callback(m_tool_material_handle);
            over->unregister_callback(m_tool_assignment_handle);
            over->unregister_callback(m_tool_material_resource_handle);
        }
    }

    auto& persistent = PersistentMaterialOverride::instance();
    auto over = persistent.get_override();

    over->unregister_callback(m_persistent_material_handle);
    over->unregister_callback(m_persistent_assignment_handle);
    over->unregister_callback(m_persistent_material_resource_handle);

    UsdViewportRefineManager::instance().unregister_refine_level_changed_callback(m_usd_refine_handle);
    UsdViewportRefineManager::instance().unregister_stage_cleared_callback(m_usd_stage_cleared_handle);
}

void ViewportUsdDelegate::update(const ViewportHydraEngineParams& engine_params)
{
    if (!m_stage)
        initialize(engine_params);
    if (!m_stage)
        return;
    if (engine_params.visibility_mask.is_dirty())
    {
        m_visibility_mask = engine_params.visibility_mask;
        UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkAllRprimsDirty(HdChangeTracker::DirtyVisibility);
    }
    update_repr_paths(engine_params.repr_paths);
    prepare_batch(engine_params);
    SetSceneMaterialsEnabled(engine_params.enable_scene_materials);
    SetSceneLightsEnabled(!engine_params.use_camera_light);
}

void ViewportUsdDelegate::populate_selection(const SelectionList& selection_list, const PXR_NS::HdSelectionSharedPtr& result)
{
    for (const auto& selection_entry : selection_list)
    {
        const auto path = convert_stage_path_to_index_path(selection_entry.first);
        if (selection_entry.second.is_fully_selected())
        {
            if (selection_entry.second.get_instance_indices().empty())
                PopulateSelection(m_selection_mode, selection_entry.first, UsdImagingDelegate::ALL_INSTANCES, result);
        }
        else
        {
            const auto& data = selection_entry.second;
            if (!data.get_point_indices().empty())
                result->AddPoints(m_selection_mode, path, data.get_point_index_intervals().flatten<VtIntArray>());
            if (!data.get_edge_indices().empty())
                result->AddEdges(m_selection_mode, path, data.get_edge_index_intervals().flatten<VtIntArray>());
            if (!data.get_element_indices().empty())
                result->AddElements(m_selection_mode, path, data.get_element_index_intervals().flatten<VtIntArray>());
            if (!data.get_instance_indices().empty())
            {
                for (const auto instance_index : data.get_instance_indices())
                    PopulateSelection(m_selection_mode, selection_entry.first, instance_index, result);
            }
        }
    }
}

GfRange3d ViewportUsdDelegate::GetExtent(SdfPath const& id)
{
    auto session = Application::instance().get_session();
    if (auto prim = m_stage->GetPrimAtPath(convert_index_path_to_stage_path(id)))
    {
        TfTokenVector tokens;
        tokens.push_back(UsdGeomTokens->default_);
        UsdGeomBBoxCache bbox_cache(GetTime(), tokens, true);
        auto bounds = bbox_cache.ComputeUntransformedBound(prim);
        return bounds.ComputeAlignedRange();
    }
    return UsdImagingDelegate::GetExtent(id);
}

GfMatrix4d ViewportUsdDelegate::GetTransform(SdfPath const& id)
{
    if (auto prim = m_stage->GetPrimAtPath(convert_index_path_to_stage_path(id)))
    {
        UsdGeomXformCache cache(GetTime());
        return cache.GetLocalToWorldTransform(prim);
    }
    return UsdImagingDelegate::GetTransform(id);
}

HdReprSelector ViewportUsdDelegate::GetReprSelector(SdfPath const& id)
{
    const auto converted = convert_index_path_to_stage_path(id);
    auto iter = m_repr_paths.find(converted);

    auto& app = Application::instance();
    const auto mode = app.get_selection_mode();

    if (mode == Application::SelectionMode::UV && UsdGeomCurves(m_stage->GetPrimAtPath(converted)))
    {
        HdReprSelector repr_selector;
        repr_selector = repr_selector.CompositeOver(HdReprSelector(HdReprTokens->refinedWireOnSurf));
        return repr_selector;
    }

    if (iter != m_repr_paths.end())
        return m_repr_selector;

    return UsdImagingDelegate::GetReprSelector(converted);
}
#if PXR_VERSION < 2005
SdfPath ViewportUsdDelegate::GetPathForInstanceIndex(const SdfPath& protoRprimId, int protoIndex, int* instancerIndex,
                                                     SdfPath* masterCachePath /*= nullptr*/, SdfPathVector* instanceContext /*= nullptr*/)
{
    return UsdImagingDelegate::GetPathForInstanceIndex(protoRprimId, protoIndex, instancerIndex, masterCachePath, instanceContext);
}
#else
PXR_NS::SdfPath ViewportUsdDelegate::GetScenePrimPath(PXR_NS::SdfPath const& rprimId, int instanceIndex, PXR_NS::HdInstancerContext* instancerContext)
{
    return UsdImagingDelegate::GetScenePrimPath(rprimId, instanceIndex, instancerContext);
}
#endif
void ViewportUsdDelegate::Sync(HdSyncRequestVector* request)
{
    UsdImagingDelegate::Sync(request);
}

void ViewportUsdDelegate::PostSyncCleanup()
{
    UsdImagingDelegate::PostSyncCleanup();
}

bool ViewportUsdDelegate::IsEnabled(TfToken const& option) const
{
    return UsdImagingDelegate::IsEnabled(option);
}

HdMeshTopology ViewportUsdDelegate::GetMeshTopology(SdfPath const& id)
{
    auto topology = UsdImagingDelegate::GetMeshTopology(id);

    auto subsets = topology.GetGeomSubsets();
    for (auto& subset : subsets)
    {
        subset.id = convert_stage_path_to_index_path(subset.id);
        subset.materialId = convert_stage_path_to_index_path(subset.materialId);
    }
    topology.SetGeomSubsets(subsets);

    return topology;
}

HdBasisCurvesTopology ViewportUsdDelegate::GetBasisCurvesTopology(SdfPath const& id)
{
    return UsdImagingDelegate::GetBasisCurvesTopology(id);
}

PxOsdSubdivTags ViewportUsdDelegate::GetSubdivTags(SdfPath const& id)
{
    return UsdImagingDelegate::GetSubdivTags(id);
}

bool ViewportUsdDelegate::GetVisible(SdfPath const& id)
{
    auto prim = m_stage->GetPrimAtPath(convert_index_path_to_stage_path(id));
    if (!prim)
        return UsdImagingDelegate::GetVisible(id);
    if (UsdGeomCamera(prim) && !m_visibility_mask.is_visible(PrimVisibilityTypes->camera))
        return false;
#if PXR_VERSION <= 2108
    if (UsdLuxLight(prim) && !m_visibility_mask.is_visible(PrimVisibilityTypes->light))
        return false;
#else
    if ((UsdLuxBoundableLightBase(prim) || UsdLuxNonboundableLightBase(prim)) && !m_visibility_mask.is_visible(PrimVisibilityTypes->light))
        return false;
#endif
    if (UsdGeomMesh(prim) && !m_visibility_mask.is_visible(PrimVisibilityTypes->mesh))
        return false;
    if (UsdGeomBasisCurves(prim) && !m_visibility_mask.is_visible(PrimVisibilityTypes->basisCurves))
        return false;
    return UsdImagingDelegate::GetVisible(id);
}

bool ViewportUsdDelegate::GetDoubleSided(SdfPath const& id)
{
    return UsdImagingDelegate::GetDoubleSided(id);
}

HdCullStyle ViewportUsdDelegate::GetCullStyle(SdfPath const& id)
{
    return UsdImagingDelegate::GetCullStyle(id);
}

VtValue ViewportUsdDelegate::GetShadingStyle(SdfPath const& id)
{
    return UsdImagingDelegate::GetShadingStyle(id);
}

HdDisplayStyle ViewportUsdDelegate::GetDisplayStyle(SdfPath const& id)
{
    return UsdImagingDelegate::GetDisplayStyle(id);
}

VtValue ViewportUsdDelegate::Get(SdfPath const& id, TfToken const& key)
{
    return UsdImagingDelegate::Get(id, key);
}

TfToken ViewportUsdDelegate::GetRenderTag(SdfPath const& id)
{
    return UsdImagingDelegate::GetRenderTag(id);
}

VtArray<TfToken> ViewportUsdDelegate::GetCategories(SdfPath const& id)
{
    return UsdImagingDelegate::GetCategories(id);
}

std::vector<VtArray<TfToken>> ViewportUsdDelegate::GetInstanceCategories(SdfPath const& instancerId)
{
    return UsdImagingDelegate::GetInstanceCategories(instancerId);
}

HdIdVectorSharedPtr ViewportUsdDelegate::GetCoordSysBindings(SdfPath const& id)
{
    return UsdImagingDelegate::GetCoordSysBindings(id);
}

size_t ViewportUsdDelegate::SampleTransform(SdfPath const& id, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues)
{
    return UsdImagingDelegate::SampleTransform(id, maxSampleCount, sampleTimes, sampleValues);
}

size_t ViewportUsdDelegate::SampleInstancerTransform(SdfPath const& instancerId, size_t maxSampleCount, float* sampleTimes, GfMatrix4d* sampleValues)
{
    return UsdImagingDelegate::SampleInstancerTransform(instancerId, maxSampleCount, sampleTimes, sampleValues);
}

size_t ViewportUsdDelegate::SamplePrimvar(SdfPath const& id, TfToken const& key, size_t maxSampleCount, float* sampleTimes, VtValue* sampleValues)
{
    return UsdImagingDelegate::SamplePrimvar(id, key, maxSampleCount, sampleTimes, sampleValues);
}

VtIntArray ViewportUsdDelegate::GetInstanceIndices(SdfPath const& instancerId, SdfPath const& prototypeId)
{
    return UsdImagingDelegate::GetInstanceIndices(instancerId, prototypeId);
}

GfMatrix4d ViewportUsdDelegate::GetInstancerTransform(SdfPath const& instancerId)
{
#if PXR_VERSION >= 2108
    if (m_on_get_instancer_transform)
    {
        auto result = m_on_get_instancer_transform(ConvertIndexPathToCachePath(instancerId));
        if (std::get<1>(result))
            return std::get<0>(result);
    }
#endif
    return UsdImagingDelegate::GetInstancerTransform(instancerId);
}

SdfPath ViewportUsdDelegate::GetMaterialId(SdfPath const& rprimId)
{
    const auto assignments = { m_tool_material_assignments, m_persistent_material_assignments };

    for (const auto& assignment : assignments)
    {
        auto mat_iter = assignment.find(rprimId);
        if (mat_iter != assignment.end())
        {
            return mat_iter->second;
        }
    }

    return UsdImagingDelegate::GetMaterialId(rprimId);
}

#if PXR_VERSION < 2002
std::string ViewportUsdDelegate::GetSurfaceShaderSource(SdfPath const& materialId)
{
    auto mat_iter = m_material_overrides.find(materialId);
    if (mat_iter != m_material_overrides.end())
    {
        return mat_iter->second.get_surface_shader_source();
    }
    return UsdImagingDelegate::GetSurfaceShaderSource(materialId);
}

std::string ViewportUsdDelegate::GetDisplacementShaderSource(SdfPath const& materialId)
{
    // TODO: implement displacement shader for material overrides
    auto mat_iter = m_material_overrides.find(materialId);
    if (mat_iter != m_material_overrides.end())
    {
        return ViewportSceneDelegate::GetDisplacementShaderSource(materialId);
    }
    return UsdImagingDelegate::GetDisplacementShaderSource(materialId);
}

VtValue ViewportUsdDelegate::GetMaterialParamValue(SdfPath const& materialId, TfToken const& paramName)
{
    // TODO: implement additional material params for material overrides
    auto mat_iter = m_material_overrides.find(materialId);
    if (mat_iter != m_material_overrides.end())
    {
        return ViewportSceneDelegate::GetMaterialParamValue(materialId, paramName);
    }
    return UsdImagingDelegate::GetMaterialParamValue(materialId, paramName);
}

HdMaterialParamVector ViewportUsdDelegate::GetMaterialParams(SdfPath const& materialId)
{
    // TODO: implement additional material params for material overrides
    auto mat_iter = m_material_overrides.find(materialId);
    if (mat_iter != m_material_overrides.end())
    {
        return ViewportSceneDelegate::GetMaterialParams(materialId);
    }
    return UsdImagingDelegate::GetMaterialParams(materialId);
}

VtDictionary ViewportUsdDelegate::GetMaterialMetadata(SdfPath const& materialId)
{
    // TODO: implement material metadata for material overrides
    auto mat_iter = m_material_overrides.find(materialId);
    if (mat_iter != m_material_overrides.end())
    {
        return ViewportSceneDelegate::GetMaterialMetadata(materialId);
    }
    return UsdImagingDelegate::GetMaterialMetadata(materialId);
}
#else

VtValue ViewportUsdDelegate::GetMaterialResource(SdfPath const& materialId)
{
    const auto overrides = { m_tool_material_overrides, m_tool_material_resource_overrides, m_persistent_material_overrides,
                             m_persistent_material_resource_overrides };

    for (const auto& override : overrides)
    {
        auto mat_iter = override.find(materialId);
        if (mat_iter != override.end())
        {
            return mat_iter->second.get_material_resource();
        }
    }

    return UsdImagingDelegate::GetMaterialResource(materialId);
}
#endif

#if PXR_VERSION < 2108
HdTextureResource::ID ViewportUsdDelegate::GetTextureResourceID(SdfPath const& textureId)
{
    return UsdImagingDelegate::GetTextureResourceID(textureId);
}

HdTextureResourceSharedPtr ViewportUsdDelegate::GetTextureResource(SdfPath const& textureId)
{
    return UsdImagingDelegate::GetTextureResource(textureId);
}
#endif
HdRenderBufferDescriptor ViewportUsdDelegate::GetRenderBufferDescriptor(SdfPath const& id)
{
    return UsdImagingDelegate::GetRenderBufferDescriptor(id);
}

VtValue ViewportUsdDelegate::GetLightParamValue(SdfPath const& id, TfToken const& paramName)
{
    return UsdImagingDelegate::GetLightParamValue(id, paramName);
}

VtValue ViewportUsdDelegate::GetCameraParamValue(SdfPath const& cameraId, TfToken const& paramName)
{
    return UsdImagingDelegate::GetCameraParamValue(cameraId, paramName);
}

HdVolumeFieldDescriptorVector ViewportUsdDelegate::GetVolumeFieldDescriptors(SdfPath const& volumeId)
{
    return UsdImagingDelegate::GetVolumeFieldDescriptors(volumeId);
}

TfTokenVector ViewportUsdDelegate::GetExtComputationSceneInputNames(SdfPath const& computationId)
{
    return UsdImagingDelegate::GetExtComputationSceneInputNames(computationId);
}

HdExtComputationInputDescriptorVector ViewportUsdDelegate::GetExtComputationInputDescriptors(SdfPath const& computationId)
{
    return UsdImagingDelegate::GetExtComputationInputDescriptors(computationId);
}

HdExtComputationOutputDescriptorVector ViewportUsdDelegate::GetExtComputationOutputDescriptors(SdfPath const& computationId)
{
    return UsdImagingDelegate::GetExtComputationOutputDescriptors(computationId);
}

HdExtComputationPrimvarDescriptorVector ViewportUsdDelegate::GetExtComputationPrimvarDescriptors(SdfPath const& id, HdInterpolation interpolationMode)
{
    return UsdImagingDelegate::GetExtComputationPrimvarDescriptors(id, interpolationMode);
}

VtValue ViewportUsdDelegate::GetExtComputationInput(SdfPath const& computationId, TfToken const& input)
{
    return UsdImagingDelegate::GetExtComputationInput(computationId, input);
}

std::string ViewportUsdDelegate::GetExtComputationKernel(SdfPath const& computationId)
{
    return UsdImagingDelegate::GetExtComputationKernel(computationId);
}

void ViewportUsdDelegate::InvokeExtComputation(SdfPath const& computationId, HdExtComputationContext* context)
{
    UsdImagingDelegate::InvokeExtComputation(computationId, context);
}

HdPrimvarDescriptorVector ViewportUsdDelegate::GetPrimvarDescriptors(SdfPath const& id, HdInterpolation interpolation)
{
    auto base_primvars = UsdImagingDelegate::GetPrimvarDescriptors(id, interpolation);

    struct Override
    {
        OverrideAssignments& assignments;
        OverrideMap& material_overrides;
        OverrideMap& material_resource_overrides;
    };
    Override overrides[] = { {
                                 m_tool_material_assignments,
                                 m_tool_material_overrides,
                                 m_tool_material_resource_overrides,
                             },
                             {
                                 m_persistent_material_assignments,
                                 m_persistent_material_overrides,
                                 m_persistent_material_resource_overrides,
                             } };

    for (const auto& override : overrides)
    {
        auto ass_iter = override.assignments.find(id);
        if (ass_iter == override.assignments.end())
        {
            continue;
        }

        auto mat_iter = override.material_overrides.find(ass_iter->second);
        if (mat_iter == override.material_overrides.end())
        {
            mat_iter = override.material_resource_overrides.find(ass_iter->second);
            if (mat_iter == override.material_resource_overrides.end())
            {
                OPENDCC_ERROR("Inconsistent material override");
                return base_primvars;
            }
        }

        for (int i = 0; i < static_cast<int>(HdInterpolation::HdInterpolationCount); i++)
        {
            const auto custom_primvars = mat_iter->second.get_primvar_descriptors(static_cast<HdInterpolation>(i));
            for (const auto& primvar : custom_primvars)
            {
                auto usd_primvar = std::find_if(base_primvars.begin(), base_primvars.end(),
                                                [custom_name = primvar.name](const HdPrimvarDescriptor& descr) { return descr.name == custom_name; });

                if (usd_primvar != base_primvars.end())
                {
                    base_primvars.erase(usd_primvar);
                }
            }
        }

        if (mat_iter->second.has_primvar_descriptor(interpolation))
        {
            auto custom_primvars = mat_iter->second.get_primvar_descriptors(interpolation);
            base_primvars.insert(base_primvars.end(), custom_primvars.begin(), custom_primvars.end());
        }
    }

    return base_primvars;
}

TfTokenVector ViewportUsdDelegate::GetTaskRenderTags(SdfPath const& taskId)
{
    return UsdImagingDelegate::GetTaskRenderTags(taskId);
}

void ViewportUsdDelegate::initialize(const ViewportHydraEngineParams& engine_params)
{
    UsdStageRefPtr current_stage;
    if (engine_params.stage_resolver)
        current_stage = engine_params.stage_resolver->get_stage(UsdImagingDelegate::GetDelegateID().GetParentPath());
    else
        current_stage = Application::instance().get_session()->get_current_stage();

    if (!current_stage)
        return;

    m_stage = current_stage; // UsdStage::Open(current_stage->GetRootLayer());
    if (!m_stage)
        return;
    auto root_prim = m_stage->GetPseudoRoot();
    if (!root_prim)
    {
        m_stage = nullptr;
        return;
    }

    auto init_material_overrides = [this] {
        clear_tool_overrides();

        auto tool = ApplicationUI::instance().get_current_viewport_tool();
        if (!tool)
        {
            return;
        }

        auto over = tool->get_prim_material_override();
        if (!over)
        {
            return;
        }

        m_tool_material_handle = over->register_callback<PrimMaterialOverride::EventType::MATERIAL>(
            [this](size_t material_id, const PrimMaterialDescriptor& descr, PrimMaterialOverride::Status status) {
                on_material_changed(material_id, descr, status, m_tool_material_overrides, m_tool_material_assignments);
            });
        m_tool_assignment_handle = over->register_callback<PrimMaterialOverride::EventType::ASSIGNMENT>(
            [this](size_t material_id, const PXR_NS::SdfPath& assignment, PrimMaterialOverride::Status status) {
                on_material_assignment_changed(material_id, assignment, status, m_tool_material_overrides, m_tool_material_assignments);
            });
        m_tool_material_resource_handle = over->register_callback<PrimMaterialOverride::EventType::MATERIAL_RESOURCE>(
            [this](SdfPath mat_path, const PrimMaterialDescriptor& descr, PrimMaterialOverride::Status status) {
                on_material_resource_changed(mat_path, descr, status, m_tool_material_overrides, m_tool_material_resource_overrides,
                                             m_tool_material_assignments);
            });

        for (const auto& mat : over->get_materials())
        {
            on_material_changed(mat.first, mat.second, PrimMaterialOverride::Status::NEW, m_tool_material_overrides, m_tool_material_assignments);
        }
        for (const auto& assign : over->get_assignments())
        {
            on_material_assignment_changed(assign.second, assign.first, PrimMaterialOverride::Status::NEW, m_tool_material_overrides,
                                           m_tool_material_assignments);
        }
        for (const auto& resource : over->get_material_resource_overrides())
        {
            on_material_resource_changed(resource.first, resource.second, PrimMaterialOverride::Status::NEW, m_tool_material_overrides,
                                         m_tool_material_resource_overrides, m_tool_material_assignments);
        }
    };

    m_tool_changed_handle = Application::instance().register_event_callback(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED,
                                                                            [this, init_material_overrides] { init_material_overrides(); });
    m_usd_refine_handle = UsdViewportRefineManager::instance().register_refine_level_changed_callback(
        [this](const UsdStageCache::Id& id, const SdfPath& prim_path, int refine_level) {
            for (const auto& path : ViewportSceneDelegate::GetRenderIndex().GetRprimSubtree(convert_stage_path_to_index_path(prim_path)))
            {
                auto prim = m_stage->GetPrimAtPath(convert_index_path_to_stage_path(path));

                if (prim.IsValid() && prim.IsA<UsdGeomGprim>())
                {
                    SetRefineLevel(convert_index_path_to_stage_path(path), refine_level);
                }
            }
        });
    m_usd_stage_cleared_handle = UsdViewportRefineManager::instance().register_stage_cleared_callback([this](const UsdStageCache::Id& id) {
        for (const auto& path : ViewportSceneDelegate::GetRenderIndex().GetRprimSubtree(UsdImagingDelegate::GetDelegateID()))
        {
            auto prim = m_stage->GetPrimAtPath(convert_index_path_to_stage_path(path));
            if (prim.IsValid() && prim.IsA<UsdGeomGprim>())
            {
                ClearRefineLevel(convert_index_path_to_stage_path(path));
            }
        }
    });

    SetUsdDrawModesEnabled(true);
    Populate(root_prim, SdfPathVector(), SdfPathVector());
    SetInvisedPrimPaths(SdfPathVector());

    const auto stage_id = Application::instance().get_session()->get_stage_id(m_stage);
    for (const auto& path : ViewportSceneDelegate::GetRenderIndex().GetRprimSubtree(UsdImagingDelegate::GetDelegateID()))
    {
        auto prim = m_stage->GetPrimAtPath(convert_index_path_to_stage_path(path));
        if (prim && prim.IsA<UsdGeomGprim>())
        {
            const int init_refine_level = UsdViewportRefineManager::instance().get_refine_level(stage_id, convert_index_path_to_stage_path(path));
            SetRefineLevel(path, init_refine_level);
        }
    }

    SetRefineLevelFallback(0);
    init_material_overrides();

    auto& persistent = PersistentMaterialOverride::instance();
    auto over = persistent.get_override();

    m_persistent_material_handle = over->register_callback<PrimMaterialOverride::EventType::MATERIAL>(
        [this](size_t material_id, const PrimMaterialDescriptor& descr, PrimMaterialOverride::Status status) {
            on_material_changed(material_id, descr, status, m_persistent_material_overrides, m_persistent_material_assignments);
        });
    m_persistent_assignment_handle = over->register_callback<PrimMaterialOverride::EventType::ASSIGNMENT>(
        [this](size_t material_id, const PXR_NS::SdfPath& assignment, PrimMaterialOverride::Status status) {
            on_material_assignment_changed(material_id, assignment, status, m_persistent_material_overrides, m_persistent_material_assignments);
        });
    m_persistent_material_resource_handle = over->register_callback<PrimMaterialOverride::EventType::MATERIAL_RESOURCE>(
        [this](SdfPath mat_path, const PrimMaterialDescriptor& descr, PrimMaterialOverride::Status status) {
            on_material_resource_changed(mat_path, descr, status, m_persistent_material_overrides, m_persistent_material_resource_overrides,
                                         m_persistent_material_assignments);
        });

    for (const auto& mat : over->get_materials())
    {
        on_material_changed(mat.first, mat.second, PrimMaterialOverride::Status::NEW, m_persistent_material_overrides,
                            m_persistent_material_assignments);
    }
    for (const auto& assign : over->get_assignments())
    {
        on_material_assignment_changed(assign.second, assign.first, PrimMaterialOverride::Status::NEW, m_persistent_material_overrides,
                                       m_persistent_material_assignments);
    }
    for (const auto& resource : over->get_material_resource_overrides())
    {
        on_material_resource_changed(resource.first, resource.second, PrimMaterialOverride::Status::NEW, m_persistent_material_overrides,
                                     m_persistent_material_resource_overrides, m_persistent_material_assignments);
    }
}

void ViewportUsdDelegate::prepare_batch(const ViewportHydraEngineParams& params)
{
    ApplyPendingUpdates();
    if (params.stage_resolver)
    {
        SetTime(params.stage_resolver->resolve_time(UsdImagingDelegate::GetDelegateID().GetParentPath(), params.frame.GetValue()));
    }
    else
    {
        SetTime(params.frame);
    }
}

SdfPathVector ViewportUsdDelegate::compute_exclude_paths(UsdStageRefPtr stage, const SdfPathVector& populated_paths) const
{
    if (populated_paths.empty())
        return {};

    auto range = stage->TraverseAll();

    SdfPathVector excluded_paths;
    std::unordered_set<SdfPath, SdfPath::Hash> populated_paths_set(populated_paths.begin(), populated_paths.end());
    for (auto it = range.begin(); it != range.end(); ++it)
    {
        auto prim = *it;
        const auto& prim_path = prim.GetPrimPath();

        bool should_prune = true;
        bool should_exclude = true;
        for (const auto& pop_path : populated_paths_set)
        {
            auto path_element_count = prim_path.GetCommonPrefix(pop_path).GetPathElementCount();
            if (path_element_count == pop_path.GetPathElementCount())
            {
                should_exclude = false;
                populated_paths_set.erase(pop_path);
                break;
            }
            if (should_prune && path_element_count == prim_path.GetPathElementCount())
            {
                should_prune = false;
                break;
            }
        }

        if (should_exclude && should_prune)
            excluded_paths.push_back(prim_path);
        if (should_prune)
            it.PruneChildren();
    }

    return excluded_paths;
}

void ViewportUsdDelegate::update_repr_paths(const std::unordered_set<SdfPath, SdfPath::Hash>& repr_paths)
{
    const auto mode = Application::instance().get_selection_mode();
    const auto selector = get_repr_selector_from_pick_target(mode);
    if (m_repr_paths == repr_paths && selector == m_repr_selector && (mode == Application::SelectionMode::UV && m_last_mode == mode))
        return;

    m_last_mode = mode;

    std::unordered_set<SdfPath, SdfPath::Hash> changed_paths;
    changed_paths = repr_paths;
    changed_paths.insert(m_repr_paths.begin(), m_repr_paths.end());
    m_repr_paths = repr_paths;
    m_repr_selector = selector;

    for (const auto& path : changed_paths)
    {
        // Ignore PointInstancer selection
        if (UsdGeomPointInstancer(m_stage->GetPrimAtPath(path)))
            continue;
        UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkRprimDirty(convert_stage_path_to_index_path(path), HdChangeTracker::DirtyRepr);
    }
}

#if PXR_VERSION >= 2108
SdfPath ViewportUsdDelegate::GetInstancerId(const SdfPath& primId)
{
    if (m_on_get_instancer_id)
    {
        auto result = m_on_get_instancer_id(primId);
        if (std::get<1>(result))
            return std::get<0>(result);
    }
    return UsdImagingDelegate::GetInstancerId(primId);
}

void ViewportUsdDelegate::set_instancer_id_callback(OnGetInstancerIdCallback callback)
{
    m_on_get_instancer_id = callback;
    UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkAllRprimsDirty(HdChangeTracker::DirtyInstancer);
}

void ViewportUsdDelegate::set_instancer_transform_callback(OnGetInstancerTransformCallback callback)
{
    m_on_get_instancer_transform = callback;
}

#endif

#ifdef OPENDCC_HOUDINI_SUPPORT
#if PXR_VERSION >= 2005
SdfPath ViewportUsdDelegate::GetDataSharingId(SdfPath const& primId)
{
    return SdfPath();
}
#endif
#endif

//////////////////////////////////////////////////////////////////////////
// ViewportUsdDelegate override
//////////////////////////////////////////////////////////////////////////

void ViewportUsdDelegate::on_material_changed(const size_t material_id, const PrimMaterialDescriptor& descr,
                                              const PrimMaterialOverride::Status status, OverrideMap& material_map, OverrideAssignments& assignments)
{
    auto custom_mat_path =
        UsdImagingDelegate::GetDelegateID().AppendPath(s_prefix_material_override_path.AppendChild(TfToken("m" + std::to_string(material_id))));
    auto sprim = UsdImagingDelegate::GetRenderIndex().GetSprim(HdPrimTypeTokens->material, custom_mat_path);

    if (sprim)
    {
        if (status == PrimMaterialOverride::Status::REMOVED)
        {
            UsdImagingDelegate::GetRenderIndex().RemoveSprim(HdPrimTypeTokens->material, custom_mat_path);
            material_map.erase(custom_mat_path);
            for (auto it = assignments.begin(); it != assignments.end();)
            {
                if (it->second != custom_mat_path)
                {
                    ++it;
                    continue;
                }
                if (UsdImagingDelegate::GetRenderIndex().HasRprim(it->first))
                {
                    UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkRprimDirty(
                        it->first, HdChangeTracker::RprimDirtyBits::DirtyMaterialId | HdChangeTracker::RprimDirtyBits::DirtyPrimvar);
                }

                assignments.erase(it++);
            }
        }
        else if (status == PrimMaterialOverride::Status::CHANGED || status == PrimMaterialOverride::Status::NEW)
        {
            UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkSprimDirty(custom_mat_path, HdMaterial::DirtyBits::AllDirty);
#if PXR_VERSION > 1911
            auto mat_net = descr.get_material_resource().Get<HdMaterialNetworkMap>();
            for (const auto& node : mat_net.map[UsdShadeTokens->surface].nodes)
            {
                if (node.identifier == UsdImagingTokens->UsdUVTexture)
                {
                    auto path_it = node.parameters.find(TfToken("file"));
                    if (path_it != node.parameters.end())
                    {
                        if (path_it->second.IsHolding<SdfAssetPath>())
                        {
                            const auto& asset_path = path_it->second.UncheckedGet<SdfAssetPath>();
                            UsdImagingDelegate::GetRenderIndex().GetResourceRegistry()->ReloadResource(HdResourceTypeTokens->texture,
                                                                                                       asset_path.GetAssetPath());
                        }
                    }
                }
            }
#endif
            for (const auto& assignment : assignments)
            {
                if (assignment.second == custom_mat_path && UsdImagingDelegate::GetRenderIndex().HasRprim(assignment.first))
                {
                    UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkRprimDirty(assignment.first,
                                                                                           HdChangeTracker::RprimDirtyBits::DirtyPrimvar);
                }
            }
            material_map[custom_mat_path] = descr;
        }
        else
        {
            OPENDCC_ERROR("Inconsistent prim material override.");
        }
    }
    else if (status == PrimMaterialOverride::Status::NEW)
    {
        UsdImagingDelegate::GetRenderIndex().InsertSprim(HdPrimTypeTokens->material, static_cast<UsdImagingDelegate*>(this), custom_mat_path);
        material_map[custom_mat_path] = descr;
    }
}

void ViewportUsdDelegate::on_material_assignment_changed(const size_t material_id, const PXR_NS::SdfPath& assignment,
                                                         const PrimMaterialOverride::Status status, OverrideMap& material_map,
                                                         OverrideAssignments& assignments)
{
    auto converted_path = convert_stage_path_to_index_path(assignment);
    auto custom_mat_path =
        UsdImagingDelegate::GetDelegateID().AppendPath(s_prefix_material_override_path.AppendChild(TfToken("m" + std::to_string(material_id))));

    if (material_map.find(custom_mat_path) == material_map.end())
    {
        return;
    }

    if (status == PrimMaterialOverride::Status::NEW)
    {
        assignments[converted_path] = custom_mat_path;
    }
    else if (status == PrimMaterialOverride::Status::REMOVED)
    {
        assignments.erase(converted_path);
    }

    if (UsdImagingDelegate::GetRenderIndex().HasRprim(converted_path))
    {
        UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkRprimDirty(converted_path, HdChangeTracker::RprimDirtyBits::DirtyMaterialId |
                                                                                                   HdChangeTracker::RprimDirtyBits::DirtyPrimvar);
    }
}

void OPENDCC_NAMESPACE::ViewportUsdDelegate::on_material_resource_changed(const PXR_NS::SdfPath& mat_path, const PrimMaterialDescriptor& descr,
                                                                          const PrimMaterialOverride::Status status, OverrideMap& material_map,
                                                                          OverrideMap& resource_map, OverrideAssignments& assignments)
{
    const auto index_mat_path = convert_stage_path_to_index_path(mat_path);
    auto sprim = UsdImagingDelegate::GetRenderIndex().GetSprim(HdPrimTypeTokens->material, index_mat_path);
    if (!sprim && (status == PrimMaterialOverride::Status::NEW || status == PrimMaterialOverride::Status::CHANGED))
    {
        material_map[index_mat_path] = descr;
        return;
    }

    if (!sprim)
    {
        return;
    }

    if (status == PrimMaterialOverride::Status::REMOVED)
    {
        resource_map.erase(index_mat_path);
        UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkSprimDirty(index_mat_path, HdMaterial::DirtyBits::AllDirty);
    }
    else if (status == PrimMaterialOverride::Status::CHANGED || status == PrimMaterialOverride::Status::NEW)
    {
        UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkSprimDirty(index_mat_path, HdMaterial::DirtyBits::AllDirty);
        for (const auto& assignment : assignments)
        {
            if (assignment.second == index_mat_path && UsdImagingDelegate::GetRenderIndex().HasRprim(assignment.first))
            {
                UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkRprimDirty(assignment.first,
                                                                                       HdChangeTracker::RprimDirtyBits::DirtyPrimvar);
            }
        }
        resource_map[index_mat_path] = descr;
    }
    else
    {
        OPENDCC_ERROR("Inconsistent prim material override.");
    }
}

void ViewportUsdDelegate::clear_tool_overrides()
{
    for (const auto& mat : m_tool_material_overrides)
    {
        UsdImagingDelegate::GetRenderIndex().RemoveSprim(HdPrimTypeTokens->material, mat.first);
    }

    for (const auto& assignment : m_tool_material_assignments)
    {
        if (UsdImagingDelegate::GetRenderIndex().HasRprim(assignment.first))
        {
            UsdImagingDelegate::GetRenderIndex().GetChangeTracker().MarkRprimDirty(
                assignment.first, HdChangeTracker::RprimDirtyBits::DirtyMaterialId | HdChangeTracker::RprimDirtyBits::DirtyPrimvar);
        }
    }

    m_tool_material_overrides.clear();
    m_tool_material_resource_overrides.clear();
    m_tool_material_assignments.clear();
}

OPENDCC_NAMESPACE_CLOSE
