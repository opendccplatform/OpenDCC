// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/pxr.h>
#if PXR_VERSION >= 2005
#include <pxr/imaging/hgiInterop/hgiInterop.h>
#include <pxr/imaging/hdx/hgiConversions.h>
#endif
#if PXR_VERSION < 2108
#include <pxr/imaging/glf/glew.h>
#else
#include <pxr/imaging/garch/glApi.h>
#endif

#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hdx/taskController.h>
#include <pxr/imaging/hdx/simpleLightTask.h>

#include <pxr/imaging/glf/info.h>
#include <pxr/imaging/glf/glContext.h>
#include <pxr/imaging/glf/diagnostic.h>
#include <pxr/imaging/glf/contextCaps.h>

#include <pxr/imaging/hgi/texture.h>
#if PXR_VERSION >= 2005
#include <pxr/imaging/hgi/blitCmdsOps.h>
#endif

#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/plug/plugin.h>
#include <pxr/base/plug/registry.h>

#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

#include "opendcc/app/core/session.h"
#include "opendcc/app/core/settings.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/viewport/istage_resolver.h"
#include "opendcc/app/viewport/hd_selection_ext.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/app/viewport/hydra_render_settings.h"
#include "opendcc/app/viewport/viewport_usd_delegate.h"
#include "opendcc/app/viewport/viewport_engine_proxy.h"

#include <algorithm>

namespace std
{
    size_t hash_value(const PXR_NS::VtArray<PXR_NS::HdAovSettingsMap>& map_array)
    {
        size_t result = 0;

        for (const auto& map : map_array)
        {
            for (const auto& elem : map)
            {
                result += elem.first.Hash() + elem.second.GetHash();
            }
        }

        return result;
    }
}

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(DepthStyleTokens, ((OpenGL, "OpenGL"))((linear, "Linear"))((NDC, "NDC")));

namespace
{
#if PXR_VERSION >= 2108
    TfToken get_depth_compositor_shader()
    {
        static TfToken result = [] {
            const auto core_plug = PlugRegistry::GetInstance().GetPluginWithName("opendcc_core");
            return TfToken(PlugFindPluginResource(core_plug, "shaders/depth_compositor.glslfx"));
        }();
        return result;
    }
#endif
};

#if PXR_VERSION >= 2008
uint32_t ViewportHydraEngine::s_engine_count = 0;

#endif

static SdfPath get_stage_prefix(const SdfPath& delegate_id)
{
    const auto prefixes = delegate_id.GetPrefixes();
    return prefixes.empty() ? SdfPath::AbsoluteRootPath() : prefixes[0];
}

static TfToken get_pick_target(SelectionList::SelectionMask pick_target)
{
    using SelectionFlags = SelectionList::SelectionFlags;
    switch (pick_target)
    {
    case SelectionFlags::POINTS:
        return HdxPickTokens->pickPoints;
    case SelectionFlags::EDGES:
        return HdxPickTokens->pickEdges;
    case SelectionFlags::ELEMENTS:
        return HdxPickTokens->pickFaces;
    default:
        return HdxPickTokens->pickPrimsAndInstances;
    }
}

ViewportHydraEngine::ViewportHydraEngine()
    : m_render_index(nullptr)
    , m_sel_tracker(new HdxSelectionTracker)
    , m_task_controller(nullptr)
    , m_selection_color(Application::instance().get_settings()->get("viewport.selection_color", GfVec4f(1, 1, 0, 0.5)))
    , m_viewport(0.0f, 0.0f, 512.0f, 512.0f)
    , m_current_root(SdfPath::AbsoluteRootPath())
    , m_root_prefixes({ SdfPath::AbsoluteRootPath() })
    , m_restore_viewport(0)
{
    static std::once_flag initFlag;

    std::call_once(initFlag, [] {
    // Initialize Glew library for GL Extensions if needed
#if PXR_VERSION < 2108
        GlfGlewInit();
#else
        GarchGLApiLoad();
#endif
        // Initialize if needed and switch to shared GL context.
        GlfSharedGLContextScopeHolder sharedContext;

        // Initialize GL context caps based on shared context
        GlfContextCaps::InitInstance();
    });
#if PXR_VERSION >= 2008
    if (s_engine_count++ == 0)
        m_hgi = Hgi::CreatePlatformDefaultHgi();
#endif
    m_view_mat.SetLookAt(GfVec3d(0), GfVec3d(-1, 0, 0), GfVec3d::YAxis());
    m_proj_mat.SetIdentity();
}

ViewportHydraEngine::ViewportHydraEngine(const std::unordered_set<TfType, TfHash>& delegate_types)
    : ViewportHydraEngine()
{
    m_scene_delegate_types = delegate_types;
}

ViewportHydraEngine::ViewportHydraEngine(const std::shared_ptr<SceneIndexManager>& si_manager)
    : ViewportHydraEngine()
{
    m_si_manager = si_manager;
}

ViewportHydraEngine::~ViewportHydraEngine()
{
    delete_hydra_resources();
#if PXR_VERSION >= 2008
    if (--s_engine_count == 0)
        m_hgi = nullptr;
#endif
    if (m_prune_cid)
    {
        HydraEngineSceneIndicesNotifier::unregister_index_created(HydraEngineSceneIndicesNotifier::IndexType::Prune, m_prune_cid);
    }
}
void ViewportHydraEngine::delete_hydra_resources(bool clean_render_plugin)
{
    m_scene_delegates.clear();
    m_engine = HdEngine();
#if PXR_VERSION >= 2005
#if PXR_VERSION >= 2108
    if (m_depth_texture)
        m_hgi->DestroyTexture(&m_depth_texture);
    m_depth_compositor.reset();
#endif
    if (m_color_texture)
        m_hgi->DestroyTexture(&m_color_texture);
    if (m_intermediate_depth_texture)
        m_hgi->DestroyTexture(&m_intermediate_depth_texture);
#endif
    if (m_task_controller)
    {
        delete m_task_controller;
        m_task_controller = nullptr;
    }
    m_prune_cid.reset();
    m_render_index = nullptr;
    m_render_delegate = nullptr;
    if (clean_render_plugin)
    {
        m_renderer_id = TfToken();
    }
}

HdRenderSettingsMap ViewportHydraEngine::read_render_settings(const HdRenderDelegate* render_delegate) const
{
    if (!render_delegate)
        return {};

    auto render_name = get_render_display_name(m_renderer_id);
    auto settings = Application::instance().get_settings();
    const auto settings_key = "viewport." + render_name;
    if (!settings->has(settings_key))
        return {};

    HdRenderSettingsMap result;
    auto descriptors = render_delegate->GetRenderSettingDescriptors();
    for (const auto& descriptor : descriptors)
    {
        const auto setting_path = settings_key + "." + descriptor.key.GetString();
        auto setting_json = settings->get_raw(setting_path);
        if (setting_json.empty())
            continue;

        VtValue vt_val;
        if (setting_json.isBool())
            vt_val = setting_json.asBool();
        else if (setting_json.isInt())
            vt_val = setting_json.asInt();
        else if (setting_json.isDouble())
            vt_val = setting_json.asDouble();
        else if (setting_json.isString())
            vt_val = setting_json.asString();
        if (vt_val.CanCastToTypeOf(descriptor.defaultValue))
        {
            vt_val.CastToTypeOf(descriptor.defaultValue);
            result[descriptor.key] = vt_val;
        }
        else
        {
            TF_WARN("The render setting '%s' of the render delegate '%s' has an incorrect type. Expected '%s', got '%s'.", descriptor.key.GetText(),
                    m_renderer_id.GetText(), descriptor.defaultValue.GetTypeName().c_str(), vt_val.GetTypeName().c_str());
        }
    }
    result[TfToken("stageMetersPerUnit")] = settings->get(settings_key + ".stageMetersPerUnit", 0.01);

    return result;
}

