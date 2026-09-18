// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/qt_python.h"
#include "viewport_widget.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/viewport/viewport_widget.h"

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QEvent>
#include <QActionGroup>
#include <QComboBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QKeySequence>

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>

#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include "viewport_isolate_selection_command.h"
#include "opendcc/app/viewport/viewport_scene_context.h"
#include "opendcc/app/viewport/viewport_refine_manager.h"

#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_camera_menu_controller.h"

#include "opendcc/app/viewport/viewport_overlay.h"

#include <OpenColorIO/OpenColorIO.h>

#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/app/viewport/visibility_mask.h"

#include <QDebug>
#include <QApplication>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

std::unordered_set<ViewportWidget*> ViewportWidget::m_live_widgets;

void ViewportWidget::setup_toolbar_action(QAction* action)
{
    QWidget* tool_widget = m_toolbar->widgetForAction(action);
    tool_widget->setMaximumSize(QSize(20, 20));
}

ViewportWidget::ViewportWidget(std::shared_ptr<ViewportSceneContext> scene_context, FeatureFlags feature)
    : m_scene_context(scene_context)
    , m_feature_flags(feature)
{
    if (m_feature_flags == FeatureFlags::VIEWPORT)
        m_live_widgets.emplace(this);
    setProperty("unfocusedKeyEvent_enable", true);
    QVBoxLayout* opengl_layout = new QVBoxLayout;
    opengl_layout->setContentsMargins(0, 0, 0, 0);
    opengl_layout->setSpacing(0);
    m_viewport_view = std::make_shared<ViewportView>();
    m_glwidget = new ViewportGLWidget(m_viewport_view, m_scene_context, this);
    opengl_layout->addWidget(m_glwidget);
    m_viewport_overlay = new ViewportOverlay(m_glwidget);

    QBoxLayout* main_layout = new QBoxLayout(QBoxLayout::TopToBottom, this);

    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    m_toolbar = new QToolBar;
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar_usd_context_group = new QActionGroup(this);
    m_toolbar_usd_context_group->setExclusive(false);

    m_select_camera_action = new QAction(QIcon(":/icons/select_camera"), i18n("viewport.actions", "Select Camera"), this);
    utils::action_set_object_name_from_text(m_select_camera_action, "viewport_set", "select_camera");
    connect(m_select_camera_action, &QAction::triggered, [this](bool checked) {
        auto path = m_glwidget->get_camera_controller()->get_follow_prim_path();
        if (!path.IsEmpty())
        {
            auto stage = Application::instance().get_session()->get_current_stage();
            auto prim = stage->GetPrimAtPath(path);

            if (prim.IsValid())
            {
                commands::UsdEditsUndoBlock undo_block;
                commands::utils::select_prims({ path });
            }
        }
    });

    toolbar_add_action(m_select_camera_action);

    m_create_camera_from_view = new QAction(i18n("viewport.menu_bar.view", "Create Camera from View"), this);
    m_create_camera_from_view->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    utils::action_set_object_name_from_text(m_create_camera_from_view, "viewport_set", "create_camera_from_view");
    connect(m_create_camera_from_view, &QAction::triggered, [this](bool checked) {
        auto stage = Application::instance().get_session()->get_current_stage();

        if (!stage)
        {
            return;
        }

        auto camera_controller = m_glwidget->get_camera_controller();

        GfCamera current_camera = camera_controller->get_gf_camera();

        auto res = CommandInterface::execute("create_camera_from_view", CommandArgs().arg(current_camera));

        if (!res.is_successful())
        {
            qWarning() << "Failed to create camera";
            return;
        }

        camera_controller->set_follow_prim(*res.get_result<SdfPath>());
    });
    // toolbar_add_action(m_create_camera_from_view); // Maya doesn't have it there

    m_toolbar->addSeparator();

    auto settings = Application::instance().get_settings();
    auto default_view_transform = settings->get("colormanagement.ocio_view_transform", "sRGB");

    const double default_gamma = 1.0;
    const double default_exposure = 0.0;

    auto gamma_icon = new QLabel;
    gamma_icon->setScaledContents(true);
    gamma_icon->setFixedSize(QSize(16, 16));
    gamma_icon->setPixmap(QPixmap(":/icons/gamma"));
    gamma_icon->setToolTip(i18n("viewport.actions", "Gamma"));
    m_toolbar->addWidget(gamma_icon);

    auto init_color_adjustment_widget = [&](double init_value, const QString& tooltip,
                                            std::function<void(double)> value_setter_fn) -> QDoubleSpinBox* {
        QDoubleSpinBox* widget = new QDoubleSpinBox;
        widget->setToolTip(tooltip);
        widget->setButtonSymbols(QAbstractSpinBox::NoButtons);
        widget->setFixedWidth(40);
        widget->setFixedHeight(20);
        widget->setMaximum(1e10);
        widget->setMinimum(-1e10);
        widget->setValue(init_value);
        widget->setLocale(QLocale(QLocale::Hawaiian, QLocale::UnitedStates));

        QObject::connect(widget, &QDoubleSpinBox::editingFinished, [this, widget, value_setter_fn] {
            double value = widget->value();
            widget->setValue(value);
            value_setter_fn(value);
        });
        return widget;
    };

    auto gamma_widget =
        init_color_adjustment_widget(default_gamma, i18n("viewport.actions", "Gamma"), [&](double value) { get_gl_widget()->set_gamma(value); });
    m_toolbar->addWidget(gamma_widget);

    auto exposure_icon = new QLabel;
    exposure_icon->setScaledContents(true);
    exposure_icon->setFixedSize(QSize(16, 16));
    exposure_icon->setPixmap(QPixmap(":/icons/exposure"));
    exposure_icon->setToolTip(i18n("viewport.actions", "Exposure"));
    m_toolbar->addWidget(exposure_icon);

    auto exposure_widget = init_color_adjustment_widget(default_exposure, i18n("viewport.actions", "Exposure"),
                                                        [&](double value) { get_gl_widget()->set_exposure(value); });
    m_toolbar->addWidget(exposure_widget);
    m_toolbar->addSeparator();

    m_view_transform = new QComboBox;
    m_view_transform->setToolTip(i18n("viewport.actions", "OCIO View Transform"));
    m_view_transform->setFixedHeight(20);
    m_toolbar->addWidget(m_view_transform);

    namespace OCIO = OCIO_NAMESPACE; // OpenColorIO;
    auto config = OCIO::GetCurrentConfig();
    auto default_display = config->getDefaultDisplay();
    for (int i = 0; i < config->getNumViews(default_display); i++)
    {
        m_view_transform->addItem(config->getView(default_display, i));
    }
    m_view_transform->setCurrentText(QString(default_view_transform.c_str()));
    m_view_transform->connect(m_view_transform, qOverload<int>(&QComboBox::activated), this,
                              [&](int index) { get_gl_widget()->set_view_OCIO(m_view_transform->itemText(index).toStdString()); });

    m_toolbar->addSeparator();

#if PXR_VERSION >= 2005
    m_aov_combobox = new QComboBox;
    m_aov_combobox->setToolTip(i18n("viewport.actions", "Displayed AOV"));
    m_aov_combobox->setFixedHeight(20);
    m_toolbar->addWidget(m_aov_combobox);
    m_toolbar->addSeparator();

    m_aov_combobox->connect(m_aov_combobox, &QComboBox::textActivated, this, [this](const QString& aov_name) {
        get_gl_widget()->get_engine()->set_renderer_aov(TfToken(aov_name.toStdString()));
        get_gl_widget()->update();
    });
    connect(m_glwidget, &ViewportGLWidget::render_settings_changed, this, &ViewportWidget::on_render_settings_changed);
#endif
    const auto connection = new QMetaObject::Connection;
    *connection = connect(m_glwidget, &ViewportGLWidget::gl_initialized, [this, connection] {
        auto engine = m_glwidget->get_engine();
        if (auto stage = Application::instance().get_session()->get_current_stage())
        {
            engine->set_render_setting(TfToken("stageMetersPerUnit"), VtValue(UsdGeomGetStageMetersPerUnit(stage)));
        }

#if PXR_VERSION >= 2005
        update_displayed_aovs();
#endif
        m_current_stage_changed_cid = Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED,
                                                                                      std::bind(&ViewportWidget::on_current_stage_changed, this));
        disconnect(*connection);
        delete connection;
    });
    // these properties here are for action's placement
    auto shading_mode_separator = m_toolbar->addSeparator();
    shading_mode_separator->setProperty("shading_mode", true);

    m_enable_scene_materials_action = new QAction(QIcon(":/icons/small_textured"), i18n("viewport.actions", "Enable Scene Materials"));
    utils::action_set_object_name_from_text(m_enable_scene_materials_action, "viewport");
    m_enable_scene_materials_action->setCheckable(true);
    m_enable_scene_materials_action->setChecked(false);
    connect(m_enable_scene_materials_action, &QAction::toggled, [this](bool value) { m_glwidget->set_enable_scene_materials(value); });

    toolbar_add_action(m_enable_scene_materials_action);
    m_toolbar->addSeparator();

    auto lights_separator = m_toolbar->addSeparator();
    lights_separator->setProperty("lights", true);

    m_isolate_selection = new QAction(i18n("viewport.actions", "Isolate Selection"), this);
    m_isolate_selection->setIcon(QIcon(":icons/IsolateSelected.png"));
    m_isolate_selection->setCheckable(true);
    m_isolate_selection->setChecked(false);
    connect(m_isolate_selection, &QAction::triggered, this, [this](bool checked) {
        SdfPathVector selection;
        if (checked)
        {
            selection = Application::instance().get_prim_selection();
        }

        auto isolate_cmd = CommandRegistry::create_command<ViewportIsolateSelectionCommand>("isolate");
        isolate_cmd->set_ui_state(m_glwidget, [this, checked](bool undo) {
            if (!m_glwidget || !m_isolate_selection)
                return;

            m_isolate_selection->setChecked(undo ? !checked : checked);
            m_glwidget->update();
        });
        CommandInterface::execute(isolate_cmd, CommandArgs().kwarg("paths", selection));
    });

    m_toolbar_usd_context_group->addAction(m_isolate_selection);
    toolbar_add_action(m_isolate_selection);

    main_layout->addWidget(m_toolbar);

    main_layout->addLayout(opengl_layout);
    setLayout(main_layout);

    m_menubar = new QMenuBar;
    m_menubar->setContentsMargins(0, 0, 0, 0);
    main_layout->setMenuBar(m_menubar);
    m_view_menu = new QMenu(i18n("viewport.menu_bar", "View"));
    m_menubar->addMenu(m_view_menu);

    m_camera_menu_controller =
        std::make_unique<ViewportUsdCameraMenuController>(m_glwidget->get_camera_controller(), m_viewport_overlay->widget(), this);
    // setup this in case someone override default controller for USD context as fallback controller
    if (m_feature_flags == FeatureFlags::VIEWPORT)
    {
        connect(this, &ViewportWidget::scene_context_changed, [this](TfToken context) {
            if (context == TfToken("USD"))
            {
                clear_camera_menu_controller_actions();
                set_camera_menu_controller(
                    new ViewportUsdCameraMenuController(m_glwidget->get_camera_controller(), m_viewport_overlay->widget(), this));
                fill_camera_menu_controller_actions();
                // idea that USD context specific actions will be hidden
                m_toolbar_usd_context_group->setVisible(true);
            }
            else
            {
                m_toolbar_usd_context_group->setVisible(false);
            }
        });
    }
    init_menu();

    m_extensions_list = ViewportUIExtensionRegistry::create_extensions(this);
}

