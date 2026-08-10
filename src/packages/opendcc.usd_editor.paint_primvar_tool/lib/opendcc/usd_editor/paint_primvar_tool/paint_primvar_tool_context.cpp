// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

// workaround
#include "opendcc/base/qt_python.h"

#include <QMouseEvent>
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include <pxr/pxr.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/camera.h>
#include "pxr/usd/usdGeom/mesh.h"

#include <pxr/imaging/cameraUtil/conformWindow.h>

#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_view.h"
#include "paint_primvar_tool_context.h"
#include "mesh_manipulation_data.h"

#include "opendcc/app/viewport/prim_material_override.h"
#if PXR_VERSION >= 2002
#include <pxr/usd/sdr/registry.h>
#include <pxr/imaging/hd/material.h>
#endif
#include "pxr/usd/usdGeom/primvarsAPI.h"
#include "pxr/usd/sdr/shaderMetadataHelpers.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    static int points_in_unit_radius = 50;
}

struct MeshData : public MeshManipulationData
{
    PrimvarType type = PrimvarType::None;
    VtVec3fArray prev_values_vec3f;
    VtFloatArray prev_values_float;
    PaintPrimvarToolContext::Properties draw_properties;

    std::vector<TfToken> primvars_names;
    size_t current_primvar_idx = 0;

    MeshData(const UsdGeomMesh& in_mesh, const PaintPrimvarToolContext::Properties& properties, bool& success)
        : MeshManipulationData(in_mesh, success)
    {
        if (!success) // MeshManipulationData constructor is fail
            return;
        draw_properties = properties;
        UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
        auto primvars = primvars_api.GetPrimvars();
        for (UsdGeomPrimvar& primvar : primvars)
        {
            if (primvar.GetTypeName() == SdfValueTypeNames->FloatArray || primvar.GetTypeName() == SdfValueTypeNames->Float3Array ||
                primvar.GetTypeName() == SdfValueTypeNames->Color3fArray)
                primvars_names.push_back(primvar.GetBaseName());
        }
        if (primvars_names.size() > 0)
            set_current_primvar_idx(0);
    }

    void update_buffers()
    {
        UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
        auto primvar = primvars_api.GetPrimvar(primvars_names[current_primvar_idx]);
        if (!primvar)
            return;
        prev_values_float.resize(0);
        prev_values_vec3f.resize(0);
        if (primvar.GetTypeName() == SdfValueTypeNames->FloatArray)
        {
            primvar.Get(&prev_values_float);
            auto prve_size = prev_values_float.size();
            prev_values_float.resize(points.size());
            for (auto i = prve_size; i < points.size(); ++i)
                prev_values_float[i] = 0;
            type = PrimvarType::Float;
        }
        else
        {
            primvar.Get(&prev_values_vec3f);
            auto prve_size = prev_values_vec3f.size();
            prev_values_vec3f.resize(points.size());
            for (auto i = prve_size; i < points.size(); ++i)
                prev_values_vec3f[i] = GfVec3f(0);
            type = PrimvarType::Vec3f;
        }
    }

    void set_current_primvar_idx(size_t primvar_index)
    {
        if (primvars_names.size() == 0 || primvar_index >= primvars_names.size())
            return;
        current_primvar_idx = primvar_index;

        type = PrimvarType::None;
        update_buffers();
    }
    void on_start() { update_buffers(); }
    void on_finish()
    {
        undo_block.reset();
        std::fill(scales.begin(), scales.end(), empty_scale);
        set_current_primvar_idx(current_primvar_idx);
    }
};

