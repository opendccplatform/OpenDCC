// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/pxr.h>
#if PXR_VERSION < 2108
#include <GL/glew.h>
#include <pxr/imaging/glf/glew.h>
#else
#include <pxr/imaging/garch/glApi.h>
#endif
#include <pxr/base/gf/gamma.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/pointBased.h>
#include <pxr/imaging/glf/diagnostic.h>
#include <pxr/imaging/glf/contextCaps.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>

#include <QMenu>
#include <QTimer>
#include <QPainter>
#include <QSurfaceFormat>
#include <QGuiApplication>
#include <QScreen>

#include "opendcc/base/qt_python.h"
#include "opendcc/app/ui/application_ui.h"

#include "opendcc/app/core/session.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/undo/block.h"

#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_grid.h"
#include "opendcc/app/viewport/istage_resolver.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/hydra_render_settings.h"
#include "opendcc/app/viewport/prim_material_override.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/app/viewport/viewport_dnd_controller.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/viewport/viewport_color_correction.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include "opendcc/app/viewport/viewport_usd_camera_mapper.h"
#include "opendcc/app/viewport/viewport_background_filler.h"
#include "opendcc/app/viewport/viewport_context_menu_registry.h"

#if PXR_VERSION >= 2205
#include "opendcc/usd/compositing/compositor.h"
#include "opendcc/app/viewport/iviewport_compositing_extension.h"
#endif

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    TfToken get_up_axis(const TfToken& context, UsdStageRefPtr stage)
    {
        return context == TfToken("USD") ? (stage ? UsdGeomGetStageUpAxis(stage) : UsdGeomTokens->y) : UsdGeomTokens->y;
    }
};

void change_cursor(const QString& icon, int hotX, int hotY)
{
    // Since Qt doesn't automatically scale the cursor for high-DPI displays,
    // this approach is necessary. By default, Qt scales the pixmap to match
    // the highest DPI screen, which can result in a strange appearance.
    // Therefore, manual scaling is required.

    QPixmap pixmap(icon);

    qreal max_scaleFactor = 1.0;

    for (auto screen : QGuiApplication::screens())
    {
        qreal scaleFactor = screen->devicePixelRatio();
        if (scaleFactor > max_scaleFactor)
            max_scaleFactor = scaleFactor;
    }

    QPoint globalCursorPos = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(globalCursorPos);

    if (screen)
    {
        qreal scaleFactor = screen->devicePixelRatio();
        pixmap.setDevicePixelRatio(max_scaleFactor / scaleFactor);
    }

    QCursor cursor(pixmap, hotX, hotY);
    QApplication::setOverrideCursor(cursor);
}

ViewportGLWidget::ViewportGLWidget(std::shared_ptr<ViewportView> viewport_view, std::shared_ptr<ViewportSceneContext> scene_context, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_viewport_view(viewport_view)
    , m_scene_context(scene_context)
{
    setProperty("unfocusedKeyEvent_enable", true);

    m_camera_controller =
        std::make_shared<ViewportCameraController>(ViewportCameraMapperFactory::create_camera_mapper(m_scene_context->get_context_name()));
    connect(m_camera_controller.get(), &ViewportCameraController::camera_changed, this, &ViewportGLWidget::on_camera_changed);

    m_viewport_view->set_gl_widget(this);
    setTextureFormat(GL_RGBA16);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setAcceptDrops(true);
    setAttribute(Qt::WA_DeleteOnClose);
    setContextMenuPolicy(Qt::ContextMenuPolicy::PreventContextMenu);

    auto& app = Application::instance();
    const auto* settings = app.get_settings();

    m_params = {};
    m_params.highlight = true;
    m_params.frame = UsdTimeCode(app.get_current_time());
    m_params.enable_sample_alpha_to_coverage = true;
    m_params.color_correction_mode = TfToken(settings->get("colormanagement.color_management", "openColorIO"));
    m_params.input_color_space = settings->get("colormanagement.ocio_rendering_space", "linear");
    m_params.view_OCIO = settings->get("colormanagement.ocio_view_transform", "sRGB");
    m_params.current_stage_root = SdfPath::AbsoluteRootPath();
    m_ui_draw_manager = nullptr;
    m_drag_and_drop_controller = std::make_unique<ViewportDndController>(scene_context->get_context_name());

    m_grid_settings.lines_color = settings->get("viewport.grid.lines_color", PXR_NS::GfVec4f(0.59462, 0.59462, 0.59462, 1));
    m_grid_settings.min_step = settings->get("viewport.grid.min_step", double(1.0));
    m_grid_settings.enable = settings->get("viewport.grid.enable", true);
}

ViewportGLWidget::~ViewportGLWidget()
{
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_current_stage_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_TIME_CHANGED, m_current_time_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED, m_before_stage_closed_cid);
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_MODE_CHANGED, m_selection_mode_changed_cid);

    Application::instance().get_settings()->unregister_setting_changed("viewport.show_camera", m_show_camera_cid);

    UsdViewportRefineManager::instance().unregister_refine_level_changed_callback(m_usd_refine_level_changed_cid);
    UsdViewportRefineManager::instance().unregister_stage_cleared_callback(m_usd_stage_cleared_cid);
    DefCamSettings::instance().unregister_event_callback(m_def_cam_settings_dispatcher_handle);
    for (const auto& cid : m_setting_changed_cids)
        Application::instance().get_settings()->unregister_setting_changed(cid.first, cid.second);

    makeCurrent();

#if PXR_VERSION >= 2205
    m_compositor.reset();
#endif

    m_stage_watcher.reset();
    m_drag_and_drop_controller->on_view_destroyed(m_viewport_view);

    close_engine();
    delete m_ui_draw_manager;

    doneCurrent();
}

void ViewportGLWidget::enable_engine(bool val)
{
    m_engine_enabled = val;
    if (!m_engine_enabled)
        close_engine();
}

void ViewportGLWidget::initializeGL()
{
#if PXR_VERSION < 2108
    GlfGlewInit();
#else
    GarchGLApiLoad();
#endif
    GlfRegisterDefaultDebugOutputMessageCallback();

    // Qt calls initializeGL() again whenever the widget's GL context is recreated, which happens
    // when the panel is reparented into another top-level window. Register the application callbacks
    // once, and replace the draw manager instead of leaking the previous one.
    if (!m_callbacks_registered)
    {
        register_callbacks();
        m_callbacks_registered = true;
    }

    delete m_ui_draw_manager;
    m_ui_draw_manager = new ViewportUiDrawManager(width() * devicePixelRatio(), height() * devicePixelRatio());
    const auto background_gradient_enable = Application::instance().get_settings()->get("viewport.background.gradient_enable", false);
    m_enable_background_gradient = background_gradient_enable;
    if (background_gradient_enable)
        m_background_drawer = std::make_unique<GradientBackgroundFiller>(this);
    else
        m_background_drawer = std::make_unique<SolidBackgroundFiller>(this);
    const auto up_axis = get_up_axis(m_scene_context->get_context_name(), Application::instance().get_session()->get_current_stage());
    m_grid = std::make_unique<ViewportGrid>(m_grid_settings.lines_color, m_grid_settings.min_step, m_grid_settings.enable, up_axis);
    m_camera_controller->set_up_axis(up_axis);
    ViewportColorCorrection::ColorCorrectionMode mode;
    if (m_params.color_correction_mode == "openColorIO")
        mode = ViewportColorCorrection::OCIO;
    else if (m_params.color_correction_mode == "sRGB")
        mode = ViewportColorCorrection::SRGB;
    else
        mode = ViewportColorCorrection::DISABLED;
    m_color_correction =
        std::make_unique<ViewportColorCorrection>(mode, m_params.view_OCIO, m_params.input_color_space, m_params.gamma, m_params.exposure);
    set_render_settings_to_engine();

#if PXR_VERSION >= 2205
    auto compositor = IViewportCompositingExtension::create_compositor(this);
    set_compositor(compositor);
#endif

    emit gl_initialized();
}