bool ViewportHydraEngine::prune_scene_index_predicate(const SdfPathVector& populated_paths, const SdfPath& path) const
{
    if (!m_task_controller)
    {
        return false;
    }
    if (populated_paths.empty() || path.HasPrefix(m_task_controller->GetControllerId()))
    {
        return false;
    }

    SdfPath usd_path;
    if (path.GetPathElementCount() > 1)
    {
        SdfPathVector prefixes;
        path.GetPrefixes(&prefixes);
        if (m_root_prefixes.size() == 1 && m_root_prefixes[0] == SdfPath::AbsoluteRootPath())
        {
            usd_path = path.ReplacePrefix(prefixes[0], SdfPath::AbsoluteRootPath());
        }
        else
        {
            usd_path = path.ReplacePrefix(prefixes[1], SdfPath::AbsoluteRootPath());
        }
    }
    else
    {
        usd_path = path;
    }

    for (const auto& populated : populated_paths)
    {
        if (usd_path.HasPrefix(populated) || populated.HasPrefix(usd_path))
        {
            return false;
        }
    }

    return true;
}

bool ViewportHydraEngine::use_hydra2() const
{
    return m_si_manager != nullptr;
}

#if PXR_VERSION >= 2008
std::unique_ptr<Hgi> ViewportHydraEngine::m_hgi;

Hgi* ViewportHydraEngine::get_hgi()
{
    return m_hgi.get();
}

#endif

void ViewportHydraEngine::render(const ViewportHydraEngineParams& params)
{
    if (!is_valid())
        return;
    SdfPathVector roots;
    if (params.stage_resolver)
        roots = params.stage_resolver->get_stage_roots(params.frame);
    else
        roots.push_back(SdfPath::AbsoluteRootPath());

    update_hydra_collection(&m_render_collection, roots, params);
    m_task_controller->SetCollection(m_render_collection);
    m_task_controller->SetFreeCameraClipPlanes(params.clip_planes);

    TfTokenVector render_tags;
    compute_render_tags(params, &render_tags);
    m_task_controller->SetRenderTags(render_tags);

    HdxRenderTaskParams hd_params = make_viewport_hydra_render_params(params);
    hd_params.depthFunc = HdCompareFunction::HdCmpFuncLEqual;
    hd_params.pointColor = params.point_color;
    m_task_controller->SetRenderParams(hd_params);
    m_task_controller->SetEnableSelection(params.highlight);

    VtValue selection_value(m_sel_tracker);
    m_engine.SetTaskContextData(HdxTokens->selectionState, selection_value);
    execute(params, m_task_controller->GetRenderingTasks());
}

void ViewportHydraEngine::set_camera_state(const GfMatrix4d& view_matrix, const GfMatrix4d& projection_matrix)
{
    if (!is_valid())
        return;
#if PXR_VERSION >= 2108 && !defined(OPENDCC_USE_HYDRA_FRAMING_API)
    GfMatrix4d proj;
    if (m_framing.IsValid())
    {
        proj = m_framing.ApplyToProjectionMatrix(projection_matrix, CameraUtilDontConform);
    }
    else
    {
        proj = projection_matrix;
    }
#else
    auto proj = projection_matrix;
#endif
    if (m_view_mat != view_matrix || m_proj_mat != proj)
    {
        m_task_controller->SetFreeCameraMatrices(view_matrix, proj);
        m_render_index->GetRenderDelegate()->Resume();
        m_view_mat = view_matrix;
        m_proj_mat = proj;
    }
}

void ViewportHydraEngine::set_render_viewport(const GfVec4d& viewport)
{
    if (!is_valid())
        return;
    m_task_controller->SetRenderViewport(viewport);
}

void ViewportHydraEngine::set_lighting_state(GlfSimpleLightVector const& lights, GlfSimpleMaterial const& material, GfVec4f const& sceneAmbient)
{
    if (!is_valid())
        return;
    auto lighting_context = GlfSimpleLightingContext::New();
    lighting_context->SetLights(lights);
    lighting_context->SetMaterial(material);
    lighting_context->SetSceneAmbient(sceneAmbient);
    lighting_context->SetUseLighting(!lights.empty());
    lighting_context->SetCamera(m_view_mat, m_proj_mat);

    m_task_controller->SetLightingState(lighting_context);
}
#if PXR_VERSION >= 2005
#if PXR_VERSION >= 2108
void ViewportHydraEngine::init_depth_compositor()
{
    const auto& renderer_info = ViewportEngineProxy::get_renderer_info(m_renderer_id);
    const auto depth_style = renderer_info.depth_style;
    if (depth_style == DepthStyle::OpenGL)
        return;

    m_depth_compositor = std::make_unique<HdxFullscreenShader>(m_hgi.get(), "ViewportDepthCompositor");

    TfToken depth_technique;
    if (depth_style == DepthStyle::Linear)
        depth_technique = DepthStyleTokens->linear;
    else if (depth_style == DepthStyle::NDC)
        depth_technique = DepthStyleTokens->NDC;
    else
        depth_technique = DepthStyleTokens->OpenGL;

    HgiShaderFunctionDesc frag_descr;
    frag_descr.debugName = "depth_compositor";
    frag_descr.shaderStage = HgiShaderStageFragment;
    HgiShaderFunctionAddStageInput(&frag_descr, "uvOut", "vec2");
    HgiShaderFunctionAddConstantParam(&frag_descr, "proj_mat", "mat4");
    HgiShaderFunctionAddConstantParam(&frag_descr, "near_far", "vec2");
    HgiShaderFunctionAddConstantParam(&frag_descr, "stage_meters_per_unit", "float");
    HgiShaderFunctionAddStageOutput(&frag_descr, "depthOut", "float", "depth(any)");

#if PXR_VERSION < 2405
    HgiShaderFunctionAddTexture(&frag_descr, "depthIn", 2, HgiFormatFloat32);

    HgiShaderFunctionDesc vert_descr;
    vert_descr.shaderStage = HgiShaderStageVertex;

    HgiShaderFunctionAddStageInput(&vert_descr, "position", "vec4", "position");
    HgiShaderFunctionAddStageInput(&vert_descr, "uvIn", "vec2");
    HgiShaderFunctionAddStageOutput(&vert_descr, "gl_Position", "vec4", "position");
    HgiShaderFunctionAddStageOutput(&vert_descr, "uvOut", "vec2");

    m_depth_compositor->SetProgram(get_depth_compositor_shader(), depth_technique, frag_descr, vert_descr);
#else
    HgiShaderFunctionAddTexture(&frag_descr, "depthIn", 0, 2, HgiFormatFloat32);
    m_depth_compositor->SetProgram(get_depth_compositor_shader(), depth_technique, frag_descr);
#endif
}

#endif

