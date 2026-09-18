/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include "opendcc/app/core/application.h"
#include <pxr/pxr.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleMaterial.h>
#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/base/tf/type.h>
#if PXR_VERSION >= 2005
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgiGL/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/imaging/hdx/fullscreenShader.h>
#else
#include <pxr/imaging/hdx/compositor.h>
#endif
#if PXR_VERSION >= 2108
#include <pxr/imaging/cameraUtil/framing.h>
#endif
#include <pxr/imaging/glf/drawTarget.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include "opendcc/app/viewport/viewport_scene_delegate.h"
#include "opendcc/app/core/rich_selection.h"
#include "opendcc/app/viewport/visibility_mask.h"
#include "opendcc/usd_editor/scene_indices/prune_scene_index.h"
#include "opendcc/app/viewport/scene_indices/hydra_engine_scene_indices_notifier.h"

#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginHandle.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class HdRenderIndex;
class HdRendererPlugin;
class HdxTaskController;
class UsdImagingDelegate;
class HDSceneDelegate;
struct HdxRenderTaskParams;
class HdxFullscreenShader;
class HgiInterop;

PXR_NAMESPACE_CLOSE_SCOPE

OPENDCC_NAMESPACE_OPEN
class IStageResolver;

enum class ViewportHydraDisplayPurpose
{
    DISPLAY_PURPOSE_DEFAULT,
    DISPLAY_PURPOSE_GUIDE,
    DISPLAY_PURPOSE_PROXY,
    DISPLAY_PURPOSE_RENDER,
    DISPLAY_PURPOSE_COUNT
};

enum ViewportHydraDrawMode
{
    DRAW_POINTS = 1,
    DRAW_WIREFRAME = 1 << 1,
    DRAW_WIREFRAME_ON_SURFACE = 1 << 2,
    DRAW_SHADED_FLAT = 1 << 3,
    DRAW_SHADED_SMOOTH = 1 << 4,
    DRAW_GEOM_ONLY = 1 << 5,
    DRAW_GEOM_FLAT = 1 << 6,
    DRAW_GEOM_SMOOTH = 1 << 7
};
using ViewportHydraDrawModeMask = uint32_t;

enum class ViewportHydraCullStyle
{
    CULL_STYLE_NO_OPINION,
    CULL_STYLE_NOTHING,
    CULL_STYLE_BACK,
    CULL_STYLE_FRONT,
    CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED,
    CULL_STYLE_COUNT
};

struct ViewportHydraEngineParams
{
    PXR_NS::UsdTimeCode frame = PXR_NS::UsdTimeCode::Default();
    bool show_guides = false;
    bool show_proxy = true;
    bool show_render = false;
    bool show_locators = true;
    bool enable_id_render = false;
    bool enable_lighting = true;
    bool enable_shadows = false;
    bool enable_sample_alpha_to_coverage = false;
    bool apply_render_state = true;
    bool gamma_correct_colors = true;
    bool highlight = false;
    VisibilityMask visibility_mask;
    ViewportHydraDrawModeMask draw_mode = ViewportHydraDrawMode::DRAW_SHADED_SMOOTH;
    ViewportHydraCullStyle cull_style = ViewportHydraCullStyle::CULL_STYLE_NOTHING;
    PXR_NS::SdfPathVector populated_paths;
    std::unordered_set<PXR_NS::SdfPath, PXR_NS::SdfPath::Hash> invised_paths;
    std::unordered_set<PXR_NS::SdfPath, PXR_NS::SdfPath::Hash> repr_paths;
    std::vector<PXR_NS::GfVec4d> clip_planes;
    PXR_NS::GfRect2i crop_region;
    bool enable_scene_materials = false;
    PXR_NS::TfToken color_correction_mode;
    std::string view_OCIO;
    std::string input_color_space;
    float gamma = 1.0f;
    float exposure = 0.0f;
    double stage_meters_per_unit = 0.01f;
    PXR_NS::GfVec2i render_resolution = PXR_NS::GfVec2i(100, 100);
    bool use_camera_light = true;
    bool is_hd_st = true;
    bool invised_paths_dirty = true;
    std::weak_ptr<PXR_NS::HdRenderIndex> main_render_index;
    std::shared_ptr<IStageResolver> stage_resolver;
    PXR_NS::SdfPath current_stage_root = PXR_NS::SdfPath::AbsoluteRootPath();
    std::unordered_map<std::string, PXR_NS::VtValue> user_data;
    PXR_NS::HdCompareFunction depth_func = PXR_NS::HdCompareFunction::HdCmpFuncLess;
    PXR_NS::GfVec4f point_color = { 0.0f, 0.0f, 0.0f, 1.0f };

    PXR_NS::UsdStageRefPtr resolve_stage(const PXR_NS::SdfPath& delegate_id) const;
};

