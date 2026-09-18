/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/app/ui/qt_utils.h"

#include "opendcc/app/core/api.h"
#include "opendcc/app/core/settings.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/stage_watcher.h"
#include "opendcc/app/core/undo/block.h"

#include "opendcc/app/viewport/def_cam_settings.h"
#include "opendcc/app/viewport/viewport_engine_proxy.h"
#include "opendcc/app/viewport/viewport_scene_context.h"
#include "opendcc/app/viewport/viewport_refine_manager.h"
#include "opendcc/app/viewport/iviewport_draw_extension.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include "opendcc/app/viewport/viewport_camera_mapper_factory.h"

#include <pxr/pxr.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/camera.h>

#include <QOpenGLWidget>
#include <QContextMenuEvent>

#include <string>

class QMouseEvent;
class QKeyEvent;
class QWheelEvent;

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class UsdEditsUndoBlock;
}

#if PXR_VERSION >= 2205
class Compositor;
#endif
class ViewportView;
class ViewportGrid;
class SelectionList;
class IStageResolver;
class HydraRenderSettings;
class ViewportUiDrawManager;
class ViewportDndController;
class ViewportColorCorrection;
class ViewportBackgroundFiller;

class OPENDCC_API ViewportGLWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    ViewportGLWidget(std::shared_ptr<ViewportView> viewport_view, std::shared_ptr<ViewportSceneContext> scene_context, QWidget* parent = nullptr);
    virtual ~ViewportGLWidget();

    enum class EventType
    {
        VIEW_UPDATE
    };

    using EventDispatcher = eventpp::EventDispatcher<EventType, void()>;
    using CallbackHandle = eventpp::EventDispatcher<EventType, void()>::Handle;

    void enable_engine(bool val);

    void initializeGL() override;
    void paintGL() override;

    std::shared_ptr<ViewportEngineProxy> get_engine() const;
    void close_engine();

    void set_enable_scene_materials(bool enable);
    bool enable_scene_materials() const;

    void set_cull_backfaces(bool cull_backfaces);
    bool cull_backfaces() const;

    void set_draw_mode(ViewportHydraDrawModeMask draw_mode);
    ViewportHydraDrawModeMask get_draw_mode();

    void set_use_camera_light(bool use_camera_light);
    bool use_camera_light() const;

    void set_enable_shadows(bool enable_shadows);
    bool are_shadows_enabled() const;

    void set_color_mode(const std::string& color_mode);
    std::string get_color_mode();

    void set_view_OCIO(const std::string& view_ocio);
    std::string get_view_ocio() const;

    void set_gamma(float gamma);
    float get_gamma() const;

    void set_exposure(float exposure);
    float get_exposure() const;

    void update_cursor();

    std::pair<PXR_NS::HdxPickHitVector, bool> intersect(const PXR_NS::GfVec2f& point, SelectionList::SelectionMask pick_target,
                                                        bool resolve_to_usd = false, const PXR_NS::HdRprimCollection* custom_collection = nullptr,
                                                        const PXR_NS::TfTokenVector& render_tags = PXR_NS::TfTokenVector());
    std::pair<PXR_NS::HdxPickHitVector, bool> intersect(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end,
                                                        SelectionList::SelectionMask pick_target, bool resolve_to_usd = false,
                                                        const PXR_NS::HdRprimCollection* custom_collection = nullptr,
                                                        const PXR_NS::TfTokenVector& render_tags = PXR_NS::TfTokenVector());

    SelectionList pick_single_prim(const PXR_NS::GfVec2f& point, SelectionList::SelectionMask pick_target);
    SelectionList pick_multiple_prim(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end, SelectionList::SelectionMask pick_target);

    PXR_NS::GfCamera get_camera();
    ViewportCameraControllerPtr get_camera_controller() { return m_camera_controller; }

    void set_display_purpose(ViewportHydraDisplayPurpose display_purpose, bool enable);
    bool is_display_purpose_enabled(ViewportHydraDisplayPurpose display_purpose);

    void add_invisible_path(const PXR_NS::SdfPath& path);
    void remove_invisible_path(const PXR_NS::SdfPath& path);
    void set_invised_paths(const std::unordered_set<PXR_NS::SdfPath, PXR_NS::SdfPath::Hash>& invised_paths = {});
    const std::unordered_set<PXR_NS::SdfPath, PXR_NS::SdfPath::Hash>& get_invised_paths() const;

    void set_rollover_prims(const PXR_NS::SdfPathVector& paths);

    void add_populated_paths(const PXR_NS::SdfPathVector& paths);
    void remove_populated_paths(const PXR_NS::SdfPathVector& paths);
    void set_populated_paths(const PXR_NS::SdfPathVector& paths);
    void set_populated_paths(PXR_NS::SdfPathVector&& paths);
    PXR_NS::SdfPathVector get_populated_paths() const;
    PXR_NS::TfToken get_scene_context_type() const;
    void set_visibility_type(bool visible, const PXR_NS::TfToken& type, const PXR_NS::TfToken& group = PXR_NS::TfToken());
    /*
    view_update
    */
    CallbackHandle register_event_callback(const EventType& event_name, std::function<void()> func);
    void unregister_event_callback(const EventType& event_type, CallbackHandle& handle);

    void set_enable_camera_navigation_undo(bool enable);
    bool enable_camera_navigation_undo() const;

    void set_draw_extensions(std::vector<IViewportDrawExtensionPtr>& extensions);