void ViewportHydraEngine::set_renderer_aov(const TfToken& aov_name)
{
    TF_VERIFY(m_render_index);
    if (!m_render_index->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
        return;

    m_viewport_output = aov_name;
    m_task_controller->SetViewportRenderOutput(m_viewport_output);
}

bool ViewportHydraEngine::has_aov(const TfToken& aov_name) const
{
    if (!is_valid())
        return false;
    if (!m_render_index->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
        return false;

    auto aovs = get_renderer_aovs();
    return std::any_of(aovs.begin(), aovs.end(), [aov_name](const TfToken& token) { return token == aov_name; });
}

TfToken ViewportHydraEngine::get_current_aov() const
{
    return m_viewport_output;
}

TfTokenVector ViewportHydraEngine::get_renderer_aovs() const
{
    if (!is_valid())
        return {};
    if (!m_render_index->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer))
        return {};

    TfTokenVector result;
    if (use_aovs())
    {
        const auto& aovs = m_render_settings->get_aovs();
        result.resize(aovs.size());
        std::transform(aovs.begin(), aovs.end(), result.begin(), [](const HydraRenderSettings::AOV& aov) { return aov.name; });
    }
    else
    {
        auto render_delegate = m_render_index->GetRenderDelegate();
        static const auto candidates = { HdAovTokens->primId, HdAovTokens->color, HdAovTokens->depth, HdAovTokens->normal,
                                         HdAovTokensMakePrimvar(TfToken("st")) };
        for (const auto& aov : candidates)
        {
            if (render_delegate->GetDefaultAovDescriptor(aov).format != HdFormatInvalid)
                result.push_back(aov);
        }
    }

    static const TfTokenVector aov_block_list = { HdAovTokens->pointId, HdAovTokens->edgeId, HdAovTokens->elementId, HdAovTokens->instanceId,
                                                  HdAovTokens->primId };
    result.erase(
        std::remove_if(result.begin(), result.end(),
                       [](const TfToken& token) { return std::find(aov_block_list.begin(), aov_block_list.end(), token) != aov_block_list.end(); }),
        result.end());
    return result;
}

void ViewportHydraEngine::initialize_aovs()
{
    if (!use_aovs())
    {
        m_task_controller->SetRenderOutputs(get_renderer_aovs());
        m_viewport_output = HdAovTokens->color;
        return;
    }

    auto aovs = m_render_settings->get_aovs();
    TfTokenVector render_outputs(aovs.size());
    bool has_depth = false;
    bool has_color = false;
    std::transform(aovs.begin(), aovs.end(), render_outputs.begin(), [&has_depth, &has_color](const HydraRenderSettings::AOV& aov) {
        if (aov.name == HdAovTokens->depth)
            has_depth = true;
        if (aov.name == HdAovTokens->color)
            has_color = true;
        return aov.name;
    });
    if (!has_depth)
        render_outputs.push_back(HdAovTokens->depth);
    if (!has_color)
        render_outputs.push_back(HdAovTokens->color);
    m_task_controller->SetRenderOutputs(render_outputs);

    for (auto& aov : aovs)
    {
        m_task_controller->SetRenderOutputSettings(aov.name, aov.descriptor);
    }

    if (!has_aov(m_viewport_output))
    {
        auto aov_tokens = get_renderer_aovs();
        m_viewport_output = aovs.empty() ? TfToken() : aov_tokens[0];
    }
    m_task_controller->SetViewportRenderOutput(m_viewport_output);
}

bool ViewportHydraEngine::use_aovs() const
{
    return m_render_settings && !m_render_settings->get_aovs().empty() && m_renderer_id != "HdStormRendererPlugin";
}
void ViewportHydraEngine::compose_aovs()
{
    GLint app_draw_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &app_draw_fbo);
    HgiTextureHandle color_handle, intermediate_depth_handle, depth_handle;
    VtValue color;
    if (!m_engine.GetTaskContextData(HdAovTokens->color, &color))
    {
        auto color_output = m_task_controller->GetRenderOutput(HdAovTokens->color);
        if (!color_output)
            return;
        color_output->Resolve();
        update_aov_texture(color_output, color_handle, m_color_texture);
    }
    else
    {
        color_handle = color.Get<HgiTextureHandle>();
    }

    auto depth_output = m_task_controller->GetRenderOutput(HdAovTokens->depth);
    if (depth_output)
    {
        depth_output->Resolve();
        update_aov_texture(depth_output, intermediate_depth_handle, m_intermediate_depth_texture);
#if PXR_VERSION >= 2108
        if (ViewportEngineProxy::get_renderer_info(m_renderer_id).depth_style != DepthStyle::OpenGL)
        {
            if (!m_depth_texture)
                m_depth_texture = m_hgi->CreateTexture(intermediate_depth_handle->GetDescriptor());

            struct Uniform
            {
                GfMatrix4f proj_mat;
                GfVec2f near_far;
                float stage_meters_per_unit;
            } uniform;
            uniform.proj_mat = GfMatrix4f(m_proj_mat);
            if (uniform.proj_mat[3][3] == 0.0) // is perspective
                uniform.near_far = GfVec2f(m_proj_mat[3][2] / (m_proj_mat[2][2] - 1), m_proj_mat[3][2] / (1 + m_proj_mat[2][2]));
            else
                uniform.near_far = GfVec2f((m_proj_mat[3][2] + 1) / m_proj_mat[2][2], (m_proj_mat[3][2] - 1) / m_proj_mat[2][2]);
            uniform.stage_meters_per_unit = m_render_index->GetRenderDelegate()->GetRenderSetting(TfToken("stageMetersPerUnit"), 0.01);

            m_depth_compositor->SetShaderConstants(sizeof(Uniform), &uniform);
#if PXR_VERSION < 2405
            m_depth_compositor->BindTextures({ HdAovTokens->depth }, { intermediate_depth_handle });
#else
            m_depth_compositor->BindTextures({ intermediate_depth_handle });
#endif
            m_depth_compositor->Draw(m_depth_texture, HgiTextureHandle());
            depth_handle = m_depth_texture;
        }
        else
        {
            depth_handle = intermediate_depth_handle;
        }
#else
        depth_handle = intermediate_depth_handle;
#endif
    }

    HgiInterop interop;
#if PXR_VERSION < 2108
    interop.TransferToApp(m_hgi.get(), HgiTokens->OpenGL, color_handle, depth_handle);
#else

    GfVec4i region;
    if (m_framing.dataWindow.IsValid())
    {
        // y-flipped
        region = GfVec4i(m_framing.dataWindow.GetMinX(), m_framing.displayWindow.GetMax()[1] - m_framing.dataWindow.GetMaxY() - 1,
                         m_framing.dataWindow.GetWidth(), m_framing.dataWindow.GetHeight());
    }
    else
    {
        region = GfVec4i(m_framing.displayWindow.GetMin()[0], m_framing.displayWindow.GetMin()[1], m_framing.displayWindow.GetSize()[0],
                         m_framing.displayWindow.GetSize()[1]);
    }

    interop.TransferToApp(m_hgi.get(), color_handle, depth_handle, HgiTokens->OpenGL, VtValue(static_cast<uint32_t>(app_draw_fbo)), region);
#endif
}