ViewportWidget::~ViewportWidget()
{
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_current_stage_changed_cid);
    PrimVisibilityRegistry::unregister_visibility_types_changes(m_visibility_types_changed_cid);

    if (m_feature_flags == FeatureFlags::VIEWPORT)
    {
        m_live_widgets.erase(this);
        ApplicationUI::instance().set_active_view(m_live_widgets.empty() ? nullptr : *m_live_widgets.begin());
    }
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    if (m_viewport_overlay != nullptr)
    {
        m_viewport_overlay->fit();
    }
    QWidget::resizeEvent(event);
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier)
    {
        auto paths = Application::instance().get_prim_selection();
        auto stage = Application::instance().get_session()->get_current_stage();
        CommandInterface::execute("copy_prims", CommandArgs().arg(paths).kwarg("stage", stage));
    }
    else if (event->key() == Qt::Key_X && event->modifiers() == Qt::ControlModifier)
    {
        auto paths = Application::instance().get_prim_selection();
        auto stage = Application::instance().get_session()->get_current_stage();
        CommandInterface::execute("cut_prims", CommandArgs().arg(paths).kwarg("stage", stage));
    }
    else if (event->key() == Qt::Key_V && event->modifiers() == Qt::ControlModifier)
    {
        auto stage = Application::instance().get_session()->get_current_stage();
        CommandInterface::execute("paste_prims", CommandArgs().arg(SdfPath("/")).kwarg("stage", stage));
    }
    else
    {
        QWidget::keyPressEvent(event);
    }
}

