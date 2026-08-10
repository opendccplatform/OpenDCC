// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/uv_editor/uv_scene_delegate.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"

#include <numeric>

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/imaging/hd/material.h>
#if PXR_VERSION < 2108
#include <pxr/imaging/glf/udimTexture.h>
#else
#include <pxr/imaging/hdSt/udimTextureObject.h>
#endif

#include <pxr/imaging/hio/glslfx.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(UVSceneDelegateTokens,
                         ((bg_texture_name, "___uv_background_texture_"))((bg_texture_material, "___uv_background_material_")));

static bool is_supported_udim_texture(const std::string& image_path)
{
#if PXR_VERSION < 2108
    return GlfIsSupportedUdimTexture(image_path);
#else
    return HdStIsSupportedUdimTexture(image_path);
#endif
}

static const std::string s_texture_source =
    R"#(-- glslfx version 0.1
-- configuration
{
    "textures": {
        "texture" : {
            "documentation" : "UV background texture"
        }
    },
    "techniques": {
        "default": {
            "surfaceShader": {
                "source": [ "uv_background" ]
            }
        }
    }
} 
--- --------------------------------------------------------------------------
-- glsl uv_background

    vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
    {
    #ifdef HD_HAS_texture
        vec4 result = vec4(HdGet_texture().xyzw);
    #else
        vec4 result = vec4(0, 0, 0, 0);
    #endif
        return result;
    }
)#";

static const std::string s_mesh_source =
    R"#(-- glslfx version 0.1

// selection_mode
// 0 - points
// 1 - edges
// 2 - faces
// 3 - uv

// render_mode
// 0 - Hull
// 1 - Wire

-- configuration
{
    "metadata": {
        "materialTag": "translucent"
    },
    "techniques": {
        "default": {
            "surfaceShader": {
                "source": [ "uv_mesh" ]
            }
        }
    },
    "parameters": {
        "selection_mode" : {
            "default": 0
        },
        "render_mode" : {
            "default": 0
        }
    }
} 
--- --------------------------------------------------------------------------
-- glsl uv_mesh

vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
{
    int selection_mode = 0;
#if defined(HD_HAS_selection_mode)
    selection_mode = HdGet_selection_mode();
#endif

    int render_mode = 0;
#if defined(HD_HAS_render_mode)
    render_mode = HdGet_render_mode();
#endif

    vec4 object_color = vec4(0.1, 0.1, 0.1, 0.1);
    vec4 override_color = ApplyColorOverrides(object_color);

    int override = int((render_mode == 0 && selection_mode == 2) || (render_mode == 1 && selection_mode != 2));

    return mix(object_color, override_color, override);
}
)#";

static HdMaterialNetworkMap get_background_texture_material_network(const SdfPath& bg_texture, const TfToken& texture_node_id,
                                                                    const SdfPath& bg_texture_mat, const std::string& texture_file)
{
    HdMaterialNetworkMap result;
    result.terminals.push_back(bg_texture);

    HdMaterialNode st_reader;
    st_reader.path = bg_texture.AppendChild(TfToken("st_reader"));
    st_reader.identifier = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdPrimvarReader_float2"))->GetIdentifier();
    st_reader.parameters[TfToken("varname")] = VtValue(TfToken("st"));

    HdMaterialNode sampler;
    sampler.path = bg_texture_mat;
    sampler.identifier = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdUVTexture"))->GetIdentifier();
    sampler.parameters[TfToken("file")] = VtValue(SdfAssetPath(texture_file));

    HdMaterialNode terminal;
    terminal.path = bg_texture;
    terminal.identifier = texture_node_id;
    terminal.parameters[TfToken("texture")] = VtValue(GfVec4f(0));

    HdMaterialRelationship st_reader_to_sampler;
    st_reader_to_sampler.inputId = st_reader.path;
    st_reader_to_sampler.inputName = TfToken("result");
    st_reader_to_sampler.outputId = sampler.path;
    st_reader_to_sampler.outputName = TfToken("st");

    HdMaterialRelationship sampler_to_terminal;
    sampler_to_terminal.inputId = sampler.path;
    sampler_to_terminal.inputName = TfToken("rgba");
    sampler_to_terminal.outputId = terminal.path;
    sampler_to_terminal.outputName = TfToken("texture");

    auto& map = result.map[HdMaterialTerminalTokens->surface];
    map.nodes = { st_reader, sampler, terminal };
    map.relationships = { st_reader_to_sampler, sampler_to_terminal };
    map.primvars.push_back(TfToken("st"));

    return result;
}

