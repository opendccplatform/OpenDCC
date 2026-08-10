// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/texture_paint/texture_paint_tool_context.h"
#include "opendcc/app/viewport/prim_material_override.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_engine_proxy.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "pxr/imaging/hgi/blitCmdsOps.h"
#include "pxr/imaging/hdSt/textureUtils.h"
#include "pxr/imaging/hdx/hgiConversions.h"
#include "opendcc/app/viewport/viewport_view.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/base/gf/matrix4f.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "pxr/base/gf/line2d.h"
#include <iostream>
#include <tbb/task_group.h>
#include "opendcc/ui/common_widgets/ramp.h"
#include <QCursor>
#include <mutex>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <igl/boundary_loop.h>
#include "opendcc/app/core/topology_cache.h"
#include <pxr/usd/ar/resolver.h>
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/app/viewport/texture_plugin.h"
#include "opendcc/usd_editor/texture_paint/brush_properties.h"
#include "opendcc/usd_editor/texture_paint/texture_paint_stroke_command.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE
using namespace OIIO;
namespace
{
    const std::string s_texture_source =
        R"#(-- glslfx version 0.1
-- configuration
{
    "textures": {
        "texture" : {
            "documentation" : "Painted texture"
        }
     },
    "techniques": {
        "default": {
            "surfaceShader": {
                "source": [ "painted_texture" ]
            }
        }
    }
}
--- --------------------------------------------------------------------------
-- glsl painted_texture

	vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
	{
    #ifdef HD_HAS_texture
        vec4 result = vec4(HdGet_texture().xyzw);
    #else
        vec4 result = vec4(1, 0, 1, 1);
    #endif
        return vec4(FallbackLighting(Peye.xyz, Neye, result.xyz), result.w);
	}
)#";

    TfToken get_texture_shader_id()
    {
        static const auto node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(s_texture_source, TfToken("glslfx"), {});
        return node ? node->GetIdentifier() : TfToken();
    }

};

TexturePaintToolContext::TexturePaintToolContext()
    : IViewportToolContext()
{
    m_brush_properties = std::make_shared<BrushProperties>();
    const auto prim_sel = Application::instance().get_prim_selection();
    if (prim_sel.empty())
        return;

    auto stage = Application::instance().get_session()->get_current_stage();
    auto prim = stage->GetPrimAtPath(prim_sel[0]);
    if (!prim)
        return;
    auto mesh = UsdGeomMesh(prim);
    if (!mesh)
        return;

    m_painted_mesh_path = prim_sel[0];
    m_current_stage_changed = Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this] { reset(); });
    m_stage_changed = std::make_unique<StageObjectChangedWatcher>(stage, [this, stage](UsdNotice::ObjectsChanged const& notice) {
        auto prim = stage->GetPrimAtPath(m_painted_mesh_path);
        if (!prim || !UsdGeomMesh(prim))
            reset();
    });

    m_material_override = std::make_shared<PrimMaterialOverride>();
    auto settings = Application::instance().get_settings();
    const auto last_selected_prim = settings->get<std::string>(settings_prefix() + ".last_selected_prim", "");
    if (last_selected_prim == m_painted_mesh_path.GetString())
    {
        set_texture_file(settings->get<std::string>(settings_prefix() + ".texture_file", ""));
    }
    else
    {
        settings->set(settings_prefix() + ".last_selected_prim", m_painted_mesh_path.GetString());
        settings->set(settings_prefix() + ".texture_file", std::string(""));
    }
    set_material();
}

TexturePaintToolContext::~TexturePaintToolContext()
{
    reset();
}

void TexturePaintToolContext::enable_writing_to_file(bool enable)
{
    m_writing_to_file = enable;
}

void TexturePaintToolContext::set_occlude(bool occlude)
{
    if (m_occlude != occlude)
        m_occlude = occlude;
}

void TexturePaintToolContext::update_material()
{
    if (m_material_override && m_material_descr && m_material_id != -1)
    {
        m_material_override->update_material(m_material_id, *m_material_descr);
    }
}

unsigned int TexturePaintToolContext::m_bake_textures_counter = 0;

void TexturePaintToolContext::bake_textures()
{
    if (!m_texture_data)
        return;
    m_texture_data->flush();

    auto settings = Application::instance().get_settings();
    settings->set("session.texture_paint_baked_texture", m_texture_data->get_texture_filename().c_str());
    settings->set("session.texture_paint_baked_texture_counter",
                  (int)m_bake_textures_counter); // because session.texture_paint_baked_texture doesn't emit when not changed

    m_bake_textures_counter++;
}

void TexturePaintToolContext::push_command()
{
    if (!m_texture_data)
        return;

    auto cmd = std::make_shared<TexturePaintStrokeCommand>(*m_texture_data.get(), *m_texture_painter.get(), m_writing_to_file);
    CommandInterface::finalize(cmd);
}