void PaintPrimvarToolContext::draw_in_mesh()
{
    if (!m_is_intersect || !m_mesh_data || (m_properties.radius < 0.01f) || m_mesh_data->type == PrimvarType::None)
        return;

    auto indices = m_mesh_data->bvh.get_points_in_radius(m_p, m_mesh_data->mesh.GetPath(), m_properties.radius);
    const VtVec3fArray& points = m_mesh_data->points;
    const VtVec3fArray& normals = m_mesh_data->normals;
    UsdGeomXformCache xform_cache(Application::instance().get_current_time());
    auto local_to_world = xform_cache.GetLocalToWorldTransform(m_mesh_data->mesh.GetPrim());
    VtVec3fArray next_values_vec3f = m_mesh_data->prev_values_vec3f;
    VtFloatArray next_values_float = m_mesh_data->prev_values_float;
    VtVec3fArray& prev_values_vec3f = m_mesh_data->prev_values_vec3f;
    VtFloatArray& prev_values_float = m_mesh_data->prev_values_float;
    const VtIntArray& adjacency_table = m_mesh_data->adjacency.GetAdjacencyTable();

    if (m_properties.mode == Mode::Smooth)
    {
        if (m_mesh_data->type == PrimvarType::Vec3f)
            next_values_vec3f.resize(prev_values_vec3f.size());
        else
            next_values_float.resize(prev_values_float.size());
    }

    if (indices.size() > 0)
    {
        auto inv_r = 1.0f / m_properties.radius;
        auto& scales = m_mesh_data->scales;
        std::unordered_set<int> sciped_indices;
        for (size_t i : indices)
        {
            if (GfDot(normals[i], m_n) < 0)
            {
                sciped_indices.insert(i);
                continue;
            }

            auto falloff = falloff_function(properties().falloff, (local_to_world.Transform(points[i]) - m_p).GetLength() * inv_r);

            if (falloff <= 0 || falloff > 1)
            {
                sciped_indices.insert(i);
                continue;
            }

            if (m_properties.mode == Mode::Smooth)
            {
                int offsetIdx = i * 2;
                int offset = adjacency_table[offsetIdx];
                int valence = adjacency_table[offsetIdx + 1];
                const int* e = &adjacency_table[offset];
                if (m_mesh_data->type == PrimvarType::Vec3f)
                {
                    GfVec3f S(prev_values_vec3f[i]);
                    for (int j = 0; j < valence; ++j)
                        S += prev_values_vec3f[e[j * 2]];
                    next_values_vec3f[i] = prev_values_vec3f[i] * (1 - falloff) + falloff * S / (valence + 1);
                }
                else
                {
                    float S(prev_values_float[i]);
                    for (int j = 0; j < valence; ++j)
                        S += prev_values_float[e[j * 2]];
                    next_values_float[i] = prev_values_float[i] * (1 - falloff) + falloff * S / (valence + 1);
                }
            }
            else
            {
                if (scales[i] == MeshData::empty_scale)
                    scales[i] = falloff;
                else
                    scales[i] += falloff;

                if (m_properties.mode != Mode::Add)
                    scales[i] = std::min(scales[i], 1.0f);
            }
        }

        if (m_properties.mode == Mode::Smooth)
        {
            if (m_mesh_data->type == PrimvarType::Vec3f)
                m_mesh_data->prev_values_vec3f = next_values_vec3f;
            else
                m_mesh_data->prev_values_float = next_values_float;
        }

        if (indices.size() > 0)
        {
            if (!m_mesh_data->undo_block)
                m_mesh_data->undo_block = std::make_unique<commands::UsdEditsUndoBlock>();
            UsdGeomPrimvarsAPI primvars_api(m_mesh_data->mesh.GetPrim());
            if (!primvars_api)
                return;
            auto primvar = primvars_api.GetPrimvar(m_mesh_data->primvars_names[m_mesh_data->current_primvar_idx]);
            TfToken interpolation;
            primvar.GetAttr().GetMetadata(UsdGeomTokens->interpolation, &interpolation);
            if (interpolation != UsdGeomTokens->vertex)
            {
                primvar.GetAttr().SetMetadata(UsdGeomTokens->interpolation, UsdGeomTokens->vertex);
            }
            if (m_mesh_data->draw_properties.mode != PaintPrimvarToolContext::Mode::Smooth)
            {
                for (size_t i : indices)
                {
                    if (sciped_indices.find(i) != sciped_indices.end())
                        continue;
                    if (m_mesh_data->type == PrimvarType::Vec3f)
                    {
                        if (m_mesh_data->draw_properties.mode == PaintPrimvarToolContext::Mode::Add)
                            prev_values_vec3f[i] += m_mesh_data->draw_properties.vec3f_value * scales[i];
                        else
                            prev_values_vec3f[i] =
                                prev_values_vec3f[i] * std::max(0.0f, (1.0f - scales[i])) + m_mesh_data->draw_properties.vec3f_value * scales[i];
                    }
                    else if (m_mesh_data->type == PrimvarType::Float)
                    {
                        if (m_mesh_data->draw_properties.mode == PaintPrimvarToolContext::Mode::Add)
                            prev_values_float[i] += m_mesh_data->draw_properties.float_value * scales[i];
                        else
                            prev_values_float[i] =
                                prev_values_float[i] * std::max(0.0f, (1.0f - scales[i])) + m_mesh_data->draw_properties.float_value * scales[i];
                    }
                }
            }
            if (m_mesh_data->type == PrimvarType::Vec3f)
                primvar.Set(prev_values_vec3f);
            else if (m_mesh_data->type == PrimvarType::Float)
                primvar.Set(prev_values_float);
        }
    }
}