OPENDCC_REGISTER_SCENE_DELEGATE(UVSceneDelegate, TfToken())

UVSceneDelegate::UVSceneDelegate(HdRenderIndex* render_index, const SdfPath& delegate_id)
    : ViewportSceneDelegate(render_index, delegate_id)
{
    GetRenderIndex().InsertSprim(HdPrimTypeTokens->material, this, get_background_texture_material());
}

UVSceneDelegate::~UVSceneDelegate()
{
    GetRenderIndex().RemoveSubtree(GetDelegateID(), this);
}

bool UVSceneDelegate::initialize(const SdfPathVector& highlighted, const TfToken& uv_primvar)
{
    m_stage = Application::instance().get_session()->get_current_stage();
    if (!m_stage)
    {
        const auto& rprims = GetRenderIndex().GetRprimIds();
        for (const auto& rpim : rprims)
        {
            GetRenderIndex().RemoveRprim(rpim);
        }

        return false;
    }

    m_watcher = std::make_unique<StageObjectChangedWatcher>(m_stage, [this](PXR_NS::UsdNotice::ObjectsChanged const& notice) {
        auto& index = GetRenderIndex();
        auto& tracker = index.GetChangeTracker();
        const auto primvar = "primvars:" + m_uv_primvar.GetString();

        UsdNotice::ObjectsChanged::PathRange paths[] = { notice.GetResyncedPaths(), notice.GetChangedInfoOnlyPaths() };

        for (const auto& paths_entry : paths)
        {
            for (const auto& path : paths_entry)
            {
                const auto prim_path = path.GetPrimPath();
                const auto index_path = convert_stage_path_to_index_path(prim_path);

                const auto rprim = index.GetRprim(index_path);
                if (!rprim)
                {
                    continue;
                }

                const auto name = path.GetName();
                if (name == primvar)
                {
                    tracker.MarkRprimDirty(index_path, HdChangeTracker::RprimDirtyBits::DirtyPoints);
                    tracker.MarkRprimDirty(index_path, HdChangeTracker::RprimDirtyBits::DirtyExtent);
                }
            }
        }
    });

    m_uv_primvar = uv_primvar;
    repopulate_geometry(highlighted);

    return true;
}