struct ViewportHydraIntersectionParams
{
    ViewportHydraEngineParams engine_params;
    PXR_NS::GfMatrix4d view_matrix;
    PXR_NS::GfMatrix4d proj_matrix;
    bool use_custom_collection = false;
    PXR_NS::HdRprimCollection collection;
    bool use_custom_render_tags = false;
    PXR_NS::TfTokenVector render_tags;
    PXR_NS::GfVec2i resolution;
    PXR_NS::TfToken resolve_mode = PXR_NS::HdxPickTokens->resolveUnique;
    SelectionList::SelectionMask pick_target = SelectionList::SelectionFlags::FULL_SELECTION;
};

class HydraRenderSettings;

class OPENDCC_API ViewportHydraEngine
{
public:
    ViewportHydraEngine(const std::unordered_set<PXR_NS::TfType, PXR_NS::TfHash>& delegate_types);
    ViewportHydraEngine(const std::shared_ptr<SceneIndexManager>& si_manager);
    virtual ~ViewportHydraEngine();

    void render(const ViewportHydraEngineParams& params);
    void set_camera_state(const PXR_NS::GfMatrix4d& view_matrix, const PXR_NS::GfMatrix4d& projection_matrix);
    void set_render_viewport(const PXR_NS::GfVec4d& viewport);
    void set_lighting_state(PXR_NS::GlfSimpleLightVector const& lights, PXR_NS::GlfSimpleMaterial const& material,
                            PXR_NS::GfVec4f const& sceneAmbient);

    static PXR_NS::TfTokenVector get_render_plugins();
    static std::string get_render_display_name(PXR_NS::TfToken const& id);
    static PXR_NS::TfToken get_render_plugin_id(PXR_NS::TfToken const& id);
    static PXR_NS::TfToken get_default_render_plugin();
    PXR_NS::TfToken get_current_render_id() const;
    bool set_renderer_plugin(PXR_NS::TfToken const& id);
    void set_selected(const SelectionList& selection_state, const RichSelection& rich_selection = RichSelection());
    void set_rollover_prims(const PXR_NS::SdfPathVector& rollover_prims);
    void set_selection_color(PXR_NS::GfVec4f const& color);
    bool is_converged() const;

    void update_init(ViewportHydraEngineParams& engine_params);
    void update_delegates(const ViewportHydraEngineParams& engine_params);
    void update_scene_indices(const ViewportHydraEngineParams& engine_params);

    PXR_NS::GfRange3d get_bbox(const PXR_NS::SdfPath& path);
    bool is_hd_st() const;
    PXR_NS::HdRenderSettingDescriptorList get_render_setting_descriptors() const;
    PXR_NS::VtValue get_render_setting(const PXR_NS::TfToken& key) const;
    void set_render_setting(const PXR_NS::TfToken& key, const PXR_NS::VtValue& value);
    void set_scene_delegates(const std::unordered_set<PXR_NS::TfType, PXR_NS::TfHash>& delegate_types);
    void set_scene_index_manager(const std::shared_ptr<SceneIndexManager>& si_manager);

    bool test_intersection_batch(const ViewportHydraIntersectionParams& params, PXR_NS::HdxPickHitVector& out_hits);

    void resume();
    void pause();
    bool is_pause_supported() const;
    void stop();
    bool is_stop_supported() const;
    void reset();
#if PXR_VERSION < 2005
    PXR_NS::SdfPath get_prim_path_from_instance_index(PXR_NS::SdfPath const& protoRprimPath, int instanceIndex, int* absoluteInstanceIndex = NULL,
                                                      PXR_NS::SdfPath* rprimPath = NULL, PXR_NS::SdfPathVector* instanceContext = NULL);
#else
    PXR_NS::SdfPath get_prim_path_from_instance_index(const PXR_NS::SdfPath& rprimId, int instanceIndex,
                                                      PXR_NS::HdInstancerContext* instancer_context = nullptr);
    void set_render_settings(std::shared_ptr<HydraRenderSettings> render_settings);
    std::shared_ptr<HydraRenderSettings> get_render_settings() const;
    PXR_NS::HdRenderBuffer* get_aov_texture(const PXR_NS::TfToken& aov) const;
    void set_renderer_aov(const PXR_NS::TfToken& aov_name);
    bool has_aov(const PXR_NS::TfToken& aov_name) const;
    PXR_NS::TfToken get_current_aov() const;
    PXR_NS::TfTokenVector get_renderer_aovs() const;
    static PXR_NS::Hgi* get_hgi();
#endif
    bool is_valid() const;
    std::weak_ptr<PXR_NS::HdRenderIndex> get_render_index() { return m_render_index; }
    void restart();
#if PXR_VERSION >= 2108
    void set_framing(const PXR_NS::CameraUtilFraming& framing);
#ifdef OPENDCC_USE_HYDRA_FRAMING_API
    void set_render_buffer_size(const PXR_NS::GfVec2i& size);
    void set_override_window_policy(const std::pair<bool, PXR_NS::CameraUtilConformWindowPolicy>& policy);
#endif
#endif

#if PXR_VERSION >= 2005
    void update_render_settings();
#endif

private:
    ViewportHydraEngine();