#if PXR_VERSION >= 2205
    void set_compositor(const std::shared_ptr<Compositor> compositor);
#endif

    void set_scene_context(std::shared_ptr<ViewportSceneContext> scene_context);

    void set_stage_resolver(std::shared_ptr<IStageResolver> stage_resolver);
    void set_sequence_time(PXR_NS::UsdTimeCode time);
    PXR_NS::UsdTimeCode get_sequence_time() const;
    const ViewportHydraEngineParams& get_render_params() const { return m_params; }

    void set_crop_region(const PXR_NS::GfRect2i& framing);
    PXR_NS::GfRect2i get_crop_region() const;

Q_SIGNALS:
    void gl_initialized();
    void render_settings_changed();

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    std::pair<PXR_NS::HdxPickHitVector, bool> intersect_impl(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end,
                                                             SelectionList::SelectionMask pick_target,
                                                             const PXR_NS::HdRprimCollection* custom_collection,
                                                             const PXR_NS::TfTokenVector& render_tags, const PXR_NS::TfToken& resolve_mode);

    SelectionList make_selection_list(const PXR_NS::HdxPickHitVector& pick_hits, SelectionList::SelectionMask selection_mask) const;
    void on_camera_changed(PXR_NS::SdfPath follow_path);
    void update_stage_watcher();
    void register_callbacks();
    void set_render_settings_to_engine();

    enum MouseMode
    {
        MouseModeNone,
        MouseModeTumble,
        MouseModeZoom,
        MouseModeTruck
    };

    enum class MouseControlMode
    {
        NONE,
        TOOL_CONTEXT,
        CAMERA
    };

    QCursor* m_tool_cursor = nullptr;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void draw_headup_display_text();
    void exec_context_menu(QContextMenuEvent::Reason reason, const QPoint& pos, const QPoint& global_pos, Qt::KeyboardModifiers modifiers,
                           ulong timestamp);

    bool m_show_camera = true;
    struct GridSettings
    {
        PXR_NS::GfVec4f lines_color = PXR_NS::GfVec4f(0.59462, 0.59462, 0.59462, 1);
        double min_step = 1.0;
        bool enable = true;
    } m_grid_settings;
    bool m_engine_enabled = true;
    // initializeGL() runs again on every GL context recreation; the callbacks must be registered once.
    bool m_callbacks_registered = false;
    bool m_enable_camera_navigation_undo = false;
    MouseControlMode m_mouse_control_mode = MouseControlMode::NONE;
    MouseMode m_mousemode;
    int m_mousex, m_mousey;
    PXR_NS::SdfPath m_camera_prim_path;
    bool m_enable_background_gradient = false;
    std::unique_ptr<ViewportBackgroundFiller> m_background_drawer;
    std::unique_ptr<ViewportGrid> m_grid;
    std::unique_ptr<ViewportColorCorrection> m_color_correction;
    mutable std::shared_ptr<ViewportEngineProxy> m_engine;
    ViewportHydraEngineParams m_params;
    ViewportCameraControllerPtr m_camera_controller;
    ViewportUiDrawManager* m_ui_draw_manager;
    std::shared_ptr<ViewportView> m_viewport_view;
    std::unique_ptr<commands::UsdEditsUndoBlock> m_tool_undo_block;

    std::shared_ptr<ViewportSceneContext> m_scene_context;
    std::unique_ptr<ViewportDndController> m_drag_and_drop_controller;

    EventDispatcher m_event_dispatcher;

    Application::CallbackHandle m_selection_changed_cid;
    Application::CallbackHandle m_current_stage_changed_cid;

    std::shared_ptr<StageObjectChangedWatcher> m_stage_watcher;
    Application::CallbackHandle m_current_time_changed_cid;
    Application::CallbackHandle m_before_stage_closed_cid;
    Application::CallbackHandle m_selection_mode_changed_cid;
    Settings::SettingChangedHandle m_show_camera_cid;
    UsdRefineHandle m_usd_refine_level_changed_cid;
    UsdStageClearedHandle m_usd_stage_cleared_cid;
    DefCamSettingsDispatcherHandle m_def_cam_settings_dispatcher_handle;
    std::unordered_map<std::string, Settings::SettingChangedHandle> m_setting_changed_cids;

    std::vector<IViewportDrawExtensionPtr> m_extensions;
#if PXR_VERSION >= 2205
    std::shared_ptr<Compositor> m_compositor;
#endif
};
OPENDCC_NAMESPACE_CLOSE