void UVSceneDelegate::repopulate_geometry(const SdfPathVector& highlighted_paths)
{
    m_highlighted_paths = highlighted_paths;

    SdfPathVector geom_paths;
    geom_paths.reserve(highlighted_paths.size());

    for (const auto& path : highlighted_paths)
    {
        if (UsdGeomMesh(m_stage->GetPrimAtPath(path)))
            geom_paths.push_back(convert_stage_path_to_index_path(path));
    }

    // Remove Rprims that are not in selection
    SdfPathVector rprims_to_remove;
    const auto& rprims = GetRenderIndex().GetRprimIds();
    std::set_difference(rprims.begin(), rprims.end(), geom_paths.begin(), geom_paths.end(), std::back_inserter(rprims_to_remove));

    const auto bg_texture_prim = get_background_texture_prim();
    for (const auto& rprim_path : rprims_to_remove)
    {
        if (rprim_path == bg_texture_prim)
        {
            continue;
        }

        GetRenderIndex().RemoveRprim(rprim_path);
    }

    // Add Rprims that are not in render index yet
    for (const auto& path : geom_paths)
    {
        if (auto mesh = UsdGeomMesh(m_stage->GetPrimAtPath(convert_index_path_to_stage_path(path))))
        {
            UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
            if (auto st = primvars_api.GetPrimvar(m_uv_primvar))
            {
                const auto st_interpolation = st.GetInterpolation();
                if (st_interpolation == UsdGeomTokens->constant || st_interpolation == UsdGeomTokens->uniform)
                {
                    TF_WARN("Unsupported interpolation type for primvar '%s' :"
                            " expected 'vertex', 'varying' or 'faceVarying', got '%s'.",
                            st.GetAttr().GetPath().GetText(), st_interpolation.GetText());
                    continue;
                }

                if (GetRenderIndex().HasRprim(path))
                {
                    GetRenderIndex().GetChangeTracker().MarkRprimDirty(path, HdChangeTracker::RprimDirtyBits::AllDirty);
                    GetRenderIndex().GetChangeTracker().MarkSprimDirty(path.AppendProperty(HdPrimTypeTokens->material),
                                                                       HdMaterial::DirtyBits::AllDirty);
                }
                else
                {
                    GetRenderIndex().InsertRprim(HdPrimTypeTokens->mesh, this, path);
                    GetRenderIndex().InsertSprim(HdPrimTypeTokens->material, this, path.AppendProperty(HdPrimTypeTokens->material));
                }
            }
        }
    }
}

SdfPath UVSceneDelegate::get_background_texture_prim() const
{
    const auto base_path = SdfPath::AbsoluteRootPath().AppendChild(UVSceneDelegateTokens->bg_texture_name);
    return convert_stage_path_to_index_path(base_path);
}

SdfPath UVSceneDelegate::get_background_texture_material() const
{
    const auto base_path = SdfPath::AbsoluteRootPath().AppendChild(UVSceneDelegateTokens->bg_texture_material);
    return convert_stage_path_to_index_path(base_path);
}

TfToken OPENDCC_NAMESPACE::UVSceneDelegate::get_texture_node_id() const
{
    if (auto node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(s_texture_source, HioGlslfxTokens->glslfx, {}))
        return node->GetIdentifier();
    else
        return TfToken();
}

void UVSceneDelegate::update_background_texture(const std::string& new_texture)
{
    const auto bg_texture_path = get_background_texture_prim();
    if (!m_show_texture || m_texture_file.empty() || (m_tiling_mode == TilingMode::NONE && is_supported_udim_texture(new_texture)))
    {
        if (GetRenderIndex().HasRprim(bg_texture_path))
            GetRenderIndex().RemoveRprim(bg_texture_path);
        return;
    }

    auto& ri = GetRenderIndex();
    ri.GetChangeTracker().MarkSprimDirty(get_background_texture_material(), HdMaterial::DirtyBits::DirtyResource);
    if (ri.HasRprim(bg_texture_path))
        ri.GetChangeTracker().MarkRprimDirty(bg_texture_path,
                                             HdChangeTracker::RprimDirtyBits::DirtyPoints | HdChangeTracker::RprimDirtyBits::DirtyPrimvar);
    else
        ri.InsertRprim(HdPrimTypeTokens->mesh, this, bg_texture_path);
}

void OPENDCC_NAMESPACE::UVSceneDelegate::reload_current_texture()
{
    GetRenderIndex().GetChangeTracker().MarkSprimDirty(get_background_texture_material(), HdMaterial::AllDirty);
}

HdReprSelector UVSceneDelegate::GetReprSelector(SdfPath const& id)
{
    if (id == get_background_texture_prim() || m_mode == RenderMode::Hull)
    {
        return HdReprSelector(HdReprTokens->hull);
    }

    const auto& app = Application::instance();
    const auto is_soft_selection_enabled = app.is_soft_selection_enabled();
    const auto selection_mode = app.get_selection_mode();
    const auto show_points =
        selection_mode == Application::SelectionMode::POINTS || selection_mode == Application::SelectionMode::UV ||
        (is_soft_selection_enabled && (selection_mode == Application::SelectionMode::EDGES || selection_mode == Application::SelectionMode::FACES));

    HdReprSelector repr_selector;
    if (show_points)
    {
        repr_selector = HdReprSelector(HdReprTokens->refinedWire, HdReprTokens->wire, HdReprTokens->points);
    }
    else
    {
        repr_selector = HdReprSelector(HdReprTokens->refinedWire, HdReprTokens->wire);
    }
    return repr_selector;
}