void ViewportWidget::init_menu()
{
    m_view_menu->addAction(m_select_camera_action);
    m_view_menu->addAction(m_create_camera_from_view);

    m_view_menu->addSeparator();

    QMenu* shading_menu = new QMenu(i18n("viewport.menu_bar.view", "Shading Mode"));
    utils::menu_set_object_name_from_title(shading_menu, "viewport", "menu");
    m_view_menu->addMenu(shading_menu);

    QAction* shading_mode_separator = nullptr;
    for (auto action : m_toolbar->actions())
    {
        auto shading_mode = action->property("shading_mode");
        if (shading_mode.isValid())
        {
            if (shading_mode.toBool())
            {
                shading_mode_separator = action;
            }
        }
    }

    QActionGroup* draw_mode_group = new QActionGroup(this);
    draw_mode_group->setExclusive(true);
    auto add_draw_mode_action = [&](QString name, ViewportHydraDrawMode value, QString icon_path = QString(),
                                    QString shortcut = QString()) -> QAction* {
        QAction* draw_mode_action = new QAction(name, this);
        utils::action_set_object_name_from_text(draw_mode_action, "viewport_set", "draw_mode");
        if (!shortcut.isEmpty())
        {
            draw_mode_action->setShortcutContext(Qt::ApplicationShortcut);
            draw_mode_action->setShortcut(shortcut);
        }
        draw_mode_action->setCheckable(true);
        if (!icon_path.isEmpty())
            draw_mode_action->setIcon(QIcon(icon_path));
        connect(draw_mode_action, &QAction::toggled, [this, value](bool checked) {
            if (checked)
            {
                m_glwidget->set_draw_mode(value);
            }
        });
        shading_menu->addAction(draw_mode_action);
        draw_mode_group->addAction(draw_mode_action);
        if (shading_mode_separator)
        {
            m_toolbar->insertAction(shading_mode_separator, draw_mode_action);
            if (!icon_path.isEmpty())
            {
                setup_toolbar_action(draw_mode_action);
            }
        }
        return draw_mode_action;
    };

    add_draw_mode_action(i18n("viewport.actions", "Points"), ViewportHydraDrawMode::DRAW_POINTS, ":/icons/shading_mode_points");
    add_draw_mode_action(i18n("viewport.actions", "Wireframe"), ViewportHydraDrawMode::DRAW_WIREFRAME, ":/icons/shading_mode_wireframe", "4");
    add_draw_mode_action(i18n("viewport.actions", "Wireframe on Surface"), ViewportHydraDrawMode::DRAW_WIREFRAME_ON_SURFACE,
                         ":/icons/shading_mode_wireframe_on_surface");
    QAction* shaded_smooth_action = add_draw_mode_action(i18n("viewport.actions", "Shaded Smooth"), ViewportHydraDrawMode::DRAW_SHADED_SMOOTH,
                                                         ":/icons/shading_mode_shaded_smooth");
    shaded_smooth_action->setChecked(true);
    add_draw_mode_action(i18n("viewport.actions", "Shaded Flat"), ViewportHydraDrawMode::DRAW_SHADED_FLAT, ":/icons/shading_mode_shaded_flat");

    QMenu* color_mode_menu = m_view_menu->addMenu(i18n("viewport.menu_bar.view", "Color Management"));
    utils::menu_set_object_name_from_title(color_mode_menu, "viewport", "menu");
    QActionGroup* color_mode_group = new QActionGroup(this);

    auto add_color_mode_action = [&](const QString& name, const std::string value) -> QAction* {
        QAction* mode_action = new QAction(name, this);
        mode_action->setData(QString(value.c_str()));
        utils::action_set_object_name_from_text(mode_action, "viewport_set", "color_mode");
        mode_action->setCheckable(true);

        connect(mode_action, &QAction::triggered, [this, value](bool) {
            if (value == "openColorIO")
                m_view_transform->setEnabled(true);
            else
                m_view_transform->setEnabled(false);
            m_glwidget->set_color_mode(value);
        });

        color_mode_menu->addAction(mode_action);
        color_mode_group->addAction(mode_action);
        return mode_action;
    };

    add_color_mode_action(i18n("viewport.menu_bar.view", "Disabled"), "disabled");
    add_color_mode_action(i18n("viewport.menu_bar.view", "sRGB"), "sRGB");
    color_mode_menu->addSeparator();
    add_color_mode_action(i18n("viewport.menu_bar.view", "OpenColorIO"), "openColorIO");

    auto default_color_mode = Application::instance().get_settings()->get("colormanagement.color_management", "openColorIO");

    if (default_color_mode == "openColorIO")
        m_view_transform->setEnabled(true);
    else
        m_view_transform->setEnabled(false);

    for (auto& action : color_mode_group->actions())
    {
        if (action->data().toString().toLocal8Bit().data() == default_color_mode)
        {
            action->setChecked(true);
            break;
        }
    }

    QMenu* renderer_menu = new QMenu(i18n("viewport.menu_bar.view", "Hydra Renderer"));
    utils::menu_set_object_name_from_title(renderer_menu, "viewport", "menu");
    m_view_menu->addMenu(renderer_menu);

    m_renderer_menu_group = new QActionGroup(this);
    m_renderer_menu_group->setExclusive(true);

    // this should fire only once, otherwise it's very slow
    QMetaObject::Connection* const connection = new QMetaObject::Connection;
    *connection = QObject::connect(m_glwidget, &ViewportGLWidget::gl_initialized, [this, renderer_menu, connection] {
        /*
        renderer_menu->clear();
        for (auto action : m_renderer_menu_group->actions())
        {
            action->deleteLater();
        }
        */
        auto engine = m_glwidget->get_engine();
        auto render_plugins = engine->get_render_plugins();
        auto current_plugin_id = engine->get_current_render_id();
        for (auto plugin_id : render_plugins)
        {
            QString render_name = engine->get_render_display_name(plugin_id).c_str();
            if (render_name == "GL")
                render_name = "Storm";

            QAction* render_plugin_action = new QAction(render_name, this);
            utils::action_set_object_name_from_text(render_plugin_action, "viewport_set", "hydra_renderer");
            render_plugin_action->setCheckable(true);
            connect(render_plugin_action, &QAction::triggered, [this, plugin_id](bool) {
                auto engine = m_glwidget->get_engine();
                engine->set_renderer_plugin(plugin_id);

                auto render_id = engine->get_current_render_id() == "GL" ? "Storm" : engine->get_current_render_id().GetText();
                for (auto action : m_renderer_menu_group->actions())
                {
                    if (action->text() == render_id)
                    {
                        action->setChecked(true);
                        break;
                    }
                }
#if PXR_VERSION >= 2005
                update_displayed_aovs();
#endif
                const auto stage_meters_per_unit = UsdGeomGetStageMetersPerUnit(Application::instance().get_session()->get_current_stage());
                engine->set_render_setting(TfToken("stageMetersPerUnit"), VtValue(stage_meters_per_unit));

                update_render_actions();
                render_plugin_changed(plugin_id);
                update();
            });
            renderer_menu->addAction(render_plugin_action);
            m_renderer_menu_group->addAction(render_plugin_action);
            m_viewport_overlay->widget()->add_renderer(render_plugin_action);
            if (plugin_id == current_plugin_id)
            {
                render_plugin_action->setChecked(true);
            }
        }
        update_render_actions();
        QObject::disconnect(*connection);
        delete connection;
    });

    QAction* render_settings = new QAction(i18n("viewport.menu_bar.view", "Render Settings"), this);
    render_settings->setObjectName("viewport_show_render_settings");
    m_view_menu->addAction(render_settings);
    connect(render_settings, &QAction::triggered, this, [this] {
        if (m_render_settings_dialog != nullptr)
        {
            m_render_settings_dialog->activateWindow();
            return;
        }

        m_render_settings_dialog = new ViewportRenderSettingsDialog(this, this);
        m_render_settings_dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_render_settings_dialog, &ViewportRenderSettingsDialog::destroyed, this, [this] { m_render_settings_dialog = nullptr; });
        m_render_settings_dialog->show();
    });

    QMenu* display_purpose_menu = new QMenu(i18n("viewport.menu_bar.view", "Display Purpose"));
    utils::menu_set_object_name_from_title(display_purpose_menu, "viewport", "menu");
    m_view_menu->addMenu(display_purpose_menu);

    auto display_purpose_actions = new QActionGroup(this);
    display_purpose_actions->addAction(i18n("viewport.menu_bar.view", "Guide"));
    display_purpose_actions->addAction(i18n("viewport.menu_bar.view", "Proxy"));
    display_purpose_actions->addAction(i18n("viewport.menu_bar.view", "Render"));
    display_purpose_actions->setExclusive(false);
    for (auto& action : display_purpose_actions->actions())
    {
        utils::action_set_object_name_from_text(action, "viewport_set", "display_purpose");
        action->setCheckable(true);
    }
    display_purpose_actions->actions()[1]->setChecked(true);
    display_purpose_menu->addActions(display_purpose_actions->actions());
    connect(display_purpose_actions, &QActionGroup::triggered, this, [this](QAction* action) {
        ViewportHydraDisplayPurpose display_purpose = ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_DEFAULT;
        if (action->text() == "Guide")
        {
            display_purpose = ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_GUIDE;
        }
        else if (action->text() == "Proxy")
        {
            display_purpose = ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_PROXY;
        }
        else if (action->text() == "Render")
        {
            display_purpose = ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_RENDER;
        }
        if (display_purpose != ViewportHydraDisplayPurpose::DISPLAY_PURPOSE_DEFAULT)
        {
            m_glwidget->set_display_purpose(display_purpose, action->isChecked());
            m_glwidget->update();
        }
    });

    m_view_menu->addSeparator();
    m_render_actions.pause = new QAction(i18n("viewport.menu_bar.view", "Pause Render"));
    m_render_actions.pause->setEnabled(false);
    m_render_actions.resume = new QAction(i18n("viewport.menu_bar.view", "Resume Render"));
    m_render_actions.resume->setEnabled(false);
    m_render_actions.restart = new QAction(i18n("viewport.menu_bar.view", "Restart Render"));
    connect(m_render_actions.pause, &QAction::triggered, this, [this] { get_gl_widget()->get_engine()->pause(); });
    connect(m_render_actions.resume, &QAction::triggered, this, [this] { get_gl_widget()->get_engine()->resume(); });
    connect(m_render_actions.restart, &QAction::triggered, this, [this] { get_gl_widget()->get_engine()->restart(); });
    m_view_menu->addAction(m_render_actions.pause);
    m_view_menu->addAction(m_render_actions.resume);
    m_view_menu->addAction(m_render_actions.restart);

    QAction* lights_separator = nullptr;
    for (auto action : m_toolbar->actions())
    {
        auto lights = action->property("lights");
        if (lights.isValid())
        {
            if (lights.toBool())
            {
                lights_separator = action;
            }
        }
    }

    QMenu* lights_menu = new QMenu(i18n("viewport.menu_bar", "Lights"));
    m_menubar->addMenu(lights_menu);
    QActionGroup* lights_action_group = new QActionGroup(this);
    lights_action_group->setExclusive(true);
    QAction* default_lighting_action = new QAction(QIcon(":/icons/use_default_lighting"), i18n("viewport.actions", "Use Default Lighting"), this);
    utils::action_set_object_name_from_text(default_lighting_action, "viewport");

    default_lighting_action->setCheckable(true);
    connect(default_lighting_action, &QAction::toggled, [this](bool checked) {
        m_glwidget->set_use_camera_light(true);
        update();
    });
    default_lighting_action->setChecked(true);
    lights_action_group->addAction(default_lighting_action);
    QAction* all_lights_action = new QAction(QIcon(":/icons/use_all_lights"), i18n("viewport.actions", "Use All Lights"), this);
    utils::action_set_object_name_from_text(all_lights_action, "viewport");
    all_lights_action->setCheckable(true);
    connect(all_lights_action, &QAction::toggled, [this](bool checked) {
        m_glwidget->set_use_camera_light(false);
        update();
    });
    lights_action_group->addAction(all_lights_action);
    QAction* enable_shadows_action = new QAction(QIcon(":/icons/enable_shadows"), i18n("viewport.actions", "Enable Shadows"));
    utils::action_set_object_name_from_text(enable_shadows_action, "viewport");
    enable_shadows_action->setCheckable(true);
    connect(enable_shadows_action, &QAction::toggled, [this](bool checked) {
        m_glwidget->set_enable_shadows(checked);
        update();
    });

    if (lights_separator)
    {
        m_toolbar->insertAction(lights_separator, default_lighting_action);
        setup_toolbar_action(default_lighting_action);
        m_toolbar->insertAction(lights_separator, all_lights_action);
        setup_toolbar_action(all_lights_action);
        m_toolbar->insertAction(lights_separator, enable_shadows_action);
        setup_toolbar_action(enable_shadows_action);
    }

    lights_menu->addAction(default_lighting_action);
    lights_menu->addAction(all_lights_action);
    lights_menu->addAction(enable_shadows_action);

    m_visibility_types_menu = new QMenu(i18n("viewport.menu_bar", "Show"));
    utils::menu_set_object_name_from_title(m_visibility_types_menu, "show");
    m_menubar->addMenu(m_visibility_types_menu);

    auto init_visibility_types = [this] {
        struct VisibilityState
        {
            QString type;
            bool visible = true;
        };
        std::map<QString, std::vector<VisibilityState>> current_state;
        bool enable_all = true;
        if (!m_visibility_types_menu->actions().empty())
        {
            QString current_group;
            const auto actions = m_visibility_types_menu->actions();
            for (int i = 1; i < actions.size(); i++)
            {
                const auto& action = actions[i];
                if (action->isSeparator())
                {
                    current_group = action->text();
                }
                else
                {
                    current_state[current_group].push_back({ action->text(), action->isChecked() });
                    enable_all &= action->isChecked();
                }
            }
        }

        m_visibility_types_menu->clear();
        auto enable_all_action = m_visibility_types_menu->addAction(i18n("viewport.menu_bar.show", "Show All"));
        enable_all_action->setCheckable(true);
        enable_all_action->setChecked(enable_all);
        connect(enable_all_action, &QAction::triggered, this, [this](bool checked) {
            for (const auto& action : m_visibility_types_menu->actions())
            {
                if (action->isCheckable())
                    action->setChecked(checked);
            }
        });
        std::map<PXR_NS::TfToken, std::vector<PrimVisibilityRegistry::PrimVisibilityType>> vis_groups;
        for (const auto& type : PrimVisibilityRegistry::get_prim_visibility_types())
            vis_groups[type.group].push_back(type);
        for (const auto& group : vis_groups)
        {
            const auto group_name = group.first.GetString() == "" ? i18n("viewport.menu_bar.show", "Common") : group.first.GetText();
            m_visibility_types_menu->addSection(group_name);
            for (const auto& type : group.second)
            {
                auto ui_name = type.ui_name.empty() ? type.type.GetText() : type.ui_name.c_str();
                auto action = new QAction(ui_name);
                action->setCheckable(true);
                bool checked = true;
                if (auto group_ptr = TfMapLookupPtr(current_state, group_name))
                {
                    auto iter = std::find_if(group_ptr->begin(), group_ptr->end(),
                                             [&ui_name](const VisibilityState& vis_state) { return ui_name == vis_state.type; });
                    if (iter == group_ptr->end())
                        checked = true;
                    else
                        checked = iter->visible;
                }
                action->setChecked(checked);
                action->setData(QVariant(QString::fromStdString(type.type.GetString())));
                m_visibility_types_menu->addAction(action);

                connect(action, &QAction::toggled, this, [this, type = type.type, group_name = group.first, enable_all_action](bool checked) {
                    get_gl_widget()->set_visibility_type(checked, type, group_name);
                });
                connect(action, &QAction::triggered, this, [this, enable_all_action](bool checked) {
                    bool enable_all = true;
                    auto actions = m_visibility_types_menu->actions();
                    for (int i = 1; i < actions.size() && enable_all; i++)
                    {
                        if (actions[i]->isCheckable())
                            enable_all &= actions[i]->isChecked();
                    }
                    enable_all_action->setChecked(enable_all);
                });
            }
        }
    };
    m_visibility_types_changed_cid = PrimVisibilityRegistry::register_visibility_types_changes(init_visibility_types);
    init_visibility_types();
    m_visibility_types_menu->addSeparator();
    m_visibility_types_menu->addAction(m_isolate_selection);

    if (m_feature_flags == FeatureFlags::VIEWPORT)
    {
        m_scene_context_menu = new QMenu(i18n("viewport.menu_bar", "Context"));
        utils::menu_set_object_name_from_title(m_scene_context_menu, "viewport");

        m_menubar->addMenu(m_scene_context_menu);

        m_scene_context_action_group = new QActionGroup(this);
        m_scene_context_action_group->setExclusive(true);

        auto context_usd_action = add_scene_context(TfToken("USD"));
        context_usd_action->setChecked(true);
    }

    m_view_menu->addSeparator();
    m_view_menu->addAction(m_enable_scene_materials_action);

    auto cull_backfaces = new QAction(i18n("viewport.menu_bar.view", "Cull Backfaces"));
    cull_backfaces->setCheckable(true);
    cull_backfaces->setChecked(false);
    connect(cull_backfaces, &QAction::toggled, [this](bool value) { m_glwidget->set_cull_backfaces(value); });
    m_view_menu->addAction(cull_backfaces);

    // add three hotkey only modes
    // '5' - smooth, camera light, no scene materials
    // '6' - smooth, camera light, enable scene materials
    // '7' - smooth, all lights, enable scene materials

    QAction* five_mode_action = new QAction(i18n("viewport.shortcut_name", "Five Mode Action"), this);
    utils::action_set_object_name_from_text(five_mode_action, "viewport");
    five_mode_action->setShortcut(tr("5"));
    five_mode_action->setCheckable(true);
    five_mode_action->setChecked(true);
    connect(five_mode_action, &QAction::triggered, [this, shaded_smooth_action, default_lighting_action](bool) {
        shaded_smooth_action->setChecked(true);
        m_enable_scene_materials_action->setChecked(false);
        default_lighting_action->setChecked(true);
    });
    this->addAction(five_mode_action);

    QAction* six_mode_action = new QAction(i18n("viewport.shortcut_name", "Six Mode Action"), this);
    utils::action_set_object_name_from_text(six_mode_action, "viewport");
    six_mode_action->setShortcut(tr("6"));
    six_mode_action->setCheckable(true);
    connect(six_mode_action, &QAction::triggered, [this, shaded_smooth_action, default_lighting_action](bool) {
        shaded_smooth_action->setChecked(true);
        m_enable_scene_materials_action->setChecked(true);
        default_lighting_action->setChecked(true);
    });
    this->addAction(six_mode_action);

    QAction* seven_mode_action = new QAction(i18n("viewport.shortcut_name", "Seven Mode Action"), this);
    utils::action_set_object_name_from_text(seven_mode_action, "viewport");
    seven_mode_action->setShortcut(tr("7"));
    seven_mode_action->setCheckable(true);
    connect(seven_mode_action, &QAction::triggered, [this, shaded_smooth_action, all_lights_action](bool) {
        shaded_smooth_action->setChecked(true);
        m_enable_scene_materials_action->setChecked(true);
        all_lights_action->setChecked(true);
    });
    this->addAction(seven_mode_action);

    // refine level hotkey actions
    //  1 - clear selection
    //  2 - set level 1 selection
    //  3 - set level 2 selection
    QAction* clear_refine_level_sel_action = new QAction(i18n("viewport.shortcut_name", "Clear Refine Level Selection"), this);
    utils::action_set_object_name_from_text(clear_refine_level_sel_action, "viewport");
    clear_refine_level_sel_action->setShortcut(tr("1"));
    connect(clear_refine_level_sel_action, &QAction::triggered, [this](bool) {
        auto selection = Application::instance().get_prim_selection();
        auto stage = Application::instance().get_session()->get_current_stage();
        if (selection.size() > 0 && stage)
        {
            for (auto path : selection)
            {
                UsdViewportRefineManager::instance().clear_refine_level(stage, path);
            }
        }
    });
    this->addAction(clear_refine_level_sel_action);

    QAction* set_refine_level1_sel_action = new QAction(i18n("viewport.shortcut_name", "Set Refine Level1 Selection"), this);
    utils::action_set_object_name_from_text(set_refine_level1_sel_action, "viewport");
    set_refine_level1_sel_action->setShortcut(tr("2"));
    connect(set_refine_level1_sel_action, &QAction::triggered, [this](bool) {
        auto selection = Application::instance().get_prim_selection();
        auto stage = Application::instance().get_session()->get_current_stage();
        if (selection.size() > 0 && stage)
        {
            for (auto path : selection)
            {
                UsdViewportRefineManager::instance().set_refine_level(stage, path, 1);
            }
        }
    });
    this->addAction(set_refine_level1_sel_action);

    QAction* set_refine_level2_sel_action = new QAction(i18n("viewport.shortcut_name", "Set Refine Level2 Selection"), this);
    utils::action_set_object_name_from_text(set_refine_level2_sel_action, "viewport");
    set_refine_level2_sel_action->setShortcut(tr("3"));
    connect(set_refine_level2_sel_action, &QAction::triggered, [this](bool) {
        auto selection = Application::instance().get_prim_selection();
        auto stage = Application::instance().get_session()->get_current_stage();
        if (selection.size() > 0 && stage)
        {
            for (auto path : selection)
            {
                UsdViewportRefineManager::instance().set_refine_level(stage, path, 2);
            }
        }
    });
    this->addAction(set_refine_level2_sel_action);

    // change gizmo global scale hotkeys:
    // '-' -- decrease global scale by half
    // '+' -- increase global_scale by 2
    QAction* decrease_global_scale = new QAction(i18n("viewport.shortcut_name", "Decrease Global Scale"), this);
    utils::action_set_object_name_from_text(decrease_global_scale, "viewport");
    decrease_global_scale->setShortcut(Qt::Key_Minus);
    connect(decrease_global_scale, &QAction::triggered, [this](bool) {
        const auto cur_scale = Application::instance().get_settings()->get("viewport.manipulators.global_scale", 1.0f);
        Application::instance().get_settings()->set("viewport.manipulators.global_scale", cur_scale / 2);
        get_gl_widget()->update();
    });
    this->addAction(decrease_global_scale);

    QAction* increase_global_scale = new QAction(i18n("viewport.shortcut_name", "Increase Global Scale"), this);
    increase_global_scale->setShortcut(Qt::Key_Equal);
    utils::action_set_object_name_from_text(increase_global_scale, "viewport");
    connect(increase_global_scale, &QAction::triggered, [this](bool) {
        const auto cur_scale = Application::instance().get_settings()->get("viewport.manipulators.global_scale", 1.0f);
        Application::instance().get_settings()->set("viewport.manipulators.global_scale", cur_scale * 2);
        get_gl_widget()->update();
    });
    this->addAction(increase_global_scale);

    clear_camera_menu_controller_actions();
    fill_camera_menu_controller_actions();
}