void ViewportHydraEngine::update_aov_texture(HdRenderBuffer* buffer, HgiTextureHandle& handle, HgiTextureHandle& persistent_buffer) const
{
    auto resource = buffer->GetResource(false);
    if (!resource.IsEmpty() && resource.IsHolding<HgiTextureHandle>())
    {
        handle = resource.UncheckedGet<HgiTextureHandle>();
        return;
    }

    const GfVec3i dim((int)buffer->GetWidth(), buffer->GetHeight(), buffer->GetDepth());
    const auto format = HdxHgiConversions::GetHgiFormat(buffer->GetFormat());
    const auto size = HdDataSizeOfFormat(buffer->GetFormat());
    const auto src_data = buffer->Map();
#if PXR_VERSION >= 2108
    if (persistent_buffer && persistent_buffer->GetDescriptor().dimensions == dim && persistent_buffer->GetDescriptor().format == format)
    {
        HgiTextureCpuToGpuOp copy_op;
        copy_op.bufferByteSize = dim[0] * dim[1] * dim[2] * size;
        copy_op.cpuSourceBuffer = src_data;
        copy_op.gpuDestinationTexture = persistent_buffer;

        auto blit_cmd = m_hgi->CreateBlitCmds();
        blit_cmd->CopyTextureCpuToGpu(copy_op);
        m_hgi->SubmitCmds(blit_cmd.get());
    }
    else
    {
#endif
        if (persistent_buffer)
            m_hgi->DestroyTexture(&persistent_buffer);
        HgiTextureDesc tex_desc;
        tex_desc.debugName = "ViewportHydraEngine_present";
        tex_desc.dimensions = dim;
        tex_desc.initialData = src_data;
        tex_desc.format = format;
        tex_desc.layerCount = 1;
        tex_desc.mipLevels = 1;
        tex_desc.pixelsByteSize = dim[0] * dim[1] * dim[2] * size;
        tex_desc.sampleCount = PXR_INTERNAL_NS::HgiSampleCount1;
        tex_desc.usage = HgiTextureUsageBitsShaderWrite;
        persistent_buffer = m_hgi->CreateTexture(tex_desc);
#if PXR_VERSION >= 2108
    }
#endif
    handle = persistent_buffer;
    buffer->Unmap();
}

void ViewportHydraEngine::set_render_settings(std::shared_ptr<HydraRenderSettings> render_settings)
{
    if (!is_valid())
        return;
    m_render_settings = render_settings;
    m_engine = HdEngine();
    if (use_aovs())
    {
        initialize_aovs();
    }
    else
    {
        m_viewport_output = HdAovTokens->color;
        m_task_controller->SetViewportRenderOutput(m_viewport_output);
    }
}

std::shared_ptr<HydraRenderSettings> ViewportHydraEngine::get_render_settings() const
{
    return m_render_settings;
}

HdRenderBuffer* OPENDCC_NAMESPACE::ViewportHydraEngine::get_aov_texture(const TfToken& aov) const
{
    if (!has_aov(aov))
        return nullptr;

    VtValue result;
    if (auto buffer = m_task_controller->GetRenderOutput(aov))
    {
        buffer->Resolve();
        return buffer;
    }
    return nullptr;
}

#endif
TfTokenVector ViewportHydraEngine::get_render_plugins()
{
    HfPluginDescVector plugin_descriptors;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&plugin_descriptors);

    TfTokenVector plugins;
    for (size_t i = 0; i < plugin_descriptors.size(); ++i)
    {
        plugins.push_back(plugin_descriptors[i].id);
    }
    return plugins;
}

std::string ViewportHydraEngine::get_render_display_name(TfToken const& id)
{
    HfPluginDesc plugin_descriptor;
    if (!TF_VERIFY(HdRendererPluginRegistry::GetInstance().GetPluginDesc(id, &plugin_descriptor)))
    {
        return std::string();
    }

    return plugin_descriptor.displayName;
}

TfToken ViewportHydraEngine::get_render_plugin_id(TfToken const& id)
{
    HfPluginDesc plugin_descriptor;
    if (!TF_VERIFY(HdRendererPluginRegistry::GetInstance().GetPluginDesc(id, &plugin_descriptor)))
    {
        return TfToken();
    }

    return plugin_descriptor.id;
}

TfToken ViewportHydraEngine::get_default_render_plugin()
{
    std::string default_renderer_display_name = TfGetenv("HD_DEFAULT_RENDERER", "");

    if (default_renderer_display_name.empty())
    {
        auto settings = Application::instance().get_settings();
        default_renderer_display_name = settings->get("viewport.default_render_delegate", "Storm");
        if (default_renderer_display_name == "Storm")
            default_renderer_display_name = "GL";
    }

    HfPluginDescVector pluginDescs;
    HdRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescs);

    // Look for the one with the matching display name
    for (const auto& plugin_desc : pluginDescs)
    {
        if (plugin_desc.displayName == default_renderer_display_name)
        {
            return plugin_desc.id;
        }
    }

    TF_WARN("Failed to find default renderer with display name '%s'.", default_renderer_display_name.c_str());

    return TfToken();
}

TfToken ViewportHydraEngine::get_current_render_id() const
{
    return m_renderer_id;
}

bool ViewportHydraEngine::set_renderer_plugin(TfToken const& id)
{
    HdRendererPlugin* plugin = nullptr;
    TfToken actual_id;

    if (id.IsEmpty())
    {
        actual_id = HdRendererPluginRegistry::GetInstance().GetDefaultPluginId();
    }
    else
    {
        auto plugin = HdRendererPluginRegistry::GetInstance().GetOrCreateRendererPlugin(id);
        if (plugin && plugin->IsSupported())
        {
            actual_id = id;
        }
        else
        {

            TF_CODING_ERROR("Invalid plugin id or plugin is unsupported: %s", id.GetText());
            return false;
        }
    }

    if (m_render_delegate && actual_id == m_renderer_id)
    {
        return true;
    }
    delete_hydra_resources(true);

    m_renderer_id = actual_id;

    init_hydra_resources();

    return true;
}

void ViewportHydraEngine::restart()
{
    init_hydra_resources();
}

#if PXR_VERSION >= 2108
void ViewportHydraEngine::set_framing(const CameraUtilFraming& framing)
{
    if (!is_valid())
        return;
    m_framing = framing;
#ifdef OPENDCC_USE_HYDRA_FRAMING_API
    m_task_controller->SetFraming(framing);
#else
    if (framing.dataWindow.IsValid())
    {
        set_render_viewport(GfVec4d(0, 0, framing.dataWindow.GetWidth(), framing.dataWindow.GetHeight()));
    }
    else
    {
        set_render_viewport(GfVec4d(0, 0, framing.displayWindow.GetMax()[0], framing.displayWindow.GetMax()[1]));
    }
#endif
}

#ifdef OPENDCC_USE_HYDRA_FRAMING_API
void ViewportHydraEngine::set_render_buffer_size(const GfVec2i& size)
{
    if (!is_valid())
        return;
    m_task_controller->SetRenderBufferSize(size);
}

void ViewportHydraEngine::set_override_window_policy(const std::pair<bool, CameraUtilConformWindowPolicy>& policy)
{
    if (!is_valid())
        return;
    m_task_controller->SetOverrideWindowPolicy(policy);
}
#endif
#endif