VtValue UVSceneDelegate::GetMaterialResource(SdfPath const& material_id)
{
    const auto bg_texture_mat = get_background_texture_material();
    const auto bg_texture = material_id == bg_texture_mat;
    if (bg_texture && !m_texture_file.empty())
    {
        const auto bg_texture = get_background_texture_prim();
        static auto texture_node_id = get_texture_node_id();
        return VtValue(get_background_texture_material_network(bg_texture, texture_node_id, bg_texture_mat, m_texture_file));
    }
    else
    {
        return !bg_texture ? VtValue(get_mesh_material_network(material_id)) : ViewportSceneDelegate::GetMaterialResource(material_id);
    }
}

SdfPath UVSceneDelegate::GetMaterialId(SdfPath const& rprimId)
{
    return rprimId == get_background_texture_prim() ? get_background_texture_material() : rprimId.AppendProperty(HdPrimTypeTokens->material);
}

VtValue UVSceneDelegate::Get(SdfPath const& id, TfToken const& key)
{
    VtValue result;
    if (id == get_background_texture_prim())
    {
        VtVec3fArray points;
        VtVec2fArray uvs;
        if (m_tiling_mode == TilingMode::UDIM && is_supported_udim_texture(m_texture_file))
        {
            points = VtVec3fArray { GfVec3f(0, 0, -1), GfVec3f(10, 0, -1), GfVec3f(10, 10, -1), GfVec3f(0, 10, -1) };
            uvs = VtVec2fArray { GfVec2f(0.0), GfVec2f(10, 0), GfVec2f(10, 10), GfVec2f(0, 10) };
        }
        else
        {
            points = VtVec3fArray { GfVec3f(0, 0, -1), GfVec3f(1, 0, -1), GfVec3f(1, 1, -1), GfVec3f(0, 1, -1) };
            uvs = VtVec2fArray { GfVec2f(0.0), GfVec2f(1, 0), GfVec2f(1, 1), GfVec2f(0, 1) };
        }
        if (key == HdTokens->points)
            result = points;
        else if (key == TfToken("st"))
            result = uvs;
        return result;
    }

    if (key == HdTokens->points)
    {
        if (auto mesh = UsdGeomMesh(m_stage->GetPrimAtPath(convert_index_path_to_stage_path(id))))
        {
            UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
            auto primvar = primvars_api.GetPrimvar(m_uv_primvar);
            VtVec2fArray st;
            if (auto res = primvar.Get(&st, m_time))
            {
                VtVec3fArray points(st.size());
                std::transform(st.begin(), st.end(), points.begin(), [](const GfVec2f& st) { return GfVec3f(st[0], st[1], 0); });
                result = points;
            }
        }
    }

    return result;
}

HdPrimvarDescriptorVector UVSceneDelegate::GetPrimvarDescriptors(SdfPath const& id, HdInterpolation interpolation)
{
    HdPrimvarDescriptorVector primvars;
    if (interpolation == HdInterpolationVarying && id == get_background_texture_prim())
    {
        primvars.emplace_back(TfToken("st"), interpolation, HdPrimvarRoleTokens->textureCoordinate);
    }

    if (interpolation == HdInterpolationVertex)
    {
        primvars.emplace_back(HdTokens->points, interpolation, HdPrimvarRoleTokens->point);
    }

    return primvars;
}

HdDisplayStyle UVSceneDelegate::GetDisplayStyle(SdfPath const& id)
{
    return HdDisplayStyle();
}

HdCullStyle UVSceneDelegate::GetCullStyle(SdfPath const& id)
{
    return HdCullStyle::HdCullStyleBackUnlessDoubleSided;
}