void ViewportGLWidget::paintGL()
{
    if (!m_engine_enabled)
        return;

    m_background_drawer->draw();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    m_params.render_resolution[0] = width() * devicePixelRatio();
    m_params.render_resolution[1] = height() * devicePixelRatio();

    auto frustum = m_camera_controller->get_frustum();
    const auto viewport_dim = m_viewport_view->get_viewport_dimensions();
    CameraUtilConformWindow(&frustum, CameraUtilConformWindowPolicy::CameraUtilFit,
                            viewport_dim.height != 0 ? (double)viewport_dim.width / viewport_dim.height : 1.0);

    const auto view_mat = frustum.ComputeViewMatrix();
    const auto proj_mat = frustum.ComputeProjectionMatrix();
    GlfSimpleLightVector lights;
    if (m_params.use_camera_light)
    {
        GlfSimpleLight camera_light;
        camera_light.SetAmbient(GfVec4f(0, 0, 0, 0));

        GfVec3d cam_pos = frustum.GetPosition();
        camera_light.SetPosition(GfVec4f(cam_pos[0], cam_pos[1], cam_pos[2], 1));
        lights.push_back(camera_light);
    }

    GlfSimpleMaterial material;
    material.SetAmbient(GfVec4f(0.2, 0.2, 0.2, 1));
    material.SetSpecular(GfVec4f(0.1, 0.1, 0.1, 1));
    material.SetShininess(32.0);
    GfVec4f scene_ambient(0.01, 0.01, 0.01, 1.0);

    auto engine = get_engine();
    engine->set_lighting_state(lights, material, scene_ambient);

#if PXR_VERSION >= 2108
    CameraUtilFraming framing { GfRange2f(GfVec2f(0, 0), GfVec2f(m_params.render_resolution[0], m_params.render_resolution[1])),
                                m_params.crop_region };
    engine->set_framing(framing);
#ifdef OPENDCC_USE_HYDRA_FRAMING_API
    if (framing.IsValid())
    {
        engine->set_render_buffer_size(GfVec2i(viewport_dim.width, viewport_dim.height));
        engine->set_override_window_policy({ true, CameraUtilFit });
    }
    else
    {
        engine->set_render_viewport(GfVec4d(0, 0, m_params.render_resolution[0], m_params.render_resolution[1]));
    }
#endif
#else
    engine->set_render_viewport(GfVec4d(0, 0, m_params.render_resolution[0], m_params.render_resolution[1]));
#endif
    engine->set_camera_state(view_mat, proj_mat);

    // We have to store current GL_UNPACK_ALIGNMENT because of the following calls to QPainter::drawText,
    // that essentially creates a texture and blits it onto current framebuffer.
    // Hydra OpenGL render changes texture alignment before uploading it to backend, we must restore it in order to
    // correct render text
    // TODO:
    // 1) consider using HgiGL_ScopedStateHolder for all such parameters
    // 2) implement our own text draw mechanism
    // 3) Pre-created QImage with specified size based on text extent and draw with QPainter into this QImage, then blit to viewport framebuffer
    GLint prev_align;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);

    engine->update(m_params);
    engine->render(m_params);

    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
    m_params.invised_paths_dirty = false;
    m_params.visibility_mask.mark_clean();

    glPushDebugGroup(GL_DEBUG_SOURCE_THIRD_PARTY, 0, -1, "ViewportColorCorrection");
    m_color_correction->apply(m_viewport_view);
    glPopDebugGroup();

    if (!engine->is_converged())
    {
        QTimer::singleShot(5, this, qOverload<>(&QOpenGLWidget::update));
    }

    if (auto tool = ApplicationUI::instance().get_current_viewport_tool())
    {
        glPushDebugGroup(GL_DEBUG_SOURCE_THIRD_PARTY, 0, -1, "CurrentViewportTool");
        tool->draw(m_viewport_view, m_ui_draw_manager);
        glPopDebugGroup();
    }

    if (!m_extensions.empty())
    {
        glPushDebugGroup(GL_DEBUG_SOURCE_THIRD_PARTY, 0, -1, "ViewportDrawExtensions");
        for (auto& extension : m_extensions)
        {
            extension->draw(m_ui_draw_manager, m_camera_controller->get_frustum(), m_params.render_resolution[0], m_params.render_resolution[1]);
        }
        glPopDebugGroup();
    }

#if PXR_VERSION >= 2205
    GLint app_draw_fbo = defaultFramebufferObject();
    const auto hgi = ViewportHydraEngine::get_hgi();
    m_compositor->composite(app_draw_fbo, hgi);
#endif

    m_grid->draw(frustum);

    m_ui_draw_manager->execute_draw_queue(m_params.render_resolution[0], m_params.render_resolution[1], m_mousex * devicePixelRatio(),
                                          m_mousey * devicePixelRatio(), GfMatrix4f(proj_mat), GfMatrix4f(view_mat));

    if (m_show_camera)
    {
        draw_headup_display_text();
    }
    if (m_params.stage_resolver)
        m_params.stage_resolver->mark_clean();
}

std::shared_ptr<ViewportEngineProxy> ViewportGLWidget::get_engine() const
{
    if (!m_engine)
    {
        if (m_scene_context->use_hydra2())
        {
            m_engine = std::make_shared<ViewportEngineProxy>(m_scene_context->get_index_manager());
        }
        else
        {
            m_engine = std::make_shared<ViewportEngineProxy>(m_scene_context->get_delegates());
        }
    }
    return m_engine;
}

void ViewportGLWidget::close_engine()
{
    m_engine = nullptr;
}

void ViewportGLWidget::resizeEvent(QResizeEvent* e)
{
    m_camera_controller->set_display_size(e->size().width() * devicePixelRatio(), e->size().height() * devicePixelRatio());
    QOpenGLWidget::resizeEvent(e);

    // The resize paint lands while HdSt is still reallocating AOVs, so it renders empty and
    // is_converged() schedules no follow-up. Qt5 issued an extra paint of its own, Qt6 does not.
    update();
}