#if PXR_VERSION >= 2005
void ViewportHydraEngine::update_render_settings()
{
    if (!m_render_settings)
    {
        return;
    }

    for (const auto& setting : m_render_settings->get_settings())
    {
        m_render_index->GetRenderDelegate()->SetRenderSetting(setting.first, setting.second);
    }

    VtArray<HdAovSettingsMap> settings_array;
    for (const auto& product : m_render_settings->get_render_products())
    {
        HdAovSettingsMap settings = product.settings;

        VtArray<HdAovSettingsMap> ordered_vars;
        for (const auto& var : product.render_vars)
        {
            ordered_vars.push_back(var.descriptor.aovSettings);
        }
        settings[TfToken("orderedVars")] = VtValue(ordered_vars);
        settings_array.push_back(settings);
    }
    m_render_index->GetRenderDelegate()->SetRenderSetting(TfToken("delegateRenderProducts"), VtValue(settings_array));
}
#endif

void ViewportHydraEngine::init_hydra_resources()
{
    HdSelectionSharedPtr selection = m_sel_tracker->GetSelectionMap();
    if (!selection)
    {
        selection.reset(new HdSelection);
    }
    // Delete hydra state.
    delete_hydra_resources(false);
    if (!m_renderer_id.IsEmpty())
    {
        m_render_delegate = HdRendererPluginRegistry::GetInstance().CreateRenderDelegate(m_renderer_id);
    }
    if (!m_render_delegate)
    {
        // quick fix to avoid app crash in case if a render delegate is impossible to create
        // TODO: refactor in a more appropriate way
        set_renderer_plugin(TfToken());
        return;
    }
    auto render_settings = read_render_settings(m_render_delegate.Get());
    for (const auto& entry : render_settings)
        m_render_delegate->SetRenderSetting(entry.first, entry.second);
    m_prune_cid = HydraEngineSceneIndicesNotifier::register_index_created(
        HydraEngineSceneIndicesNotifier::IndexType::Prune, [this](HdSceneIndexBaseRefPtr index) {
            if (m_prune_si = TfDynamic_cast<TfRefPtr<PruneSceneIndex>>(index))
            {
                m_prune_si->set_predicate(
                    [this, populated = m_populated_paths](const SdfPath& path) { return prune_scene_index_predicate(populated, path); });
            }
        });
    // Force init registry
    HdSceneIndexPluginRegistry::GetInstance();
#if PXR_VERSION >= 2005
    m_render_driver = { HgiTokens->renderDriver, VtValue(m_hgi.get()) };
    m_render_index = std::shared_ptr<HdRenderIndex>(HdRenderIndex::New(m_render_delegate.Get(), { &m_render_driver }));
#else
    m_render_index = std::shared_ptr<HdRenderIndex>(HdRenderIndex::New(renderDelegate));
#endif

    HydraEngineSceneIndicesNotifier::unregister_index_created(HydraEngineSceneIndicesNotifier::IndexType::Prune, m_prune_cid);
    // Create the new delegate & task controller.

    init_scene_resources();

    m_task_controller = new HdxTaskController(
        m_render_index.get(), SdfPath::AbsoluteRootPath().AppendChild(
                                  TfToken(TfStringPrintf("_UsdImaging_%s_%p", TfMakeValidIdentifier(m_renderer_id.GetText()).c_str(), this))));

    m_sel_tracker->SetSelection(selection);
    m_task_controller->SetSelectionColor(m_selection_color);
    HdxColorCorrectionTaskParams hdParams;
    hdParams.colorCorrectionMode = HdxColorCorrectionTokens->disabled;
    m_task_controller->SetColorCorrectionParams(hdParams);
    m_task_controller->SetFreeCameraMatrices(m_view_mat, m_proj_mat);
    m_task_controller->SetEnableShadows(true);

    HdxShadowTaskParams shadow_params;
    shadow_params.cullStyle = HdCullStyleBack;
    m_task_controller->SetShadowParams(shadow_params);
#if PXR_VERSION >= 2108
    m_task_controller->SetEnablePresentation(false);
#endif
#if PXR_VERSION >= 2005
    initialize_aovs();
#if PXR_VERSION >= 2108
    init_depth_compositor();
#endif
#endif
}

void OPENDCC_NAMESPACE::ViewportHydraEngine::init_scene_resources()
{
    if (!use_hydra2())
    {
        m_scene_delegates.clear();
        for (const auto& root_path : m_root_prefixes)
        {
            const SdfPath delegate_id = root_path;
            for (const auto& delegate_type : m_scene_delegate_types)
            {
                if (auto delegate_factory = delegate_type.GetFactory<ViewportSceneDelegateFactoryBase>())
                {
                    m_scene_delegates[root_path].insert(delegate_factory->create(
                        m_render_index.get(), delegate_id.AppendChild(TfToken(TfMakeValidIdentifier(delegate_type.GetTypeName())))));
                }
            }
        }
    }
    else
    {
        m_render_index->InsertSceneIndex(m_si_manager->get_terminal_index(), SdfPath::AbsoluteRootPath());
    }
}

bool ViewportHydraEngine::update_hydra_collection(HdRprimCollection* collection, SdfPathVector const& roots, ViewportHydraEngineParams const& params)
{
    if (collection == nullptr)
    {
        TF_CODING_ERROR("Null passed to _UpdateHydraCollection");
        return false;
    }

    if (collection == &m_intersect_collection)
    {
        *collection =
            HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->refinedWireOnSurf, HdReprTokens->wireOnSurf, HdReprTokens->points));
        collection->SetRootPaths(roots);
        return true;
    }

    // choose repr
    HdReprSelector reprSelector;
    TfToken unrefined_token;
    TfToken points_token = params.draw_mode & ViewportHydraDrawMode::DRAW_POINTS ? HdReprTokens->points : TfToken();
    // bool refined = params.complexity > 1.0;
    bool refined = true;

    if (params.draw_mode & (ViewportHydraDrawMode::DRAW_GEOM_FLAT | ViewportHydraDrawMode::DRAW_SHADED_FLAT))
    {
        // Flat shading
        reprSelector = HdReprSelector(HdReprTokens->hull, unrefined_token, points_token);
    }
    else if (params.draw_mode & ViewportHydraDrawMode::DRAW_WIREFRAME_ON_SURFACE)
    {
        // Wireframe on surface
        reprSelector = HdReprSelector(refined ? HdReprTokens->refinedWireOnSurf : HdReprTokens->wireOnSurf, unrefined_token, points_token);
    }
    else if (params.draw_mode & ViewportHydraDrawMode::DRAW_WIREFRAME)
    {
        // Wireframe
        reprSelector = HdReprSelector(refined ? HdReprTokens->refinedWire : HdReprTokens->wire, unrefined_token, points_token);
    }
    else if (params.draw_mode == ViewportHydraDrawMode::DRAW_POINTS)
    {
        reprSelector = HdReprSelector(HdReprTokens->points);
    }
    else
    {
        // Smooth shading
        reprSelector = HdReprSelector(refined ? HdReprTokens->refined : HdReprTokens->smoothHull, unrefined_token, points_token);
    }

    // By default our main collection will be called geometry
    TfToken colName = HdTokens->geometry;

    // Check if the collection needs to be updated (so we can avoid the sort).
    SdfPathVector const& oldRoots = collection->GetRootPaths();

    // inexpensive comparison first
    bool match = collection->GetName() == colName && oldRoots.size() == roots.size() && collection->GetReprSelector() == reprSelector;

    // Only take the time to compare root paths if everything else matches.
    if (match)
    {
        // Note that oldRoots is guaranteed to be sorted.
        for (size_t i = 0; i < roots.size(); i++)
        {
            // Avoid binary search when both vectors are sorted.
            if (oldRoots[i] == roots[i])
                continue;
            // Binary search to find the current root.
            if (!std::binary_search(oldRoots.begin(), oldRoots.end(), roots[i]))
            {
                match = false;
                break;
            }
        }

        // if everything matches, do nothing.
        if (match)
            return false;
    }

    // Recreate the collection.
    *collection = HdRprimCollection(colName, reprSelector);
    collection->SetRootPaths(roots);

    return true;
}