bool UVSceneDelegate::GetDoubleSided(SdfPath const& id)
{
    return true;
}

bool UVSceneDelegate::GetVisible(SdfPath const& id)
{
    const auto texture = get_background_texture_prim();
    return !(m_mode == RenderMode::Wire && id == texture);
}

GfMatrix4d OPENDCC_NAMESPACE::UVSceneDelegate::GetTransform(SdfPath const& id)
{
    return GfMatrix4d(1);
}

GfRange3d UVSceneDelegate::GetExtent(SdfPath const& id)
{
    if (id == get_background_texture_prim())
        return m_tiling_mode == TilingMode::NONE ? GfRange3d(GfVec3d(0), GfVec3d(1)) : GfRange3d(GfVec3d(0), GfVec3d(10));

    if (auto result = TfMapLookupPtr(m_prims_info, convert_index_path_to_stage_path(id)))
        return result->range;
    return GfRange3d(GfVec3d(0), GfVec3d(1));
}

HdMeshTopology UVSceneDelegate::GetMeshTopology(SdfPath const& id)
{
    if (id == get_background_texture_prim())
        return HdMeshTopology(HdTokens->linear, UsdGeomTokens->rightHanded, { 4 }, { 0, 1, 2, 3 });

    return m_prims_info.at(convert_index_path_to_stage_path(id)).topology;
}

void UVSceneDelegate::update(const ViewportHydraEngineParams& engine_params)
{
    const auto uv_primvar = engine_params.user_data.at("uv.uv_primvar").Get<TfToken>();
    const auto populated_paths = engine_params.user_data.at("uv.prim_paths").Get<SdfPathVector>();
    m_prims_info = engine_params.user_data.at("uv.prims_info").Get<PrimInfoMap>();
    if (!m_is_initialized || m_stage != Application::instance().get_session()->get_current_stage() || m_uv_primvar != uv_primvar ||
        m_highlighted_paths != populated_paths)
    {
        m_is_initialized = initialize(populated_paths, uv_primvar);
    }
    if (m_time != Application::instance().get_current_time())
    {
        m_time = Application::instance().get_current_time();
        // TODO:
        // mark time-varying primvars dirty
    }

    m_mode = engine_params.user_data.at("uv.render_mode").Get<TfToken>() == TfToken("hull") ? RenderMode::Hull : RenderMode::Wire;

    auto& index = GetRenderIndex();
    auto& tracker = index.GetChangeTracker();

    const auto texture = get_background_texture_prim();
    if (index.HasRprim(texture))
    {
        tracker.MarkRprimDirty(texture, HdChangeTracker::DirtyVisibility);
    }

    for (const auto& rprim : index.GetRprimIds())
    {
        if (rprim != texture)
        {
            tracker.MarkSprimDirty(rprim.AppendProperty(HdPrimTypeTokens->material), HdMaterial::DirtyBits::DirtyParams);
            tracker.MarkRprimDirty(rprim, HdChangeTracker::DirtyRepr);
        }
    }

    bool texture_is_dirty = false;
    const auto show_texture = TfMapLookupByValue(engine_params.user_data, "uv.show_texture", VtValue(false)).UncheckedGet<bool>();
    if (m_show_texture != show_texture)
    {
        m_show_texture = show_texture;
        texture_is_dirty = true;
    }

    if (m_show_texture)
    {
        const auto tiling_mode_token =
            TfMapLookupByValue(engine_params.user_data, "uv.tiling_mode", VtValue(TfToken("none"))).UncheckedGet<TfToken>();
        TilingMode tiling_mode;

        if (tiling_mode_token == TfToken("udim"))
            tiling_mode = TilingMode::UDIM;
        else
            tiling_mode = TilingMode::NONE;

        if (tiling_mode != m_tiling_mode)
        {
            m_tiling_mode = tiling_mode;
            texture_is_dirty = true;
        }

        const auto texture_file = TfMapLookupByValue(engine_params.user_data, "uv.texture_file", VtValue(std::string())).UncheckedGet<std::string>();
        if (texture_file != m_texture_file)
        {
            m_texture_file = texture_file;
            texture_is_dirty = true;
        }
    }

    if (texture_is_dirty)
        update_background_texture(m_texture_file);
    const auto force_reload_texture = TfMapLookupByValue(engine_params.user_data, "uv.force_reload_texture", VtValue(false)).UncheckedGet<bool>();
    if (force_reload_texture)
        reload_current_texture();
}