void ViewportWidget::on_current_stage_changed()
{
    clear_camera_menu_controller_actions();
    set_camera_menu_controller(new ViewportUsdCameraMenuController(m_glwidget->get_camera_controller(), m_viewport_overlay->widget(), this));
    scene_context_changed(get_gl_widget()->get_scene_context_type());
    fill_camera_menu_controller_actions();
    if (m_render_settings_dialog)
        m_render_settings_dialog->on_render_plugin_changed(m_glwidget->get_engine()->get_current_render_id());

    auto engine = m_glwidget->get_engine();
    auto render_plugins = engine->get_render_plugins();
    auto current_plugin_id = engine->get_current_render_id();
    for (auto plugin_id : render_plugins)
    {
        if (current_plugin_id == plugin_id)
        {
            QString render_name = engine->get_render_display_name(plugin_id).c_str();
            if (render_name == "GL")
                render_name = "Storm";
            auto action = m_renderer_menu_group->checkedAction();
            if (action && action->text() != render_name)
            {
                for (const auto action : m_renderer_menu_group->actions())
                {
                    if (action->text() == render_name)
                    {
                        action->setChecked(true);
                        break;
                    }
                }
            }
        }
    }
#if PXR_VERSION >= 2005
    update_displayed_aovs();
#endif
    m_isolate_selection->setChecked(false);
}