std::pair<PXR_NS::HdxPickHitVector, bool> ViewportGLWidget::intersect_impl(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end,
                                                                           SelectionList::SelectionMask pick_target,
                                                                           const PXR_NS::HdRprimCollection* custom_collection,
                                                                           const PXR_NS::TfTokenVector& render_tags,
                                                                           const PXR_NS::TfToken& resolve_mode)
{
    UsdStageRefPtr stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
    {
        return { HdxPickHitVector {}, false };
    }

    makeCurrent();

    ViewportHydraIntersectionParams pick_params;
    pick_params.engine_params = m_params;
    pick_params.engine_params.gamma_correct_colors = false;
    pick_params.engine_params.enable_id_render = true;
    pick_params.engine_params.enable_sample_alpha_to_coverage = false;
    pick_params.pick_target = pick_target;
    if (custom_collection)
    {
        pick_params.use_custom_collection = true;
        pick_params.collection = *custom_collection;
    }
    else
    {
        pick_params.use_custom_collection = false;
    }

    if (!render_tags.empty())
    {
        pick_params.use_custom_render_tags = true;
        pick_params.render_tags = render_tags;
    }
    else
    {
        pick_params.use_custom_render_tags = false;
    }

    auto frustum = m_camera_controller->get_frustum();

    const auto view_dim = m_viewport_view->get_viewport_dimensions();
    CameraUtilConformWindow(&frustum, CameraUtilConformWindowPolicy::CameraUtilFit,
                            view_dim.height != 0.0 ? (double)view_dim.width / view_dim.height : 1.0);

    pick_params.view_matrix = frustum.ComputeViewMatrix();
    pick_params.proj_matrix = frustum.ComputeProjectionMatrix();

    GfVec2f _start = { std::min(start[0], end[0]), std::max(start[1], end[1]) };
    GfVec2f _end = { std::max(start[0], end[0]), std::min(start[1], end[1]) };

    _start[1] = view_dim.height - _start[1];
    _end[1] = view_dim.height - _end[1];

    const float select_rect_width = _end[0] - _start[0];
    const float select_rect_height = _end[1] - _start[1];
    pick_params.resolution = GfVec2i(select_rect_width, select_rect_height);

    GfMatrix4d selection_matrix;
    selection_matrix.SetIdentity();

    selection_matrix[0][0] = view_dim.width / select_rect_width;
    selection_matrix[1][1] = view_dim.height / select_rect_height;
    selection_matrix[3][0] = (view_dim.width - (_start[0] * 2 + select_rect_width)) / select_rect_width;
    selection_matrix[3][1] = (view_dim.height - (_start[1] * 2 + select_rect_height)) / select_rect_height;

    pick_params.proj_matrix *= selection_matrix;
    pick_params.resolve_mode = resolve_mode;

    HdxPickHitVector out;
    const auto pick_result = get_engine()->test_intersection_batch(pick_params, out);

    doneCurrent();

    return { std::move(out), pick_result };
}

SelectionList ViewportGLWidget::make_selection_list(const PXR_NS::HdxPickHitVector& pick_hits, SelectionList::SelectionMask selection_mask) const
{
    struct Data
    {
        SelectionData::IndexIntervals points;
        SelectionData::IndexIntervals edges;
        SelectionData::IndexIntervals elements;
        SelectionData::IndexIntervals instances;
        bool full = false;
    };
    using SelectionFlags = SelectionList::SelectionFlags;
    auto stage = Application::instance().get_session()->get_current_stage();

    std::unordered_map<SdfPath, Data, SdfPath::Hash> sel_data;
    for (const auto& hit : pick_hits)
    {
        const auto obj_id = hit.objectId.ReplacePrefix(hit.delegateId, SdfPath::AbsoluteRootPath());
        auto& val = sel_data[obj_id];
        if ((selection_mask & SelectionFlags::POINTS) && hit.pointIndex >= 0 && hit.instancerId.IsEmpty())
            val.points.insert(hit.pointIndex);
        if ((selection_mask & SelectionFlags::EDGES) && hit.edgeIndex >= 0 && hit.instancerId.IsEmpty())
            val.edges.insert(hit.edgeIndex);
        if ((selection_mask & SelectionFlags::ELEMENTS) && hit.elementIndex >= 0 && hit.instancerId.IsEmpty())
            val.elements.insert(hit.elementIndex);
        if ((selection_mask & SelectionFlags::INSTANCES) && hit.instanceIndex >= 0 && !hit.instancerId.IsEmpty())
        {
#if PXR_VERSION >= 2005
            HdInstancerContext instancer_context;
            auto real_path = get_engine()->get_prim_path_from_instance_index(hit.objectId, hit.instanceIndex, &instancer_context);
            if (!instancer_context.empty())
            {
                sel_data[instancer_context[0].first].instances.insert(instancer_context[0].second);
            }
            else if (m_scene_context->get_context_name() == TfToken("USD"))
            {
                auto tmp = stage->GetPrimAtPath(real_path);
                if (tmp)
                {
                    while (!tmp.IsInstance())
                        tmp = tmp.GetParent();
                    real_path = tmp.GetPrimPath();
                    sel_data[real_path].instances.insert(hit.instanceIndex);
                }
            }
            else
            {
                sel_data[real_path].instances.insert(hit.instanceIndex);
            }
#else
            int global_id = -1;
            auto real_path = get_engine()->get_prim_path_from_instance_index(hit.objectId, hit.instanceIndex, &global_id);
            sel_data[real_path].instances.push_back(
                { global_id == -1 ? static_cast<SelectionList::IndexType>(hit.instanceIndex) : static_cast<SelectionList::IndexType>(global_id) });
#endif
        }
        if (selection_mask & SelectionFlags::FULL_SELECTION)
        {
            SdfPath real_path;
#if PXR_VERSION >= 2005
            if (hit.instancerId.IsEmpty())
            {
                real_path = obj_id;
            }
            else
            {
                HdInstancerContext ctx;
                real_path = get_engine()->get_prim_path_from_instance_index(hit.objectId, hit.instanceIndex, &ctx);
                if (!ctx.empty())
                    real_path = ctx[0].first;
                else if (m_scene_context->get_context_name() == TfToken("USD"))
                {
                    auto tmp = stage->GetPrimAtPath(real_path);
                    while (tmp && !tmp.IsInstance())
                        tmp = tmp.GetParent();
                    real_path = tmp.GetPrimPath();
                }
            }
#else
            if (hit.instancerId.IsEmpty())
                real_path = hit.objectId.ReplacePrefix(hit.delegateId, SdfPath::AbsoluteRootPath());
            else
                real_path = get_engine()->get_prim_path_from_instance_index(hit.objectId, hit.instanceIndex);
#endif
            sel_data[real_path].full = true;
        }
    }

    SelectionList list;
    for (const auto entry : sel_data)
    {
        SelectionData data(entry.second.full, entry.second.points, entry.second.edges, entry.second.elements, entry.second.instances, {});
        list.set_selection_data(entry.first, std::move(data));
    }

    return list;
}

void ViewportGLWidget::on_camera_changed(PXR_NS::SdfPath follow_path)
{
    if (m_camera_prim_path == follow_path)
        return;

    auto invised_paths = m_params.invised_paths;
    invised_paths.erase(m_camera_prim_path);
    if (!follow_path.IsEmpty())
        invised_paths.insert(follow_path);
    set_enable_camera_navigation_undo(!follow_path.IsEmpty());
    set_invised_paths(invised_paths);
    m_camera_prim_path = follow_path;
    update();
}

