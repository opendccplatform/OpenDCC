/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include <QWidget>
#include "opendcc/base/vendor/eventpp/eventdispatcher.h"
#include <unordered_set>
#include <pxr/base/tf/token.h>
#include "opendcc/app/viewport/viewport_render_settings_dialog.h"
#include "opendcc/app/viewport/viewport_camera_mapper_factory.h"
#include "opendcc/app/viewport/iviewport_ui_extension.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/viewport/visibility_mask.h"

class QMenuBar;
class QMenu;
class QAction;
class QToolBar;
class QActionGroup;
class QFocusEvent;
class QComboBox;

OPENDCC_NAMESPACE_OPEN
class ViewportGLWidget;
class ViewportSceneContext;
class ViewportCameraMenuController;
class ViewportView;
class ViewportOverlay;

typedef eventpp::EventDispatcher<std::string, void()>::Handle BasicEventDispatcherHandle;

class OPENDCC_API ViewportWidget : public QWidget
{
    Q_OBJECT
public:
    enum class FeatureFlags
    {
        VIEWPORT,
        SEQUENCE_VIEW
    };
    ViewportWidget(std::shared_ptr<ViewportSceneContext> scene_context, FeatureFlags feature = FeatureFlags::VIEWPORT);
    /** @brief Construct a USD viewport with normal Qt parent ownership. */
    explicit ViewportWidget(QWidget* parent = nullptr);
    virtual ~ViewportWidget();

    inline ViewportGLWidget* get_gl_widget() { return m_glwidget; }

    std::shared_ptr<ViewportView> get_viewport_view() const { return m_viewport_view; }
    QMenuBar* get_menubar() { return m_menubar; }
    QToolBar* get_toolbar() { return m_toolbar; }

    ViewportOverlay* get_overlay() { return m_viewport_overlay; }

    // temporary workaround until we figure out how to scale icons automagically on toolbar
    void toolbar_add_action(QAction* action);

    // register camera menu for switching viewport scene context
    // note that contexts are exclusive
    QAction* add_scene_context(const PXR_NS::TfToken& context);

    // allow override construction of the camera dynamic select menus
    void set_camera_menu_controller(ViewportCameraMenuController* camera_controller);

    void fill_camera_menu_controller_actions();
    void clear_camera_menu_controller_actions();

    std::shared_ptr<ViewportSceneContext> get_scene_context() const { return m_scene_context; }

    static const std::unordered_set<ViewportWidget*>& get_live_widgets();
    static void update_all_gl_widget();
Q_SIGNALS:
    void render_plugin_changed(const PXR_NS::TfToken& render_plugin);
    void scene_context_changed(const PXR_NS::TfToken& context);

protected:
    virtual bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void init_menu();
    void on_current_stage_changed();
    void update_render_actions();
    // probably needs to be removed in the future
    void setup_toolbar_action(QAction* action);

#if PXR_VERSION >= 2005
    void update_displayed_aovs();
    void on_render_settings_changed();
    QComboBox* m_aov_combobox = nullptr;
#endif
    static std::unordered_set<ViewportWidget*> m_live_widgets;

    ViewportGLWidget* m_glwidget;
    ViewportRenderSettingsDialog* m_render_settings_dialog = nullptr;
    std::shared_ptr<ViewportSceneContext> m_scene_context;
    std::shared_ptr<ViewportView> m_viewport_view;
    ;
    QMenuBar* m_menubar;
    QToolBar* m_toolbar;
    QMenu* m_view_menu;
    QMenu* m_visibility_types_menu;
    QMenu* m_scene_context_menu;
    QActionGroup* m_scene_context_action_group = nullptr;
    QActionGroup* m_toolbar_usd_context_group = nullptr;
    QActionGroup* m_renderer_menu_group = nullptr;
    QAction* m_select_camera_action;
    QAction* m_create_camera_from_view = nullptr;
    QAction* m_enable_scene_materials_action = nullptr;

    std::unique_ptr<ViewportCameraMenuController> m_camera_menu_controller;

    Application::CallbackHandle m_current_stage_changed_cid;
    PrimVisibilityRegistry::CallbackHandle m_visibility_types_changed_cid;
    QComboBox* m_view_transform;
    QAction* m_isolate_selection = nullptr;

    struct
    {
        QAction* pause = nullptr;
        QAction* resume = nullptr;
        QAction* restart = nullptr;
    } m_render_actions;

    std::vector<IViewportUiExtensionPtr> m_extensions_list;

    ViewportOverlay* m_viewport_overlay = nullptr;
    FeatureFlags m_feature_flags;
};

OPENDCC_NAMESPACE_CLOSE