void PaintPrimvarToolContext::update_context()
{
    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
    {
        m_mesh_data.reset();
        return;
    }

    if (m_mesh_data)
        m_prim_material_override->clear_override(m_mesh_data->mesh.GetPath());
    m_mesh_data.reset();

    const SelectionList& selection_list = Application::instance().get_selection();
    if (selection_list.empty())
    {
        m_on_mesh_changed();
        return;
    }

    else if (selection_list.fully_selected_paths_size() > 1)
    {
        OPENDCC_WARN("Multiple Selection");
        m_on_mesh_changed();
        return;
    }

    SdfPath prim_path = selection_list.begin()->first;
    UsdPrim prim = stage->GetPrimAtPath(prim_path);

    if (prim && prim.IsA<UsdGeomMesh>())
    {
        bool ok = false;
        m_mesh_data = std::make_unique<MeshData>(UsdGeomMesh(prim), properties(), ok);
        if (ok)
            m_prim_material_override->assign_material(m_primvar_material_id, prim.GetPrimPath());
        else
            m_mesh_data.reset();
    }
    m_on_mesh_changed();
}

GfMatrix4f get_vp_matrix(ViewportGLWidget* viewport)
{
    GfCamera camera = viewport->get_camera();
    GfFrustum frustum = camera.GetFrustum();
    GfVec4d viewport_resolution(0, 0, viewport->width(), viewport->height());
    CameraUtilConformWindow(&frustum, CameraUtilConformWindowPolicy::CameraUtilFit,
                            viewport_resolution[3] != 0.0 ? viewport_resolution[2] / viewport_resolution[3] : 1.0);

    GfMatrix4d m = frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix();

    return GfMatrix4f(m);
}

PaintPrimvarToolContext::PaintPrimvarToolContext()
{
    m_selection_event_hndl = Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] { update_context(); });
    m_properties.read_from_settings(settings_prefix());
    m_prim_material_override = std::make_shared<PrimMaterialOverride>();
#if PXR_VERSION < 2002
    m_primvar_material_id = m_prim_material_override->insert_material(PrimMaterialDescriptor("", {}));
#else
    m_primvar_material_id = m_prim_material_override->insert_material(PrimMaterialDescriptor(VtValue(), {}));
#endif
    update_context();

    m_cursor = new QCursor(QPixmap(":/icons/cursor_crosshair"), -10, -10);
}

PaintPrimvarToolContext::~PaintPrimvarToolContext()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_event_hndl);
    delete m_cursor;
}

bool PaintPrimvarToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    is_b_key_pressed = (key_event->key() == Qt::Key_B);
    if (m_mesh_data)
        m_mesh_data->on_start();
    return is_b_key_pressed;
}

bool PaintPrimvarToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    is_b_key_pressed = false;
    if (m_mesh_data)
        m_mesh_data->on_finish();
    return (key_event->key() == Qt::Key_B);
}

void PaintPrimvarToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view || !m_is_intersect)
        return;

    float up_shift = 0.03f;
    float R = m_properties.radius;
    int N = points_in_unit_radius * std::ceil(R);

    GfVec3f p(m_p);
    GfVec3f n(m_n);

    GfVec3f e = GfVec3f(1, 0, 0);

    if (std::abs(GfDot(e, n)) > 0.8f)
        e = GfVec3f(0, 1, 0);

    const GfVec3f x_axis = GfCross(e, n).GetNormalized();
    const GfVec3f y_axis = GfCross(n, x_axis).GetNormalized();

    std::vector<GfVec3f> points(N + 1);
    for (int i = 0; i <= N; ++i)
    {
        points[i] = p + R * x_axis * cos((2 * M_PI * i) / N) + R * y_axis * sin((2 * M_PI * i) / N) + up_shift * n;
    }

    auto active_view = ApplicationUI::instance().get_active_view();
    if (!active_view)
        return;

    auto viewport = active_view->get_gl_widget();
    GfMatrix4f mf = get_vp_matrix(viewport);

    draw_manager->begin_drawable();
    draw_manager->set_mvp_matrix(mf);
    draw_manager->mesh(ViewportUiDrawManager::PrimitiveTypeLinesStrip, points);
    draw_manager->end_drawable();

    draw_manager->begin_drawable();
    draw_manager->set_mvp_matrix(mf);
    draw_manager->set_prim_type(ViewportUiDrawManager::PrimitiveTypeLines);
    float half_R = R / 2;
    draw_manager->line(p, p + n * half_R);
    draw_manager->end_drawable();
}