void UVSceneDelegate::populate_selection(const SelectionList& selection_list, const HdSelectionSharedPtr& result)
{
    if (m_selection_mode == HdSelection::HighlightMode::HighlightModeLocate)
    {
        return;
    }

    const auto& app = Application::instance();
    const auto point = app.get_selection_mode() == Application::SelectionMode::POINTS;
    const auto uv = app.get_selection_mode() == Application::SelectionMode::UV;
    const auto show_points = point || uv;

    for (const auto& prim : selection_list)
    {
        const auto& path = prim.first;
        const auto& selection_data = prim.second;
        const auto converted_path = convert_stage_path_to_index_path(path);
        if (!GetRenderIndex().HasRprim(converted_path))
            continue;

        const auto& points = selection_data.get_point_indices();
        const auto& edges = selection_data.get_edge_indices();
        const auto& elements = selection_data.get_element_index_intervals();
        auto& pi = m_prims_info[path];
        if (!points.empty() && show_points)
        {
            if (uv)
            {
                const auto& points = selection_data.get_point_index_intervals();
                result->AddPoints(m_selection_mode, converted_path, points.flatten<VtIntArray>());
            }
            else
            {
                for (const auto i : points)
                {
                    result->AddPoints(m_selection_mode, converted_path, pi.mesh_vertices_to_uv_vertices[i]);
                }
            }
            GetRenderIndex().GetChangeTracker().MarkRprimDirty(converted_path, HdChangeTracker::DirtyRepr);
        }
        if (!edges.empty())
        {
            for (const auto i : edges)
            {
                result->AddEdges(m_selection_mode, converted_path, pi.mesh_edges_to_uv_edges[i]);
            }
            GetRenderIndex().GetChangeTracker().MarkRprimDirty(converted_path, HdChangeTracker::DirtyRepr);
        }
        if (!elements.empty())
        {
            result->AddElements(m_selection_mode, converted_path, elements.flatten<VtIntArray>());
            GetRenderIndex().GetChangeTracker().MarkRprimDirty(converted_path, HdChangeTracker::DirtyRepr);
        }
    }
    for (auto& prim_info : m_prims_info)
    {
        if (!selection_list.contains(prim_info.first) || (selection_list.get_selection_data(prim_info.first).get_point_indices().empty()))
        {
            GetRenderIndex().GetChangeTracker().MarkRprimDirty(convert_stage_path_to_index_path(prim_info.first), HdChangeTracker::DirtyRepr);
        }
    }
}

HdMaterialNetworkMap UVSceneDelegate::get_mesh_material_network(PXR_NS::SdfPath prim_path)
{
    const auto mesh_node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(s_mesh_source, HioGlslfxTokens->glslfx, {});
    const auto& mesh_source_id = mesh_node->GetIdentifier();

    HdMaterialNetworkMap material_network_map;
    material_network_map.terminals.push_back(prim_path.GetPrimPath());

    HdMaterialNode mesh_shader_node = { prim_path, mesh_source_id };

    const auto mode = Application::instance().get_selection_mode();
    mesh_shader_node.parameters[TfToken("selection_mode")] = VtValue(static_cast<int>(mode));
    mesh_shader_node.parameters[TfToken("render_mode")] = VtValue(static_cast<int>(m_mode));

    HdMaterialNetwork& surface = material_network_map.map[HdMaterialTerminalTokens->surface];
    surface.nodes.push_back(mesh_shader_node);

    return material_network_map;
}

OPENDCC_NAMESPACE_CLOSE