    void init_hydra_resources();

    void init_scene_resources();

    PXR_NS::HdxRenderTaskParams make_viewport_hydra_render_params(const ViewportHydraEngineParams& params);
    void execute(const ViewportHydraEngineParams& params, PXR_NS::HdTaskSharedPtrVector tasks);
#if PXR_VERSION >= 2005
    void initialize_aovs();
    void compose_aovs();
    void update_aov_texture(PXR_NS::HdRenderBuffer* buffer, PXR_NS::HgiTextureHandle& handle, PXR_NS::HgiTextureHandle& persistent_buffer) const;
    bool use_aovs() const;
    void init_depth_compositor();
#endif
    static void compute_render_tags(ViewportHydraEngineParams const& params, PXR_NS::TfTokenVector* renderTags);
    bool update_hydra_collection(PXR_NS::HdRprimCollection* collection, PXR_NS::SdfPathVector const& roots, ViewportHydraEngineParams const& params);

    void delete_hydra_resources(bool clean_render_plugin = false);
    PXR_NS::HdRenderSettingsMap read_render_settings(const PXR_NS::HdRenderDelegate* render_delegate) const;
    bool prune_scene_index_predicate(const PXR_NS::SdfPathVector& populated_paths, const PXR_NS::SdfPath& path) const;

    bool use_hydra2() const;

    PXR_NS::HdEngine m_engine;
    std::shared_ptr<PXR_NS::HdRenderIndex> m_render_index;
    PXR_NS::HdxSelectionTrackerSharedPtr m_sel_tracker;
    PXR_NS::HdRprimCollection m_render_collection;
    PXR_NS::HdRprimCollection m_intersect_collection;

    std::unordered_set<PXR_NS::TfType, PXR_NS::TfHash> m_scene_delegate_types;
    std::unordered_map<PXR_NS::SdfPath, std::unordered_set<ViewportSceneDelegateSPtr>, PXR_NS::SdfPath::Hash> m_scene_delegates;
    PXR_NS::SdfPathVector m_root_prefixes;
    PXR_NS::TfToken m_renderer_id;
    PXR_NS::HdxTaskController* m_task_controller;

    PXR_NS::SdfPath m_current_root;

    PXR_NS::GfVec4d m_viewport;
    PXR_NS::TfTokenVector m_render_tags;
    PXR_NS::GfVec4f m_selection_color;

    PXR_NS::GfVec4i m_restore_viewport;
    PXR_NS::SdfPathVector m_populated_paths;
    PXR_NS::GfMatrix4d m_view_mat;
    PXR_NS::GfMatrix4d m_proj_mat;

    std::mutex m_mutex;
    bool m_dirty_selection = true;
    SelectionList m_selection_list;
    SelectionList m_rollover_list;
    RichSelection m_rich_selection;
#if PXR_VERSION >= 2005
    static std::unique_ptr<PXR_NS::Hgi> m_hgi;
    // One framebuffer arena per GL context, as hgiGL/hgi.h requires. One context per widget,
    // so one arena per engine.
    // Held rather than built per frame: HgiInterop owns a GL program and VAO, so a function-local
    // rebuilt and destroyed them on every composited frame. Destroyed with the engine, which the
    // widget tears down with its context current.
    std::unique_ptr<PXR_NS::HgiInterop> m_interop;
    PXR_NS::HgiGLContextArenaHandle m_gl_arena;
    void _bind_context_arena();
    PXR_NS::HdDriver m_render_driver;
    PXR_NS::HgiTextureHandle m_color_texture;
    PXR_NS::HgiTextureHandle m_intermediate_depth_texture;
    std::shared_ptr<HydraRenderSettings> m_render_settings;
    PXR_NS::TfToken m_viewport_output;
#if PXR_VERSION >= 2108
    PXR_NS::HgiTextureHandle m_depth_texture;
    std::unique_ptr<PXR_NS::HdxFullscreenShader> m_depth_compositor;
    PXR_NS::CameraUtilFraming m_framing;
#endif
    static uint32_t s_engine_count;
#endif
    PXR_NS::HdPluginRenderDelegateUniqueHandle m_render_delegate;
    HydraEngineSceneIndicesNotifier::Handle m_prune_cid;
    PXR_NS::TfRefPtr<PruneSceneIndex> m_prune_si;
    std::shared_ptr<SceneIndexManager> m_si_manager;
};

OPENDCC_NAMESPACE_CLOSE