bool TexturePaintToolContext::init_paint_data(const ViewportViewPtr& viewport_view)
{
    const auto selected_prim = Application::instance().get_session()->get_current_stage()->GetPrimAtPath(m_painted_mesh_path);
    const auto selected_mesh = UsdGeomMesh(selected_prim);
    if (!selected_mesh)
        return false;

    m_texture_data->invalidate();
    m_texture_painter =
        std::make_unique<TexturePainter>(viewport_view, selected_mesh, m_mouse_x, m_mouse_y, m_brush_properties, m_texture_data, m_occlude);

    return m_texture_data->is_valid();
}

const std::string& TexturePaintToolContext::get_texture_file() const
{
    static const std::string empty;
    return m_texture_data ? m_texture_data->get_texture_filename() : empty;
}

bool TexturePaintToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (key_event->key() == Qt::Key_B)
    {
        // If drawing right now
        if (m_texture_painter)
            return true;
        if (key_event->isAutoRepeat())
            return true;

        m_radius_editable = false;
        return true;
    }
    return false;
}

bool TexturePaintToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (key_event->key() == Qt::Key_B)
    {
        // If drawing right now
        if (m_texture_painter)
            return true;
        if (!key_event->isAutoRepeat())
            m_radius_editable = true;
    }
    return false;
}

void TexturePaintToolContext::reset()
{
    m_stage_changed.reset();
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_current_stage_changed);
    m_texture_painter.reset();
    m_painted_mesh_path = SdfPath::EmptyPath();
    if (m_material_override)
    {
        m_material_override->remove_material(m_material_id);
        m_material_id = -1;
    }
}

void TexturePaintToolContext::set_texture_file(const std::string& texture_file)
{
    if ((m_texture_data && m_texture_data->get_texture_filename() == texture_file) || !m_material_override)
        return;

    m_texture_data = std::make_unique<TextureData>(texture_file);
    auto settings = Application::instance().get_settings();
    if (m_texture_data->is_valid())
    {
        settings->set(settings_prefix() + ".texture_file", texture_file);
    }
    else
    {
        settings->set(settings_prefix() + ".texture_file", std::string());
        m_texture_data = nullptr;
    }

    set_material();
}

std::shared_ptr<BrushProperties> TexturePaintToolContext::get_brush_properties() const
{
    return m_brush_properties;
}

std::string TexturePaintToolContext::settings_prefix()
{
    static const std::string settings_preferences_prefix("texture_paint");
    return settings_preferences_prefix;
}

bool TexturePaintToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                               ViewportUiDrawManager* draw_manager)
{
    if (m_changing_radius)
    {
        const auto radius_delta = QCursor::pos().x() - m_radius_change_cursor_start_pos.x();
        const auto new_radius = std::min(std::max(m_start_radius + radius_delta, 1), 500);
        m_brush_properties->set_radius(new_radius);
        Application::instance().get_settings()->set(settings_prefix() + ".radius", new_radius);
        QCursor::setPos(m_radius_change_cursor_start_pos);
        ApplicationUI::instance().get_active_view()->unsetCursor();
        m_changing_radius = false;
        return true;
    }

    m_mouse_x = mouse_event.x();
    m_mouse_y = mouse_event.y();
    if (!m_texture_painter || !m_texture_data || m_painted_mesh_path.IsEmpty())
        return false;

    const auto view_dim = viewport_view->get_viewport_dimensions();
    const GfVec2f center_ss(m_mouse_x, view_dim.height - m_mouse_y - 1);
    m_texture_painter->paint_stroke(center_ss);

    push_command();
    m_texture_painter = nullptr;
    m_material_override->update_material(m_material_id, *m_material_descr);

    if (m_writing_to_file)
        bake_textures();

    return true;
}

bool TexturePaintToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                            ViewportUiDrawManager* draw_manager)
{
    if (m_changing_radius)
    {
        const auto radius_delta = QCursor::pos().x() - m_radius_change_cursor_start_pos.x();
        const auto new_radius = std::min(std::max(m_start_radius + radius_delta, 1), 500);
        m_brush_properties->set_radius(new_radius);
        Application::instance().get_settings()->set(settings_prefix() + ".radius", new_radius);
        return true;
    }
    m_mouse_x = mouse_event.x();
    m_mouse_y = mouse_event.y();

    if (!m_texture_painter || !m_texture_data || m_painted_mesh_path.IsEmpty())
        return false;

    m_texture_painter->paint_stroke_to(GfVec2i(m_mouse_x, viewport_view->get_viewport_dimensions().height - m_mouse_y - 1));

    m_material_override->update_material(m_material_id, *m_material_descr);
    return true;
}

void TexturePaintToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    const auto view_dim = viewport_view->get_viewport_dimensions();

    GfVec2i pos_ss;
    if (m_changing_radius)
    {
        const auto local_pos = ApplicationUI::instance().get_active_view()->get_gl_widget()->mapFromGlobal(m_radius_change_cursor_start_pos);
        pos_ss = { local_pos.x(), local_pos.y() };
    }
    else
    {
        const auto local_pos = ApplicationUI::instance().get_active_view()->get_gl_widget()->mapFromGlobal(QCursor::pos());
        pos_ss = { local_pos.x(), local_pos.y() };
    }
    if (pos_ss[0] < 0 || pos_ss[1] < 0 || pos_ss[0] >= view_dim.width || pos_ss[1] >= view_dim.height)
        return;

    draw_manager->begin_drawable();
    draw_manager->set_color(GfVec4f(1, 1, 1, 1));
    draw_manager->set_mvp_matrix(GfMatrix4f(1));
    draw_manager->set_paint_style(ViewportUiDrawManager::PaintStyle::FLAT);
    draw_manager->set_line_width(1);

    const auto orig = GfVec3f(((float)pos_ss[0] / view_dim.width * 2 - 1), ((view_dim.height - (float)pos_ss[1]) / view_dim.height * 2 - 1), 0.5);

    const GfVec2f radius(m_brush_properties->get_radius() / (float)view_dim.width * 2, m_brush_properties->get_radius() / (float)view_dim.height * 2);
    std::vector<GfVec3f> points;
    for (int i = 0; i < 50; i++)
    {
        GfVec3f vt;
        vt = GfVec3f::XAxis() * cos((2 * M_PI / 50) * i) * radius[0];
        vt += GfVec3f::YAxis() * sin((2 * M_PI / 50) * i) * radius[1];
        vt += orig;
        points.push_back(vt);
    }
    draw_manager->mesh(ViewportUiDrawManager::PrimitiveTypeLinesLoop, points);
    draw_manager->end_drawable();
}

bool TexturePaintToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                             ViewportUiDrawManager* draw_manager)
{
    m_texture_painter = nullptr;
    const auto screen_x = mouse_event.x();
    const auto screen_y = mouse_event.y();
    m_mouse_x = screen_x;
    m_mouse_y = screen_y;
    if (mouse_event.button() == Qt::LeftButton && m_radius_editable)
    {

        m_radius_change_cursor_start_pos = QCursor::pos();
        m_changing_radius = true;
        m_start_radius = m_brush_properties->get_radius();
        ApplicationUI::instance().get_active_view()->setCursor(Qt::BlankCursor);
        return true;
    }

    if (!m_texture_data || m_painted_mesh_path.IsEmpty())
        return false;

    if (!init_paint_data(viewport_view))
        return false;

    const GfVec2f center_ss(m_mouse_x, viewport_view->get_viewport_dimensions().height - m_mouse_y - 1);
    m_texture_painter->paint_stroke(center_ss);
    m_material_override->update_material(m_material_id, *m_material_descr);
    return true;
}

void TexturePaintToolContext::set_material()
{
    if (!m_texture_data || !m_material_override)
    {
        if (m_material_id != -1)
        {
            m_material_override->remove_material(m_material_id);
            m_material_id = -1;
        }

        return;
    }

    HdMaterialNetworkMap mat_network;

    SdfAssetPath painted_texture;
    if (m_texture_data->get_texture_filename().find("<UDIM>") != std::string::npos)
        painted_texture = SdfAssetPath("texblock://painted_texture_<UDIM>.wtex", "texblock://painted_texture_<UDIM>.wtex");
    else
        painted_texture = SdfAssetPath("texblock://painted_texture.wtex", "texblock://painted_texture.wtex");

    HdMaterialNode st_reader;
    st_reader.path = SdfPath("/st_reader");
    st_reader.identifier = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdPrimvarReader_float2"))->GetIdentifier();
    st_reader.parameters[TfToken("varname")] = VtValue(TfToken("st"));

    HdMaterialNode sampler;
    sampler.path = SdfPath("/sampler");
    sampler.identifier = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdUVTexture"))->GetIdentifier();
    sampler.parameters[TfToken("file")] = VtValue(painted_texture);

    HdMaterialNode terminal;
    terminal.path = SdfPath("/override");
    terminal.identifier = get_texture_shader_id();
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

    auto& map = mat_network.map[HdMaterialTerminalTokens->surface];
    map.nodes = { st_reader, sampler, terminal };
    map.relationships = { st_reader_to_sampler, sampler_to_terminal };
    map.primvars.push_back(TfToken("st"));
    mat_network.terminals.push_back(terminal.path);

    PrimMaterialDescriptor::PrimvarDescriptorMap primvars;
    primvars[HdInterpolation::HdInterpolationFaceVarying] = { HdPrimvarDescriptor(TfToken("st"), HdInterpolation::HdInterpolationFaceVarying,
                                                                                  HdPrimvarRoleTokens->textureCoordinate, true) };

    m_material_descr = std::make_unique<PrimMaterialDescriptor>(VtValue(mat_network), primvars);
    if (m_material_id == -1)
        m_material_id = m_material_override->insert_material(*m_material_descr.get());
    else
        m_material_override->update_material(m_material_id, *m_material_descr.get());
    m_material_override->assign_material(m_material_id, m_painted_mesh_path);
}

TfToken TexturePaintToolContext::get_name() const
{
    static TfToken name("texture_paint", TfToken::Immortal);
    return name;
}

std::shared_ptr<PrimMaterialOverride> TexturePaintToolContext::get_prim_material_override()
{
    return m_material_override;
}

OPENDCC_NAMESPACE_CLOSE