#if PXR_VERSION >= 2005
void ViewportWidget::update_displayed_aovs()
{
    auto current_aov = m_aov_combobox->currentText();
    m_aov_combobox->clear();
    for (const auto& aov : get_gl_widget()->get_engine()->get_renderer_aovs())
        m_aov_combobox->addItem(aov.GetText());
    m_aov_combobox->setCurrentText(get_gl_widget()->get_engine()->get_current_aov().GetText());
}

void ViewportWidget::on_render_settings_changed()
{
    update_displayed_aovs();
}
#endif

void ViewportWidget::toolbar_add_action(QAction* action)
{
    m_toolbar->addAction(action);
    setup_toolbar_action(action);
}

QAction* ViewportWidget::add_scene_context(const PXR_NS::TfToken& context)
{
    if (m_feature_flags == FeatureFlags::VIEWPORT)
    {
        QAction* new_context_action = new QAction(context.GetString().c_str(), this);
        utils::action_set_object_name_from_text(new_context_action, "viewport");
        new_context_action->setCheckable(true);
        connect(new_context_action, &QAction::triggered, [this, context](bool) {
            m_scene_context = ViewportSceneContextRegistry::get_instance().create_scene_context(context);
            get_gl_widget()->set_scene_context(m_scene_context);
            scene_context_changed(context);
            update();
        });
        m_scene_context_menu->addAction(new_context_action);
        m_scene_context_action_group->addAction(new_context_action);
        m_viewport_overlay->widget()->add_scene_context(new_context_action);
        return new_context_action;
    }
    return nullptr;
}