void ViewportGLWidget::update_stage_watcher()
{
    auto stage = Application::instance().get_session()->get_current_stage();
    if (stage)
    {
        m_stage_watcher = std::make_shared<StageObjectChangedWatcher>(stage, [this](PXR_NS::UsdNotice::ObjectsChanged const& notice) {
            bool need_to_update = false;
            get_engine()->resume();
            m_params.stage_meters_per_unit = UsdGeomGetStageMetersPerUnit(notice.GetStage());
            get_engine()->set_render_setting(TfToken("stageMetersPerUnit"), VtValue(m_params.stage_meters_per_unit));
            if (notice.GetResyncedPaths().size() > 0)
            {
                update();
                return;
            }

            for (auto& path : notice.GetChangedInfoOnlyPaths())
            {
                if (path.IsPropertyPath() && path.GetName() == "ui:nodegraph:node:pos")
                {
                }
                else
                {
                    need_to_update = true;
                }
            }
            if (need_to_update)
            {
                update();
            }
        });
    }
    else
    {
        m_stage_watcher.reset();
    }
}

void ViewportGLWidget::register_callbacks()
{
    m_scene_context->register_event_handler(ViewportSceneContext::EventType::DirtyRenderSettings, [this] { set_render_settings_to_engine(); });
    m_def_cam_settings_dispatcher_handle = DefCamSettings::instance().register_event_callback([this](const GfCamera& camera) { update(); });

    m_current_stage_changed_cid = Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this] {
        // Rebuilding the engine here is GL work outside paintGL. Qt 6.4+ composites through RHI and
        // leaves RHI's context current, so without this the new GL objects land in the wrong context.
        makeCurrent();

        get_engine()->reset();

        if (m_scene_context->get_context_name() == TfToken("USD"))
        {
            if (auto stage = Application::instance().get_session()->get_current_stage())
            {
                m_params.stage_meters_per_unit = UsdGeomGetStageMetersPerUnit(stage);
            }
            get_engine()->set_render_setting(TfToken("stageMetersPerUnit"), VtValue(m_params.stage_meters_per_unit));
        }

        const auto up_axis = get_up_axis(m_scene_context->get_context_name(), Application::instance().get_session()->get_current_stage());
        if (m_grid)
            m_grid->set_up_axis(up_axis);
        m_camera_controller->set_up_axis(up_axis);
        m_camera_controller->set_default_camera();
        m_params.current_stage_root = SdfPath::AbsoluteRootPath();
        if (auto stage = Application::instance().get_session()->get_current_stage())
        {
            if (m_params.stage_resolver)
                m_params.current_stage_root =
                    m_params.current_stage_root.AppendChild(TfToken(TfMakeValidIdentifier(stage->GetRootLayer()->GetIdentifier())));
        }
        update_stage_watcher();

        doneCurrent();
    });

    m_current_time_changed_cid = Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this] {
        if (!m_params.stage_resolver)
        {
            m_params.frame = UsdTimeCode(Application::instance().get_current_time());
            m_camera_controller->set_time(m_params.frame);
            update();
        }
    });

    update_stage_watcher();

    m_selection_changed_cid = Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] {
        if (auto engine = get_engine())
        {

            if (m_scene_context->get_context_name() == TfToken("USD"))
            {
                auto rich_selection = Application::instance().is_soft_selection_enabled() &&
                                              Application::instance().get_settings()->get("soft_selection.enable_color", true)
                                          ? Application::instance().get_rich_selection()
                                          : RichSelection();
                engine->set_selected(Application::instance().get_selection(), rich_selection);
                auto prim_selection = Application::instance().get_highlighted_prims();
                m_params.repr_paths = std::unordered_set<SdfPath, SdfPath::Hash>(prim_selection.begin(), prim_selection.end());
            }
        }
        update();
    });
    auto update_rich_selection = [this](const std::string&, const Settings::Value& val, Settings::ChangeType) {
        if (auto engine = get_engine())
        {
            if (m_scene_context->get_context_name() == TfToken("USD"))
            {
                auto rich_selection = Application::instance().is_soft_selection_enabled() &&
                                              Application::instance().get_settings()->get("soft_selection.enable_color", true)
                                          ? Application::instance().get_rich_selection()
                                          : RichSelection();
                engine->set_selected(Application::instance().get_selection(), rich_selection);
            }
        }
    };
    m_setting_changed_cids["soft_selection.falloff_radius"] =
        Application::instance().get_settings()->register_setting_changed("soft_selection.falloff_radius", update_rich_selection);
    m_setting_changed_cids["soft_selection.falloff_mode"] =
        Application::instance().get_settings()->register_setting_changed("soft_selection.falloff_mode", update_rich_selection);
    m_setting_changed_cids["soft_selection.enable_color"] =
        Application::instance().get_settings()->register_setting_changed("soft_selection.enable_color", update_rich_selection);
    m_setting_changed_cids["soft_selection.falloff_curve"] =
        Application::instance().get_settings()->register_setting_changed("soft_selection.falloff_curve", update_rich_selection);
    m_setting_changed_cids["soft_selection.falloff_color"] =
        Application::instance().get_settings()->register_setting_changed("soft_selection.falloff_color", update_rich_selection);

    m_setting_changed_cids["colormanagement.ocio_rendering_space"] = Application::instance().get_settings()->register_setting_changed(
        "colormanagement.ocio_rendering_space", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            if (!val.try_get<std::string>(&m_params.input_color_space))
                return;
            if (m_color_correction)
                m_color_correction->set_color_space(m_params.input_color_space);
            update();
        });
    m_setting_changed_cids["viewport.grid.lines_color"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.grid.lines_color", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            if (!val.try_get<GfVec4f>(&m_grid_settings.lines_color))
                return;
            if (m_grid)
                m_grid->set_grid_color(m_grid_settings.lines_color);
            update();
        });
    m_setting_changed_cids["viewport.grid.min_step"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.grid.min_step", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            if (!val.try_get<double>(&m_grid_settings.min_step))
                return;
            if (m_grid)
                m_grid->set_min_step(m_grid_settings.min_step);
            update();
        });
    m_setting_changed_cids["viewport.grid.enable"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.grid.enable", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            if (!val.try_get<bool>(&m_grid_settings.enable))
                return;
            if (m_grid)
                m_grid->set_enabled(m_grid_settings.enable);
            update();
        });
    m_before_stage_closed_cid =
        Application::instance().register_event_callback(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED, [this] { get_engine()->reset(); });

    m_selection_mode_changed_cid = Application::instance().register_event_callback(Application::EventType::SELECTION_MODE_CHANGED, [this] {
        if (Application::instance().get_selection_mode() == Application::SelectionMode::UV)
        {
            m_params.point_color = { 100.0f / 255.0f, 54.0f / 255.0f, 38.0f / 255.0f, 1.0f };
            if (auto engine = get_engine())
            {
                engine->set_selection_color({ 0.0f, 1.0f, 0.0f, 1.0f });
            }
        }
        else
        {
            m_params.point_color = { 0.0f, 0.0f, 0.0f, 1.0f };

            if (auto engine = get_engine())
            {
                const auto color = Application::instance().get_settings()->get("viewport.selection_color", GfVec4f(1, 1, 0, 0.5));
                engine->set_selection_color(color);
            }
        }
        update();
    });

    m_show_camera_cid = Application::instance().get_settings()->register_setting_changed(
        "viewport.show_camera", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            m_show_camera = val.get(true);
            update();
        });

    m_show_camera = Application::instance().get_settings()->get("viewport.show_camera", true);

    m_usd_refine_level_changed_cid = UsdViewportRefineManager::instance().register_refine_level_changed_callback(
        [this](const PXR_NS::UsdStageCache::Id&, const PXR_NS::SdfPath&, int) { update(); });

    m_usd_stage_cleared_cid =
        UsdViewportRefineManager::instance().register_stage_cleared_callback([this](const PXR_NS::UsdStageCache::Id&) { update(); });

    m_setting_changed_cids["viewport.selection_color"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.selection_color", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            GfVec4f color;
            if (!TF_VERIFY(val.try_get<GfVec4f>(&color), "Failed to extract GfVec4f from \"viewport.selection_color\" setting."))
                return;

            if (auto engine = get_engine())
            {
                engine->set_selection_color(color);
                update();
            }
        });

    m_setting_changed_cids["viewport.manipulators.global_scale"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.manipulators.global_scale", [this](const std::string& name, const Settings::Value&, Settings::ChangeType) { update(); });
    m_setting_changed_cids["viewport.background.gradient_enable"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.background.gradient_enable", [this](const std::string& name, const Settings::Value& val, Settings::ChangeType) {
            bool enable_gradient = false;
            if (!TF_VERIFY(val.try_get<bool>(&enable_gradient), "Failed to extract bool from \"viewport.background.gradient_enable\" setting."))
                return;

            if (enable_gradient == m_enable_background_gradient)
                return;

            m_enable_background_gradient = enable_gradient;
            if (enable_gradient)
                m_background_drawer = std::make_unique<GradientBackgroundFiller>(this);
            else
                m_background_drawer = std::make_unique<SolidBackgroundFiller>(this);
            update();
        });
}