void ViewportHydraEngine::compute_render_tags(ViewportHydraEngineParams const& params, TfTokenVector* renderTags)
{
    renderTags->clear();
    renderTags->reserve(4);
    renderTags->push_back(HdTokens->geometry);
    if (params.show_guides)
    {
        renderTags->push_back(HdRenderTagTokens->guide);
    }
    if (params.show_proxy)
    {
        renderTags->push_back(HdRenderTagTokens->proxy);
    }
    if (params.show_render)
    {
        renderTags->push_back(HdRenderTagTokens->render);
    }
    if (params.show_locators)
    {
        renderTags->push_back(TfToken("locator"));
    }
}

void ViewportHydraEngine::execute(const ViewportHydraEngineParams& params, HdTaskSharedPtrVector tasks)
{
    // User is responsible for initializing GL context and glew
    bool isCoreProfileContext = GlfContextCaps::GetInstance().coreProfile;

    GLF_GROUP_FUNCTION();
#if PXR_VERSION >= 2108
    // store current app's framebuffer and restore it after render is done
    GLint restore_read_fbo = 0;
    GLint restore_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restore_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restore_draw_fbo);
#endif
    if (!isCoreProfileContext)
    {
        glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT);
    }
    GLuint vao;
    // We must bind a VAO (Vertex Array Object) because core profile
    // contexts do not have a default vertex array object. VAO objects are
    // container objects which are not shared between contexts, so we create
    // and bind a VAO here so that core rendering code does not have to
    // explicitly manage per-GL context state.
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // hydra orients all geometry during topological processing so that
    // front faces have ccw winding. We disable culling because culling
    // is handled by fragment shader discard.
    bool flipFrontFacing = false;
    if (flipFrontFacing)
    {
        glFrontFace(GL_CW); // < State is pushed via GL_POLYGON_BIT
    }
    else
    {
        glFrontFace(GL_CCW); // < State is pushed via GL_POLYGON_BIT
    }

    glDisable(GL_CULL_FACE);

    if (params.apply_render_state)
    {
        glDisable(GL_BLEND);
    }

    // note: to get benefit of alpha-to-coverage, the target framebuffer
    // has to be a MSAA buffer.
    if (params.enable_id_render)
    {
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
    else if (params.enable_sample_alpha_to_coverage)
    {
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    // for points width
    glEnable(GL_PROGRAM_POINT_SIZE);

    // TODO:
    //  * forceRefresh
    //  * showGuides, showRender, showProxy
    //  * gammaCorrectColors

    // HdTaskSharedPtrVector tasks = m_task_controller->GetTasks();
    m_engine.Execute(m_render_index.get(), &tasks);

#if PXR_VERSION >= 2108
    glBindFramebuffer(GL_READ_FRAMEBUFFER, restore_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restore_draw_fbo);
#endif
#if PXR_VERSION >= 2005
    compose_aovs();
#endif
    glBindVertexArray(0);
    // XXX: We should not delete the VAO on every draw call, but we
    // currently must because it is GL Context state and we do not control
    // the context.
    glDeleteVertexArrays(1, &vao);
    if (!isCoreProfileContext)
    {
        glPopAttrib(); // GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT
    }
}

HdxRenderTaskParams ViewportHydraEngine::make_viewport_hydra_render_params(const ViewportHydraEngineParams& params)
{
    HdxRenderTaskParams hd_params;

    if (params.draw_mode == ViewportHydraDrawMode::DRAW_POINTS)
    {
        hd_params.enableLighting = false;
    }
    else
    {
        hd_params.enableLighting = params.enable_lighting && !params.enable_id_render;
    }

    // enableIdRender was removed from HdxRenderTaskParams in USD 25.05, id render is driven through HdxPickTask there
#if PXR_VERSION < 2505
    hd_params.enableIdRender = params.enable_id_render;
#endif
    hd_params.depthBiasUseDefault = true;
    hd_params.depthFunc = params.depth_func;
    hd_params.cullStyle =
        params.cull_style == ViewportHydraCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED ? HdCullStyleBackUnlessDoubleSided : HdCullStyleNothing;
    // enableSceneMaterials was removed from HdxRenderTaskParams in USD 25.11
#if PXR_VERSION < 2511
    hd_params.enableSceneMaterials = params.enable_scene_materials;
#endif

    return hd_params;
}

void ViewportHydraEngine::set_selected(const SelectionList& selection_list, const RichSelection& rich_selection)
{
    if (!is_valid())
        return;
    m_dirty_selection = true;
    m_selection_list = selection_list;
    m_rich_selection = rich_selection;
}

void ViewportHydraEngine::set_rollover_prims(const SdfPathVector& rollover_prims)
{
    if (!is_valid())
        return;
    if (m_rollover_list.get_fully_selected_paths() == rollover_prims)
        return;

    m_dirty_selection = true;
    m_rollover_list = SelectionList(rollover_prims);
}

void ViewportHydraEngine::set_selection_color(GfVec4f const& color)
{
    if (!is_valid())
        return;
    m_selection_color = color;
    m_task_controller->SetSelectionColor(m_selection_color);
}

bool ViewportHydraEngine::is_converged() const
{
    if (!is_valid())
        return false;
    return m_task_controller->IsConverged();
}

bool ViewportHydraEngine::test_intersection_batch(const ViewportHydraIntersectionParams& params, HdxPickHitVector& out_hits)
{
    if (!is_valid())
        return false;
    HdxPickTaskContextParams pick_params;
    if (params.use_custom_collection)
    {
        pick_params.collection = params.collection;
        // We have two prefixes for each delegateId:
        // /stage_prefix/delegate_type_prefix/path/to/prim
        // When user pases custom collection for intersection he passes USD paths to prims
        // It is necessary to convert these USD paths to correct delegateIDs, otherwise hydra picking task will simply ignore them
        // Loop below IS NOT O(n^4) as it looks like:
        // For each collection (roots, excluded) we need to convert their paths to each delegate ID path.
        // This means that if we have only 3 delegates and 1 stage (most of the time) then the loop will make only GetRootPaths().size() *
        // GetExcludePaths().size() * 1 * 3 iterations
        // TODO: It looks dirty, we have to revisit the way how we handle picking in multi-delegate/multi-stage scenarios
        std::pair<SdfPathVector, SdfPathVector> roots = { pick_params.collection.GetRootPaths(), SdfPathVector() };
        std::pair<SdfPathVector, SdfPathVector> exclude_roots = { pick_params.collection.GetExcludePaths(), SdfPathVector() };
        for (auto& col : std::vector<decltype(roots)*> { &roots, &exclude_roots })
        {
            for (const auto& r : col->first)
            {
                for (const auto& per_stage_delegates : m_scene_delegates)
                {
                    for (const auto& delegate : per_stage_delegates.second)
                    {
                        col->second.push_back(delegate->GetDelegateID().AppendPath(r.MakeRelativePath(SdfPath::AbsoluteRootPath())));
                    }
                }
            }
        }
        pick_params.collection.SetRootPaths(roots.second);
        pick_params.collection.SetExcludePaths(exclude_roots.second);
    }
    else if (params.engine_params.stage_resolver)
    {
        const auto roots = params.engine_params.stage_resolver->get_stage_roots(params.engine_params.frame);
        auto iter = std::find(roots.begin(), roots.end(), m_current_root);
        if (iter == roots.end())
            return false;
        update_hydra_collection(&m_render_collection, { m_current_root }, params.engine_params);
        pick_params.collection = m_render_collection;
    }
    else
    {
        pick_params.collection = m_render_collection;
    }

    if (params.use_custom_render_tags)
    {
        m_task_controller->SetRenderTags(params.render_tags);
    }
    else
    {
        TfTokenVector render_tags;
        compute_render_tags(params.engine_params, &render_tags);
        m_task_controller->SetRenderTags(render_tags);
    }

    HdxRenderTaskParams hdParams = make_viewport_hydra_render_params(params.engine_params);
    m_task_controller->SetRenderParams(hdParams);

    pick_params.resolution = params.resolution;
    pick_params.viewMatrix = params.view_matrix;
    pick_params.resolveMode = params.resolve_mode;
    pick_params.pickTarget = get_pick_target(params.pick_target);
    pick_params.projectionMatrix = params.proj_matrix;
    pick_params.clipPlanes = params.engine_params.clip_planes;
    pick_params.outHits = &out_hits;

    m_engine.SetTaskContextData(HdxPickTokens->pickParams, VtValue(pick_params));

    execute(params.engine_params, m_task_controller->GetPickingTasks());

    // Since HdxPickTask in ResolveAll mode very slow, we prefer to use ResolveUnique mode.
    // But HdxPickResult::_GetHash doesn't takes into account component hash unless explicitly specified HdxPickTokens->pickPoints,
    // HdxPickTokens->pickEdges, etc. Hence, it is impossible to select points, edges, elements, prims, instances in a single render pass. For this
    // reason, we execute pick task twice with specified pick mode.. It still about two times faster than process ResolveAll results.
    TfToken component_pick_target;
    if ((params.pick_target != SelectionList::SelectionFlags::POINTS) && (params.pick_target & SelectionList::SelectionFlags::POINTS))
    {
        component_pick_target = get_pick_target(SelectionList::SelectionFlags::POINTS);
    }
    else if ((params.pick_target != SelectionList::SelectionFlags::EDGES) && (params.pick_target & SelectionList::SelectionFlags::EDGES))
    {
        component_pick_target = get_pick_target(SelectionList::SelectionFlags::EDGES);
    }
    else if ((params.pick_target != SelectionList::SelectionFlags::ELEMENTS) && (params.pick_target & SelectionList::SelectionFlags::ELEMENTS))
    {
        component_pick_target = get_pick_target(SelectionList::SelectionFlags::ELEMENTS);
    }

    if (!component_pick_target.IsEmpty())
    {
        HdxPickHitVector point_hits;
        pick_params.pickTarget = component_pick_target;
        pick_params.outHits = &point_hits;
        m_engine.SetTaskContextData(HdxPickTokens->pickParams, VtValue(pick_params));
        execute(params.engine_params, m_task_controller->GetPickingTasks());

        out_hits.insert(out_hits.end(), point_hits.begin(), point_hits.end());
    }

    if (!out_hits.empty())
    {
        // If selection pick target is edges then we must ensure that we select both half edges
        if (params.pick_target & SelectionList::SelectionFlags::EDGES)
        {
            struct EdgeTopology
            {
                HdMeshTopology topo;
                std::unique_ptr<EdgeIndexTable> edge_table;
            };
            std::unordered_map<SdfPath, EdgeTopology, SdfPath::Hash> index_table_cache;

            std::unordered_map<int, const HdxPickHit*> half_edges_to_add;
            for (const auto& hit : out_hits)
            {
                if (hit.edgeIndex == -1)
                {
                    continue;
                }
                half_edges_to_add.erase(hit.edgeIndex);

                auto index_table_it = index_table_cache.find(hit.objectId);
                if (index_table_it == index_table_cache.end())
                {
                    auto delegate = m_render_index->GetSceneDelegateForRprim(hit.objectId);
                    EdgeTopology new_edge_topo;
                    new_edge_topo.topo = delegate->GetMeshTopology(hit.objectId);
                    new_edge_topo.edge_table = std::make_unique<EdgeIndexTable>(&new_edge_topo.topo);
                    index_table_it = index_table_cache.emplace(hit.objectId, std::move(new_edge_topo)).first;
                    if (index_table_it == index_table_cache.end())
                    {
                        continue;
                    }
                }

                auto [vertices, vert_res] = index_table_it->second.edge_table->get_vertices_by_edge_id(hit.edgeIndex);
                if (!vert_res)
                {
                    continue;
                }

                auto [edge_indices, edge_res] = index_table_it->second.edge_table->get_edge_id_by_edge_vertices(vertices);
                for (auto ind : edge_indices)
                {
                    if (ind != hit.edgeIndex)
                    {
                        half_edges_to_add.emplace(ind, &hit);
                    }
                }
            }

            for (const auto& edge_to_add : half_edges_to_add)
            {
                HdxPickHit edge_hit = *edge_to_add.second;
                edge_hit.edgeIndex = edge_to_add.first;

                out_hits.emplace_back(std::move(edge_hit));
            }
        }

        return true;
    }
    return false;
}

void ViewportHydraEngine::resume()
{
    if (!is_valid())
        return;
    m_render_index->GetRenderDelegate()->Resume();
}

void ViewportHydraEngine::pause()
{
    if (!is_valid())
        return;
    m_render_index->GetRenderDelegate()->Pause();
}

bool ViewportHydraEngine::is_pause_supported() const
{
    if (!is_valid())
        return false;
    return m_render_index->GetRenderDelegate()->IsPauseSupported();
}

void ViewportHydraEngine::stop()
{
    if (!is_valid())
        return;
#if PXR_VERSION > 1911
    m_render_index->GetRenderDelegate()->Stop();
#endif
}

bool ViewportHydraEngine::is_stop_supported() const
{
    if (!is_valid())
        return false;
#if PXR_VERSION > 1911
    return m_render_index->GetRenderDelegate()->IsStopSupported();
#else
    return false;
#endif
}

void ViewportHydraEngine::reset()
{
    set_selected(SelectionList());
    set_rollover_prims({});
    init_hydra_resources();
}

#if PXR_VERSION < 2005
SdfPath ViewportHydraEngine::get_prim_path_from_instance_index(SdfPath const& protoRprimPath, int instanceIndex, int* absoluteInstanceIndex,
                                                               SdfPath* rprimPath, SdfPathVector* instanceContext)
{
    auto iter = m_scene_delegates.find(m_current_root);
    if (iter == m_scene_delegates.end())
        return SdfPath();
    for (const auto& delegate : iter->second)
    {
        auto instance_path = delegate->GetPathForInstanceIndex(protoRprimPath, instanceIndex, absoluteInstanceIndex, rprimPath, instanceContext);
        if (instance_path.IsEmpty())
            continue;
        if (!instance_path.IsEmpty())
            return instance_path;
    }
    return SdfPath();
}
#else
SdfPath ViewportHydraEngine::get_prim_path_from_instance_index(const SdfPath& rprimId, int instanceIndex, HdInstancerContext* instancer_context)
{
    auto iter = m_scene_delegates.find(m_current_root);
    if (iter == m_scene_delegates.end())
        return SdfPath();
    for (const auto& delegate : iter->second)
    {
        HdInstancerContext ctx;
        auto instance_path = delegate->GetScenePrimPath(rprimId, instanceIndex, &ctx);
        if (instance_path.IsEmpty())
            continue;
        if (instance_path != rprimId || !rprimId.IsPropertyPath())
        {
            if (instancer_context)
                *instancer_context = ctx;
            return instance_path;
        }
    }
    return SdfPath();
}
#endif

void ViewportHydraEngine::update_init(ViewportHydraEngineParams& engine_params)
{
    if (!is_valid())
        return;

    if (engine_params.populated_paths != m_populated_paths)
    {
        m_populated_paths = engine_params.populated_paths;
        if (m_prune_si)
        {
            m_prune_si->set_predicate(
                [this, populated = m_populated_paths](const SdfPath& path) { return prune_scene_index_predicate(populated, path); });
        }
    }

    if (engine_params.stage_resolver)
    {
        if (engine_params.stage_resolver->is_dirty() || (m_root_prefixes.size() == 1 && m_root_prefixes[0] == SdfPath::AbsoluteRootPath()))
        {
            m_root_prefixes = engine_params.stage_resolver->get_stage_roots();
            init_scene_resources();
        }
    }

    m_current_root = engine_params.current_stage_root;
}

void ViewportHydraEngine::update_delegates(const ViewportHydraEngineParams& engine_params)
{
    if (!is_valid())
        return;

    for (const auto& per_stage_delegates : m_scene_delegates)
    {
        std::shared_ptr<ViewportUsdDelegate> usd_imaging_delegate;
#if PXR_VERSION >= 2108
        for (const auto& delegate : per_stage_delegates.second)
        {
            if (auto usd_delegate = std::dynamic_pointer_cast<ViewportUsdDelegate>(delegate))
            {
                usd_imaging_delegate = usd_delegate;
                break;
            }
        }
#endif
        if (usd_imaging_delegate)
        {
            auto params = engine_params;
            params.user_data["usd_delegate"] = VtValue(usd_imaging_delegate.get());
            for (const auto& delegate : per_stage_delegates.second)
                delegate->update(params);
        }
        else
        {
            for (const auto& delegate : per_stage_delegates.second)
                delegate->update(engine_params);
        }
    }

    if (m_dirty_selection)
    {
        auto selection = std::make_shared<HdSelectionExt>();
        if (m_rich_selection.has_color_data())
        {
            for (const auto& entry : m_rich_selection)
            {
                std::vector<VtIntArray> point_indices;
                point_indices.reserve(entry.second.size());
                VtVec4fArray point_colors;
                point_colors.reserve(entry.second.size());
                for (const auto& weight : entry.second)
                {
                    point_indices.push_back({ (int)weight.first });
                    auto col = m_rich_selection.get_soft_selection_color(weight.second);
                    point_colors.push_back(GfVec4f(col[0], col[1], col[2], 1));
                }
                // find scene delegates that have an interest for this prim
                for (const auto& delegates : m_scene_delegates)
                {
                    for (const auto& delegate : delegates.second)
                    {
                        const auto index_prim_name = delegate->convert_stage_path_to_index_path(entry.first);
                        if (delegate->GetRenderIndex().HasRprim(index_prim_name))
                            selection->add_points(HdSelection::HighlightMode::HighlightModeSelect, index_prim_name, point_indices, point_colors);
                    }
                }
            }
        }

        for (const auto& per_stage_delegates : m_scene_delegates)
        {
            for (const auto& delegate : per_stage_delegates.second)
            {
                // TODO:
                // Add to SelectionList rollover indices, remove `set_selection_mode` delegate method and remove engine, gl_widget
                // `set_rollover_prim`. Current solution allows to achieve rollover effect on drag and drop action and it doesn't breaks existing API.
                delegate->set_selection_mode(HdSelection::HighlightMode::HighlightModeSelect);
                delegate->populate_selection(m_selection_list, selection);
                delegate->set_selection_mode(HdSelection::HighlightMode::HighlightModeLocate);
                delegate->populate_selection(m_rollover_list, selection);
            }
        }

        // set the result back to selection tracker
        m_sel_tracker->SetSelection(selection);
        m_dirty_selection = false;
    }
}

void ViewportHydraEngine::update_scene_indices(const ViewportHydraEngineParams& engine_params)
{
    if (!is_valid())
        return;

    if (m_dirty_selection)
    {
        m_si_manager->set_selection(m_selection_list);
        m_dirty_selection = false;
    }
}

GfRange3d ViewportHydraEngine::get_bbox(const SdfPath& path)
{
    GfRange3d bbox;
    auto iter = m_scene_delegates.find(m_current_root);
    if (iter == m_scene_delegates.end())
        return bbox;

    for (const auto& delegate : iter->second)
    {
        auto index_path = delegate->convert_stage_path_to_index_path(path);
        GfBBox3d local_extent = delegate->GetExtent(index_path);
        auto world_transform = delegate->GetTransform(index_path);
        local_extent.Transform(world_transform);
        bbox.ExtendBy(local_extent.ComputeAlignedBox());
    }

    return bbox;
}

bool ViewportHydraEngine::is_hd_st() const
{
    return m_renderer_id == HdRendererPluginRegistry::GetInstance().GetDefaultPluginId();
}

HdRenderSettingDescriptorList ViewportHydraEngine::get_render_setting_descriptors() const
{
    if (!is_valid())
        return HdRenderSettingDescriptorList();
    return m_render_index->GetRenderDelegate()->GetRenderSettingDescriptors();
}

VtValue ViewportHydraEngine::get_render_setting(const TfToken& key) const
{
    if (!is_valid())
        return VtValue();
    return m_render_index->GetRenderDelegate()->GetRenderSetting(key);
}

void ViewportHydraEngine::set_render_setting(const TfToken& key, const VtValue& value)
{
    if (!is_valid())
        return;
    auto settings = Application::instance().get_settings();
    settings->set("viewport." + get_render_display_name(m_renderer_id) + "." + key.GetString(), value);
    m_render_index->GetRenderDelegate()->SetRenderSetting(key, value);
}

void ViewportHydraEngine::set_scene_delegates(const std::unordered_set<TfType, TfHash>& delegate_types)
{
    if (m_scene_delegate_types == delegate_types)
        return;

    m_si_manager = nullptr;
    m_scene_delegate_types = delegate_types;
    init_hydra_resources();
}
void ViewportHydraEngine::set_scene_index_manager(const std::shared_ptr<SceneIndexManager>& si_manager)
{
    if (m_si_manager == si_manager)
        return;

    m_scene_delegate_types.clear();
    m_si_manager = si_manager;
    init_hydra_resources();
}

bool ViewportHydraEngine::is_valid() const
{
    return m_task_controller && m_render_index;
}

UsdStageRefPtr ViewportHydraEngineParams::resolve_stage(const SdfPath& delegate_id) const
{
    return stage_resolver ? stage_resolver->get_stage(get_stage_prefix(delegate_id)) : Application::instance().get_session()->get_current_stage();
}

OPENDCC_NAMESPACE_CLOSE