void ViewportWidget::set_camera_menu_controller(ViewportCameraMenuController* camera_controller)
{
    m_camera_menu_controller.reset(camera_controller);
}

void ViewportWidget::fill_camera_menu_controller_actions()
{
    if (auto menu = m_camera_menu_controller->get_camera_menu())
        m_view_menu->insertMenu(m_view_menu->actions().at(0), menu);
    if (auto look_through = m_camera_menu_controller->get_look_through_action())
        m_view_menu->insertAction(m_view_menu->actions().at(1), look_through);
}

void ViewportWidget::clear_camera_menu_controller_actions()
{
    if (m_camera_menu_controller)
    {
        m_view_menu->removeAction(m_camera_menu_controller->get_camera_menu()->menuAction());
        m_view_menu->removeAction(m_camera_menu_controller->get_look_through_action());
    }
}

const std::unordered_set<ViewportWidget*>& ViewportWidget::get_live_widgets()
{
    return m_live_widgets;
}

/* static */
void ViewportWidget::update_all_gl_widget()
{
    for (auto widget : get_live_widgets())
    {
        widget->get_gl_widget()->update();
    }
}

bool ViewportWidget::event(QEvent* event)
{
    if (event->type() == QEvent::WindowActivate)
        ApplicationUI::instance().set_active_view(this);

    return QWidget::event(event);
}

void ViewportWidget::update_render_actions()
{
    m_render_actions.pause->setEnabled(get_gl_widget()->get_engine()->is_pause_supported());
    m_render_actions.resume->setEnabled(get_gl_widget()->get_engine()->is_pause_supported());
}

OPENDCC_NAMESPACE_CLOSE