void ViewportGLWidget::set_render_settings_to_engine()
{
    if (m_scene_context)
    {
        get_engine()->set_render_settings(m_scene_context->get_render_settings());
    }
    else
    {
        get_engine()->set_render_settings(nullptr);
    }
    Q_EMIT render_settings_changed();
}

void ViewportGLWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_mouse_control_mode == MouseControlMode::NONE && (event->modifiers() & Qt::AltModifier))
    {
        if (m_enable_camera_navigation_undo)
            m_tool_undo_block = std::make_unique<commands::UsdEditsUndoBlock>();
        QPoint point = event->pos();
        m_mousex = point.x();
        m_mousey = point.y();

        Qt::MouseButton button = event->button();

        if (button == Qt::LeftButton)
        {
            m_mousemode = MouseMode::MouseModeTumble;
            change_cursor(":/icons/cursor_tumble", -12, -12);
        }
        else if (button == Qt::MiddleButton)
        {
            m_mousemode = MouseMode::MouseModeTruck;
            change_cursor(":/icons/cursor_track", -12, -12);
        }
        else if (button == Qt::RightButton)
        {
            m_mousemode = MouseMode::MouseModeZoom;
            change_cursor(":/icons/cursor_dolly", -12, -12);
        }
        m_mouse_control_mode = MouseControlMode::CAMERA;
    }

    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    if (m_mouse_control_mode == MouseControlMode::NONE && tool)
    {
        m_mouse_control_mode = MouseControlMode::TOOL_CONTEXT;
        ViewportMouseEvent mouse_event(m_mousex * devicePixelRatio(), m_mousey * devicePixelRatio(), event->globalPos(), event->button(),
                                       event->buttons(), event->modifiers());
        tool->on_mouse_press(mouse_event, m_viewport_view, m_ui_draw_manager);
    }

    update();
}

void ViewportGLWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_mouse_control_mode == MouseControlMode::NONE && (event->modifiers() & Qt::AltModifier))
    {
        if (m_enable_camera_navigation_undo)
            m_tool_undo_block = std::make_unique<commands::UsdEditsUndoBlock>();
        QPoint point = event->pos();
        m_mousex = point.x();
        m_mousey = point.y();

        Qt::MouseButton button = event->button();

        if (button == Qt::LeftButton)
        {
            m_mousemode = MouseMode::MouseModeTumble;
            change_cursor(":/icons/cursor_tumble", -12, -12);
        }
        else if (button == Qt::MiddleButton)
        {
            m_mousemode = MouseMode::MouseModeTruck;
            change_cursor(":/icons/cursor_track", -12, -12);
        }
        else if (button == Qt::RightButton)
        {
            m_mousemode = MouseMode::MouseModeZoom;
            change_cursor(":/icons/cursor_dolly", -12, -12);
        }
        m_mouse_control_mode = MouseControlMode::CAMERA;
    }

    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    if (m_mouse_control_mode == MouseControlMode::NONE && tool)
    {
        m_mouse_control_mode = MouseControlMode::TOOL_CONTEXT;
        ViewportMouseEvent mouse_event(m_mousex * devicePixelRatio(), m_mousey * devicePixelRatio(), event->globalPos(), event->button(),
                                       event->buttons(), event->modifiers());
        tool->on_mouse_double_click(mouse_event, m_viewport_view, m_ui_draw_manager);
    }

    update();
}

void ViewportGLWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QPoint point = event->pos();
    m_mousex = point.x();
    m_mousey = point.y();
    m_mousemode = MouseMode::MouseModeNone;
    m_event_dispatcher.dispatch(EventType::VIEW_UPDATE);
    if (m_enable_camera_navigation_undo)
        m_tool_undo_block = nullptr;

    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    bool accepted = false;
    if (m_mouse_control_mode == MouseControlMode::TOOL_CONTEXT && tool)
    {
        ViewportMouseEvent mouse_event(m_mousex * devicePixelRatio(), m_mousey * devicePixelRatio(), event->globalPos(), event->button(),
                                       event->buttons(), event->modifiers());
        accepted = tool->on_mouse_release(mouse_event, m_viewport_view, m_ui_draw_manager);
    }

    if (!accepted && event->button() == Qt::MouseButton::RightButton && m_mouse_control_mode != MouseControlMode::CAMERA)
        exec_context_menu(QContextMenuEvent::Reason::Mouse, event->pos(), event->globalPos(), event->modifiers(), event->timestamp());
    if (!event->buttons())
    {
        m_mouse_control_mode = MouseControlMode::NONE;
        QGuiApplication::restoreOverrideCursor();
    }

    update();
}

void ViewportGLWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPoint point = event->pos();
    double dx = (point.x() - m_mousex);
    double dy = (point.y() - m_mousey);
    if (m_mouse_control_mode == MouseControlMode::CAMERA)
    {
        if (m_mousemode == MouseMode::MouseModeTumble)
        {
            m_camera_controller->tumble(0.25 * dx, 0.25 * dy);
            m_event_dispatcher.dispatch(EventType::VIEW_UPDATE);
        }
        else if (m_mousemode == MouseMode::MouseModeTruck)
        {
            const double pixel_to_world = m_camera_controller->compute_pixels_to_world_factor(height());
            m_camera_controller->truck(-dx * pixel_to_world, dy * pixel_to_world);
            m_event_dispatcher.dispatch(EventType::VIEW_UPDATE);
        }

        else if (m_mousemode == MouseMode::MouseModeZoom)
        {
            double zoom_delta = -0.002 * (dx + dy);
            m_camera_controller->adjust_distance(1 + zoom_delta);
            m_event_dispatcher.dispatch(EventType::VIEW_UPDATE);
        }
    }
    m_mousex = point.x();
    m_mousey = point.y();

    update_cursor();

    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    if (tool)
    {
        ViewportMouseEvent mouse_event(m_mousex * devicePixelRatio(), m_mousey * devicePixelRatio(), event->globalPos(), event->button(),
                                       event->buttons(), event->modifiers());
        tool->on_mouse_move(mouse_event, m_viewport_view, m_ui_draw_manager);
    }
    update();
}

void ViewportGLWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F)
    {
        GfRange3d selection_range;
        auto session = Application::instance().get_session();
        auto stage = session->get_current_stage();
        if (!stage)
            return;
        auto stage_id = Application::instance().get_session()->get_current_stage_id();
        auto cache = session->get_stage_bbox_cache(stage_id);
        const auto time = m_params.frame;
        for (const auto& selection : m_scene_context->get_selection())
        {
            if (selection.second.is_fully_selected())
            {
                selection_range.ExtendBy(get_engine()->get_bbox(selection.first));
                continue;
            }
            auto prim = stage->GetPrimAtPath(selection.first);
            if (!selection.second.get_instance_indices().empty())
            {
                if (auto point_instancer = UsdGeomPointInstancer(prim))
                {
                    VtInt64Array indices(selection.second.get_instance_indices().begin(), selection.second.get_instance_indices().end());
                    std::vector<GfBBox3d> result_bboxes(indices.size());
                    if (cache.ComputePointInstanceWorldBounds(point_instancer, indices.data(), indices.size(), result_bboxes.data()))
                    {
                        GfRange3d united_box;
                        for (const auto& box : result_bboxes)
                            united_box.UnionWith(box.ComputeAlignedRange());
                        selection_range = united_box;
                    }
                }
                else
                {
                    selection_range.ExtendBy(get_engine()->get_bbox(selection.first));
                }
            }
            auto point_based = UsdGeomPointBased(prim);
            if (!point_based)
                continue;

            auto world_transform = point_based.ComputeLocalToWorldTransform(time);
            VtVec3fArray points;
            point_based.GetPointsAttr().Get<VtVec3fArray>(&points, time);
            for (const auto& ind : selection.second.get_point_indices())
                selection_range.ExtendBy(world_transform.Transform(points[ind]));

            auto topology = Application::instance().get_session()->get_stage_topology_cache(stage_id).get_topology(prim, time);
            if (!topology)
                continue;

            for (const auto edge_ind : selection.second.get_edge_indices())
            {
                auto verts = topology->edge_map.get_vertices_by_edge_id(edge_ind);
                if (!std::get<1>(verts))
                    continue;

                selection_range.ExtendBy(world_transform.Transform(points[std::get<0>(verts)[0]]));
                selection_range.ExtendBy(world_transform.Transform(points[std::get<0>(verts)[1]]));
            }

            const auto& face_counts = topology->mesh_topology.GetFaceVertexCounts();
            const auto& face_indices = topology->mesh_topology.GetFaceVertexIndices();
            for (const auto face_ind : selection.second.get_element_indices())
            {
                const auto face_start = topology->face_starts[face_ind];
                for (int i = 0; i < face_counts[face_ind]; i++)
                {
                    const auto& point = points[face_indices[face_start + i]];
                    selection_range.ExtendBy(world_transform.Transform(point));
                }
            }
        }

        if (selection_range.IsEmpty())
            selection_range = get_engine()->get_bbox(stage->GetPseudoRoot().GetPrimPath());
        m_camera_controller->frame_selection(selection_range.IsEmpty() ? GfRange3d(GfVec3d(-2), GfVec3d(2)) : selection_range);
        update();
        return;
    }
    else if (auto tool = ApplicationUI::instance().get_current_viewport_tool())
    {
        if (tool->on_key_press(event, m_viewport_view, m_ui_draw_manager))
        {
            update();
            return;
        }
        else
        {
            update();
            QWidget::keyPressEvent(event);
        }
    }
}

void ViewportGLWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (auto tool = ApplicationUI::instance().get_current_viewport_tool())
    {
        tool->on_key_release(event, m_viewport_view, m_ui_draw_manager);
        update();
    }
}

void ViewportGLWidget::wheelEvent(QWheelEvent* event)
{
    const auto event_delta = event->angleDelta();
    if (event_delta.isNull())
    {
        return;
    }

    if (m_enable_camera_navigation_undo)
    {
        m_tool_undo_block = std::make_unique<commands::UsdEditsUndoBlock>();
    }

    const double zoom_delta = (event_delta.x() == 0 && event_delta.y() > 0) || (event_delta.y() == 0 && event_delta.x() > 0) ? 0.88 : 1.12;
    m_camera_controller->adjust_distance(zoom_delta);
    m_event_dispatcher.dispatch(EventType::VIEW_UPDATE);
    update();

    if (m_enable_camera_navigation_undo)
    {
        m_camera_controller->get_gf_camera(); // hack to push camera transform changes in undo block
        m_tool_undo_block = nullptr;
    }
}

void ViewportGLWidget::exec_context_menu(QContextMenuEvent::Reason reason, const QPoint& pos, const QPoint& global_pos,
                                         Qt::KeyboardModifiers modifiers, ulong timestamp)
{
    auto context_menu_event = QContextMenuEvent(reason, pos, global_pos, modifiers);
    context_menu_event.setTimestamp(timestamp);

    if (auto menu =
            ViewportContextMenuRegistry::instance().create_menu(m_scene_context->get_context_name(), &context_menu_event, m_viewport_view, this))
    {
        if (menu->actions().empty())
            return;

        menu->exec(global_pos);
    }
}

void ViewportGLWidget::dragEnterEvent(QDragEnterEvent* event)
{
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
    {
        event->ignore();
        return;
    }
    m_drag_and_drop_controller->on_enter(m_viewport_view, event);
    update();
}

void ViewportGLWidget::dragMoveEvent(QDragMoveEvent* event)
{
    m_drag_and_drop_controller->on_move(m_viewport_view, event);
    update();
}

void ViewportGLWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_drag_and_drop_controller->on_leave(m_viewport_view, event);
    update();
}

void ViewportGLWidget::dropEvent(QDropEvent* event)
{
    m_drag_and_drop_controller->on_drop(m_viewport_view, event);
    update();
}