PXR_NS::TfToken PaintPrimvarToolContext::get_name() const
{
    return TfToken("PaintPrimvar");
}

void PaintPrimvarToolContext::set_properties(const Properties& properties)
{
    m_properties = properties;
    if (m_mesh_data)
        m_mesh_data->draw_properties = properties;
    m_properties.write_to_settings(settings_prefix());
}

const std::vector<TfToken>& PaintPrimvarToolContext::primvars_names() const
{
    static std::vector<PXR_NS::TfToken> empty;
    if (m_mesh_data)
        return m_mesh_data->primvars_names;
    else
        return empty;
}

void PaintPrimvarToolContext::set_primvar_index(size_t idx)
{
    if (!m_mesh_data)
        return;
    m_mesh_data->set_current_primvar_idx(idx);

    const auto primvar_name = m_mesh_data->primvars_names[m_mesh_data->current_primvar_idx].GetString();
    std::string frag_src;
    if (m_mesh_data->type == PrimvarType::Float)
    {
        frag_src = R"#(vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
{
#ifdef HD_HAS_)#" + primvar_name +
                   R"#(
    float value = HdGet_)#" +
                   primvar_name + R"#(().r;
    return vec4(vec3(value), 1);
#else
    return vec4(0, 0, 0, 1);
#endif
}
)#";
    }
    else if (m_mesh_data->type == PrimvarType::Vec3f)
    {
        frag_src = R"#(vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
{
#ifdef HD_HAS_)#" + primvar_name +
                   R"#(
    vec3 value = HdGet_)#" +
                   primvar_name + R"#(().rgb;
    return vec4(value, 1);
#else
    return vec4(0, 0, 0, 1);
#endif
}
)#";
    }

#if PXR_VERSION < 2002
    const auto mat_descr = PrimMaterialDescriptor(
        frag_src,
        { { HdInterpolationVertex, { HdPrimvarDescriptor(m_mesh_data->primvars_names[m_mesh_data->current_primvar_idx], HdInterpolationVertex) } } });
#else
    std::string frag_header = R"#(-- glslfx version 0.1
-- configuration
{
    "techniques": {
        "default": {
            "surfaceShader": {
                "source": [ "primvar" ]
            }
        }
    }
}

--- --------------------------------------------------------------------------
-- glsl primvar

)#";

    frag_src = frag_header + frag_src;
#if PXR_VERSION >= 2508
    SdrTokenMap shader_metadata = { { SdrNodeMetadata->Primvars, ShaderMetadataHelpers::CreateStringFromStringVec(SdrStringVec { primvar_name }) } };
#else
    NdrTokenMap shader_metadata = { { SdrNodeMetadata->Primvars, ShaderMetadataHelpers::CreateStringFromStringVec(NdrStringVec { primvar_name }) } };
#endif
    auto shader_node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(frag_src, TfToken("glslfx"), shader_metadata);
    HdMaterialNetworkMap material_network_map;
    material_network_map.terminals.push_back(m_mesh_data->mesh.GetPath());

    auto& network = material_network_map.map[HdMaterialTerminalTokens->surface];
    HdMaterialNode mat_node = { m_mesh_data->mesh.GetPath(), shader_node->GetIdentifier() };
    network.nodes.push_back(mat_node);
    network.primvars.push_back(TfToken(primvar_name));
    const auto mat_descr =
        PrimMaterialDescriptor(VtValue(material_network_map), { { HdInterpolationVertex,
                                                                  { HdPrimvarDescriptor(m_mesh_data->primvars_names[m_mesh_data->current_primvar_idx],
                                                                                        HdInterpolationVertex, HdPrimvarRoleTokens->color) } } });
#endif
    m_prim_material_override->update_material(m_primvar_material_id, mat_descr);
}