void ViewportGLWidget::set_enable_scene_materials(bool enable)
{
    m_params.enable_scene_materials = enable;
    update();
}

bool ViewportGLWidget::enable_scene_materials() const
{
    return m_params.enable_scene_materials;
}

void ViewportGLWidget::set_cull_backfaces(bool cull_backfaces)
{
    m_params.cull_style = cull_backfaces ? ViewportHydraCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED : ViewportHydraCullStyle::CULL_STYLE_NOTHING;
    update();
}

bool ViewportGLWidget::cull_backfaces() const
{
    return m_params.cull_style == ViewportHydraCullStyle::CULL_STYLE_BACK_UNLESS_DOUBLE_SIDED;
}

void ViewportGLWidget::set_draw_mode(ViewportHydraDrawModeMask draw_mode)
{
    if (m_params.draw_mode == draw_mode)
        return;

    m_params.draw_mode = draw_mode;
    update();
}

ViewportHydraDrawModeMask ViewportGLWidget::get_draw_mode()
{
    return m_params.draw_mode;
}

void ViewportGLWidget::set_use_camera_light(bool use_camera_light)
{
    if (m_params.use_camera_light != use_camera_light)
    {
        m_params.use_camera_light = use_camera_light;
        update();
    }
}

bool ViewportGLWidget::use_camera_light() const
{
    return m_params.use_camera_light;
}

void ViewportGLWidget::set_enable_shadows(bool enable_shadows)
{
    m_params.enable_shadows = enable_shadows;
    update();
}

bool ViewportGLWidget::are_shadows_enabled() const
{
    return m_params.enable_shadows;
}

void ViewportGLWidget::set_color_mode(const std::string& color_mode)
{
    m_params.color_correction_mode = TfToken(color_mode.c_str());
    if (!m_color_correction)
        return;
    if (color_mode == "openColorIO")
        m_color_correction->set_mode(ViewportColorCorrection::OCIO);
    else if (color_mode == "sRGB")
        m_color_correction->set_mode(ViewportColorCorrection::SRGB);
    else
        m_color_correction->set_mode(ViewportColorCorrection::DISABLED);
    update();
}

std::string ViewportGLWidget::get_color_mode()
{
    return std::string(m_params.color_correction_mode);
}

void ViewportGLWidget::set_view_OCIO(const std::string& view_ocio)
{
    m_params.view_OCIO = view_ocio;
    if (m_color_correction)
        m_color_correction->set_ocio_view(view_ocio);
    update();
}

std::string ViewportGLWidget::get_view_ocio() const
{
    return m_params.view_OCIO;
}

void ViewportGLWidget::set_gamma(float gamma)
{
    m_params.gamma = gamma;
    if (m_color_correction)
        m_color_correction->set_gamma(gamma);
    update();
}

float ViewportGLWidget::get_gamma() const
{
    return m_params.gamma;
}

void ViewportGLWidget::set_exposure(float exposure)
{
    m_params.exposure = exposure;
    if (m_color_correction)
        m_color_correction->set_exposure(exposure);
    update();
}

float ViewportGLWidget::get_exposure() const
{
    return m_params.exposure;
}

void ViewportGLWidget::update_cursor()
{
    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    if (tool)
    {
        auto tool_cursor = tool->get_cursor();
        if (tool_cursor)
        {
            if (m_tool_cursor != tool_cursor)
            {
                setCursor(*tool_cursor);
                m_tool_cursor = tool_cursor;
            }
        }
        else
        {
            if (m_tool_cursor)
            {
                unsetCursor();
                m_tool_cursor = nullptr;
            }
        }
    }
    else
    {
        if (m_tool_cursor)
        {
            unsetCursor();
            m_tool_cursor = nullptr;
        }
    }
}

std::pair<PXR_NS::HdxPickHitVector, bool> ViewportGLWidget::intersect(const PXR_NS::GfVec2f& point, SelectionList::SelectionMask pick_target,
                                                                      bool resolve_to_usd /*= false*/,
                                                                      const PXR_NS::HdRprimCollection* custom_collection /*= nullptr*/,
                                                                      const TfTokenVector& render_tags /*= TfTokenVector()*/)
{
    const GfVec2f pick_point(point);
    const GfVec2f start = pick_point - GfVec2f(2, -2);
    const GfVec2f end = pick_point + GfVec2f(2, -2);

    auto result = intersect_impl(start, end, pick_target, custom_collection, render_tags, HdxPickTokens->resolveNearestToCenter);
    if (result.second)
    {
        m_scene_context->resolve_picking(result.first);
    }

    return result;
}

std::pair<PXR_NS::HdxPickHitVector, bool> ViewportGLWidget::intersect(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end,
                                                                      SelectionList::SelectionMask pick_target, bool resolve_to_usd /*= false*/,
                                                                      const PXR_NS::HdRprimCollection* custom_collection /*= nullptr*/,
                                                                      const TfTokenVector& render_tags /*= TfTokenVector()*/)
{
    auto result = intersect_impl(start, end, pick_target, custom_collection, render_tags, HdxPickTokens->resolveUnique);
    if (result.second)
    {
        m_scene_context->resolve_picking(result.first);
    }
    return result;
}

SelectionList ViewportGLWidget::pick_single_prim(const GfVec2f& point, SelectionList::SelectionMask pick_target)
{
    auto pick_result = intersect(point, pick_target);
    if (!pick_result.second)
    {
        return {};
    }
    return make_selection_list(pick_result.first, pick_target);
}

SelectionList ViewportGLWidget::pick_multiple_prim(const GfVec2f& start, const GfVec2f& end, SelectionList::SelectionMask pick_target)
{
    auto pick_result = intersect(start, end, pick_target);
    return make_selection_list(pick_result.first, pick_target);
}

PXR_NS::GfCamera ViewportGLWidget::get_camera()
{
    return m_camera_controller->get_gf_camera();
}

void ViewportGLWidget::set_display_purpose(ViewportHydraDisplayPurpose display_purpose, bool enable)
{
    switch (display_purpose)
    {
    case ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_GUIDE:
        m_params.show_guides = enable;
        break;
    case ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_PROXY:
        m_params.show_proxy = enable;
        break;
    case ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_RENDER:
        m_params.show_render = enable;
        break;
    }
}

bool ViewportGLWidget::is_display_purpose_enabled(ViewportHydraDisplayPurpose display_purpose)
{
    switch (display_purpose)
    {
    case ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_GUIDE:
        return m_params.show_guides;
    case ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_PROXY:
        return m_params.show_proxy;
    case ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_RENDER:
        return m_params.show_render;
    default:
        TF_RUNTIME_ERROR("Unknown display purpose option.");
        return false;
    }
}

void ViewportGLWidget::add_invisible_path(const SdfPath& path)
{
    m_params.invised_paths.insert(path);
    m_params.invised_paths_dirty = true;
    update();
}

void ViewportGLWidget::remove_invisible_path(const SdfPath& path)
{
    m_params.invised_paths.erase(path);
    m_params.invised_paths_dirty = true;
    update();
}

void ViewportGLWidget::set_invised_paths(const std::unordered_set<SdfPath, SdfPath::Hash>& invised_paths /*= {}*/)
{
    m_params.invised_paths = invised_paths;
    m_params.invised_paths_dirty = true;
    update();
}

const std::unordered_set<SdfPath, SdfPath::Hash>& ViewportGLWidget::get_invised_paths() const
{
    return m_params.invised_paths;
}

void ViewportGLWidget::set_rollover_prims(const SdfPathVector& paths)
{
    get_engine()->set_rollover_prims(paths);
}

void ViewportGLWidget::add_populated_paths(const PXR_NS::SdfPathVector& paths)
{
    SdfPathVector result;
    auto sorted_paths = paths;
    std::sort(sorted_paths.begin(), sorted_paths.end());
    std::set_union(m_params.populated_paths.begin(), m_params.populated_paths.end(), sorted_paths.begin(), sorted_paths.end(),
                   std::back_inserter(result));
    set_populated_paths(std::move(result));
}

void ViewportGLWidget::remove_populated_paths(const PXR_NS::SdfPathVector& paths)
{
    SdfPathVector result;
    auto sorted_paths = paths;
    std::sort(sorted_paths.begin(), sorted_paths.end());
    std::set_difference(m_params.populated_paths.begin(), m_params.populated_paths.end(), sorted_paths.begin(), sorted_paths.end(),
                        std::back_inserter(result));
    set_populated_paths(std::move(result));
}

void ViewportGLWidget::set_populated_paths(const PXR_NS::SdfPathVector& paths)
{
    if (m_params.populated_paths == paths)
        return;

    m_params.populated_paths = paths;
    SdfPath::RemoveDescendentPaths(&m_params.populated_paths);
    update();
}

void ViewportGLWidget::set_populated_paths(PXR_NS::SdfPathVector&& paths)
{
    SdfPath::RemoveDescendentPaths(&paths);
    m_params.populated_paths = std::move(paths);
    update();
}

SdfPathVector ViewportGLWidget::get_populated_paths() const
{
    return m_params.populated_paths;
}

TfToken ViewportGLWidget::get_scene_context_type() const
{
    return m_scene_context->get_context_name();
}

void OPENDCC_NAMESPACE::ViewportGLWidget::set_visibility_type(bool visible, const PXR_NS::TfToken& type, const PXR_NS::TfToken& group)
{
    m_params.visibility_mask.set_visible(visible, type, group);
    update();
}

ViewportGLWidget::CallbackHandle ViewportGLWidget::register_event_callback(const EventType& event_name, std::function<void()> func)
{
    return m_event_dispatcher.appendListener(event_name, func);
}

void ViewportGLWidget::unregister_event_callback(const EventType& event_type, CallbackHandle& handle)
{
    m_event_dispatcher.removeListener(event_type, handle);
}

void ViewportGLWidget::set_enable_camera_navigation_undo(bool enable)
{
    m_enable_camera_navigation_undo = enable;
}

bool ViewportGLWidget::enable_camera_navigation_undo() const
{
    return m_enable_camera_navigation_undo;
}

void ViewportGLWidget::set_draw_extensions(std::vector<IViewportDrawExtensionPtr>& extensions)
{
    m_extensions = extensions;
}

#if PXR_VERSION >= 2205
void ViewportGLWidget::set_compositor(const std::shared_ptr<Compositor> compositor)
{
    m_compositor = compositor;
}
#endif

void ViewportGLWidget::set_scene_context(std::shared_ptr<ViewportSceneContext> scene_context)
{
    m_scene_context = scene_context;
    if (m_scene_context->use_hydra2())
    {
        get_engine()->set_scene_index_manager(m_scene_context->get_index_manager());
    }
    else
    {
        get_engine()->set_scene_delegates(m_scene_context->get_delegates());
    }
    m_scene_context->register_event_handler(ViewportSceneContext::EventType::DirtyRenderSettings, [this] { set_render_settings_to_engine(); });
    m_drag_and_drop_controller->set_scene_context(m_scene_context->get_context_name());
    ViewportCameraMapperPtr cam_map;
    if (scene_context->get_context_name() == TfToken("USD"))
        cam_map = std::make_shared<ViewportUsdCameraMapper>(SdfPath());
    else
        cam_map = ViewportCameraMapperFactory::create_camera_mapper(m_scene_context->get_context_name());
    m_camera_controller->update_camera_mapper(cam_map);
    Application::instance().set_active_view_scene_context(m_scene_context->get_context_name());
    set_render_settings_to_engine();
    update();
}

void ViewportGLWidget::set_crop_region(const GfRect2i& crop_region)
{
    m_params.crop_region = crop_region;
}

GfRect2i ViewportGLWidget::get_crop_region() const
{
    return m_params.crop_region;
}

void ViewportGLWidget::set_stage_resolver(std::shared_ptr<IStageResolver> stage_resolver)
{
    m_params.stage_resolver = stage_resolver;
    m_params.current_stage_root = SdfPath::AbsoluteRootPath();
    auto time = UsdTimeCode(0);
    if (!m_params.stage_resolver)
    {
        m_params.frame = Application::instance().get_current_time();
        time = m_params.frame;
    }
    else if (auto stage = Application::instance().get_session()->get_current_stage())
    {
        m_params.current_stage_root = m_params.current_stage_root.AppendChild(TfToken(TfMakeValidIdentifier(stage->GetRootLayer()->GetIdentifier())));
    }

    if (m_scene_context->get_context_name() == TfToken("USD"))
    {
        ViewportCameraMapperPtr cam_map;
        if (!m_params.stage_resolver)
            cam_map = std::make_shared<ViewportUsdCameraMapper>(m_camera_controller->get_follow_prim_path());
        else
        {

            cam_map = m_params.stage_resolver->create_camera_mapper(m_camera_controller->get_follow_prim_path(), this);
        }
        m_camera_controller->update_camera_mapper(cam_map);
    }
    update();
}

UsdTimeCode ViewportGLWidget::get_sequence_time() const
{
    return m_params.frame;
}

void OPENDCC_NAMESPACE::ViewportGLWidget::set_sequence_time(PXR_NS::UsdTimeCode time)
{
    if (m_params.stage_resolver)
    {
        m_params.frame = time;
        m_camera_controller->set_time(time);
        update();
    }
}

void ViewportGLWidget::draw_headup_display_text()
{
    std::string camera_text = m_camera_controller->get_follow_prim_path().GetString();
    if (camera_text.empty())
        camera_text = "Def Cam";

    QRectF area(0, 0, width(), height() - 10);
    // Draw twice with offset to emulate text shadow
    QPainter painter(this);
    painter.setPen(Qt::black);
    painter.translate(QPoint(1, 0));
    painter.drawText(area, Qt::AlignHCenter | Qt::AlignBottom, camera_text.c_str());

    painter.setPen(Qt::white);
    painter.translate(QPoint(-1, 0));
    painter.drawText(area, Qt::AlignHCenter | Qt::AlignBottom, camera_text.c_str());
    painter.end();
}

OPENDCC_NAMESPACE_CLOSE