PrimvarType PaintPrimvarToolContext::get_primvar_type()
{
    if (m_mesh_data)
        return m_mesh_data->type;
    else
        return PrimvarType::None;
}

std::shared_ptr<PrimMaterialOverride> PaintPrimvarToolContext::get_prim_material_override()
{
    return m_prim_material_override;
}

bool PaintPrimvarToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                             ViewportUiDrawManager* draw_manager)
{
    if (!m_mesh_data)
    {
        OPENDCC_WARN("No Selected PaintPrimvar");
        return false;
    }
    m_mesh_data->draw_properties = properties();

    if (is_b_key_pressed)
    {
        m_start_radius = m_properties.radius;
        m_start_x = mouse_event.x();
        is_adjust_radius = true;
        return true;
    }

    on_mouse_move(mouse_event, viewport_view, draw_manager);

    if (!m_is_intersect)
        return false;

    return true;
}

bool PaintPrimvarToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                            ViewportUiDrawManager* draw_manager)
{
    if (!m_mesh_data)
    {
        m_is_intersect = false;
        return false;
    }

    if (is_adjust_radius)
    {
        auto x = mouse_event.x();
        float distance = x - m_start_x;
        if (distance >= 0.0f)
        {
            m_properties.radius = m_start_radius + distance / points_in_unit_radius;
        }
        else
        {
            float mult = (points_in_unit_radius - std::min(float(points_in_unit_radius), std::abs(distance))) / points_in_unit_radius;
            m_properties.radius = m_start_radius * mult;
        }
        m_properties.radius = std::max(m_properties.radius, 0.1f);
        draw(viewport_view, draw_manager);
        Application::instance().get_settings()->set(settings_prefix() + ".radius", m_properties.radius);
        m_mesh_data->draw_properties = m_properties;
        return true;
    }

    auto custom_collection =
        HdRprimCollection(HdTokens->geometry, HdReprSelector(HdReprTokens->refined, HdReprTokens->hull), SdfPath::AbsoluteRootPath());

    auto intersect = viewport_view->intersect(GfVec2f(mouse_event.x(), mouse_event.y()), SelectionList::SelectionFlags::FULL_SELECTION, true,
                                              &custom_collection, { HdTokens->geometry });
    m_is_intersect = intersect.second;
    if (intersect.second)
    {
        if (m_mesh_data->mesh.GetPrim().GetName() == intersect.first.front().objectId.GetName())
        {
            m_p = GfVec3f(intersect.first.front().worldSpaceHitPoint);
            m_n = GfVec3f(intersect.first.front().worldSpaceHitNormal);
        }
        else
        {
            m_is_intersect = false;
        }
    }

    if (mouse_event.buttons() != Qt::LeftButton || mouse_event.modifiers() != Qt::NoModifier)
        return true;

    draw_in_mesh();

    return true;
}

bool PaintPrimvarToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                               ViewportUiDrawManager* draw_manager)
{
    is_adjust_radius = false;
    if (m_mesh_data)
        m_mesh_data->on_finish();
    return true;
}

void PaintPrimvarToolContext::Properties::read_from_settings(const std::string& prefix)
{
    auto settings = Application::instance().get_settings();
    radius = settings->get(prefix + ".radius", 1.0f);
    float_value = settings->get(prefix + ".float_value", 1.0f);
    vec3f_value[0] = settings->get(prefix + ".vec3f_value_x", 1.0f);
    vec3f_value[1] = settings->get(prefix + ".vec3f_value_y", 1.0f);
    vec3f_value[2] = settings->get(prefix + ".vec3f_value_z", 1.0f);
    falloff = settings->get(prefix + ".falloff", 0.3f);
    mode = static_cast<Mode>(settings->get(prefix + ".mode", 0));
}

void PaintPrimvarToolContext::Properties::write_to_settings(const std::string& prefix) const
{
    auto settings = Application::instance().get_settings();
    settings->set(prefix + ".radius", radius);
    settings->set(prefix + ".float_value", float_value);
    settings->set(prefix + ".vec3f_value_x", vec3f_value[0]);
    settings->set(prefix + ".vec3f_value_y", vec3f_value[1]);
    settings->set(prefix + ".vec3f_value_z", vec3f_value[2]);
    settings->set(prefix + ".falloff", falloff);
    settings->set(prefix + ".mode", (int)mode);
}

OPENDCC_NAMESPACE_CLOSE
