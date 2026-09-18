// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <fstream> // std::ifstream

#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/sdf/path.h"

#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QApplication>
#include <QDialog>
#include <QWidgetAction>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QSplitter>
#include <QUndoStack>
#include <QColor>
#include <memory>

#include "opendcc/anim_engine/curve/curve.h"
#include "opendcc/anim_engine/ui/graph_editor/graph_editor.h"
#include "opendcc/anim_engine/core/session.h"
#include "utils.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/ui/node_icon_registry.h"
#include "opendcc/anim_engine/core/utils.h"

#include "opendcc/app/ui/application_ui.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

namespace
{
    void update_AE()
    {
    
    }

    bool register_AE_updates_callback()
    {
        AnimEngineSession::instance().register_event_callback(AnimEngineSession::EventType::CURVES_ADDED, update_AE);
        AnimEngineSession::instance().register_event_callback(AnimEngineSession::EventType::CURVES_REMOVED, update_AE);
        return true;
    }

    bool register_result = register_AE_updates_callback();
}

GraphEditor::GraphEditor(QWidget* parent)
    : QWidget(parent)
{
    update_content();
    QLocale::setDefault(QLocale(QLocale::Hawaiian, QLocale::UnitedStates));

    snap_window = new SnapWindow();
    menu_bar = new QMenuBar(this);
    curves_list_widget = new ChannelEditor(true, this);

    spline_widget = new SplineWidget(this);
    spline_widget->set_mode(SplineWidget::Mode::RegionTools);
    // Must be Expanding: SplineWidget has no sizeHint, and Qt6's QSplitter gives a Maximum-policy
    // widget with an invalid hint zero width. A zero-width QOpenGLWidget never paints, so it never
    // creates its GL context and the canvas comes up blank.
    spline_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    create_actions();
    create_toolbar();
    create_menubar();
    create_context_menu();

    connect(spline_widget, SIGNAL(selection_changed()), this, SLOT(splines_selection_changed()));
    connect(spline_widget, SIGNAL(context_menu_event_signal(QContextMenuEvent*)), this, SLOT(show_context_menu(QContextMenuEvent*)));

    connect(snap_window, SIGNAL(snap_selection_signal(bool, bool)), spline_widget, SLOT(snap_selection(bool, bool)));
    connect(snap_window, SIGNAL(snap_time_change_signal(double)), spline_widget, SLOT(set_auto_snap_time_interval(double)));
    connect(snap_window, SIGNAL(snap_value_change_signal(double)), spline_widget, SLOT(set_auto_snap_value_interval(double)));

    connect(curves_list_widget, SIGNAL(itemSelectionChanged()), this, SLOT(curves_selection_changed()));

    QSplitter* splitter = new QSplitter;
    splitter->addWidget(curves_list_widget);
    splitter->addWidget(spline_widget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // QHBoxLayout *h_layout = new QHBoxLayout;
    // h_layout->addWidget(curves_list_widget);
    // h_layout->addWidget(spline_widget);

    QVBoxLayout* v_layout = new QVBoxLayout;
    v_layout->setSpacing(0);
    v_layout->setContentsMargins(0, 0, 0, 0);
    // v_layout->addWidget(menu_bar);
    v_layout->setMenuBar(menu_bar);
    v_layout->addWidget(tool_bar);
    // v_layout->addLayout(h_layout);
    v_layout->addWidget(splitter);

    setLayout(v_layout);
    spline_widget->setFocus();
    attach_to_application();
}

void GraphEditor::attach_to_application()
{
    m_application_events_handles[Application::EventType::SELECTION_CHANGED] =
        Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] { update_content(); });
    m_application_events_handles[Application::EventType::CURRENT_STAGE_CHANGED] =
        Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this]() { current_stage_changed(); });
    m_application_events_handles[Application::EventType::CURRENT_TIME_CHANGED] =
        Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this]() { current_time_changed(); });
    m_application_events_handles[Application::EventType::BEFORE_CURRENT_STAGE_CLOSED] =
        Application::instance().register_event_callback(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED, [this]() { current_stage_closed(); });

    current_stage_changed();
}

bool GraphEditor::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride)
    {
        auto keyevent_event = static_cast<QKeyEvent*>(event);
        if (keyevent_event->key() == Qt::Key_Delete)
        {
            event->accept();
        }
    }
    return QWidget::event(event);
}

void GraphEditor::current_stage_changed()
{
    if (m_current_engine.get() == AnimEngineSession::instance().current_engine().get())
        return;
    clear_anim_engine_events();
    m_current_engine = AnimEngineSession::instance().current_engine();
    if (m_current_engine)
    {
        save_on_current_layer->setChecked(m_current_engine->is_save_on_current_layer());

        m_anim_engine_option_changed = m_current_engine->AnimEngineOptionChanged::subscribe(
            AnimEngineOption::isSaveOnCurrentLayer, [this]() { save_on_current_layer->setChecked(m_current_engine->is_save_on_current_layer()); });
    }
    create_anim_engine_events();
    spline_widget->set_current_engine(m_current_engine);
}

void GraphEditor::current_time_changed() {}

void GraphEditor::current_stage_closed()
{
    current_stage_changed();
}

void GraphEditor::create_anim_engine_events()
{
    clear_anim_engine_events();
    if (m_current_engine)
    {
        m_events[AnimEngine::EventType::CURVES_ADDED] = m_current_engine->register_event_callback(
            AnimEngine::EventType::CURVES_ADDED, [&](const AnimEngine::CurveIdsList& ids) { update_content(); });

        m_events[AnimEngine::EventType::CURVES_REMOVED] = m_current_engine->register_event_callback(
            AnimEngine::EventType::CURVES_REMOVED, [&](const AnimEngine::CurveIdsList& ids) { update_content(); });
    }
}

void GraphEditor::clear_anim_engine_events()
{
    if (m_current_engine)
    {
        for (auto& it : m_events)
            m_current_engine->unregister_event_callback(it.first, it.second);
        m_events.clear();
    }
}

GraphEditor::~GraphEditor()
{
    for (auto it : m_application_events_handles)
        Application::instance().unregister_event_callback(it.first, it.second);

    clear_anim_engine_events();
}

void GraphEditor::create_actions()
{
    set_region_tools = new QAction(i18n("graph_editor", "Edit Mode"), this);
    set_region_tools->setIcon(QIcon(":icons/regionSelectKeySmall.png"));
    set_region_tools->setCheckable(true);
    set_region_tools->setChecked(true);

    set_insert_keys_tools = new QAction(i18n("graph_editor", "Insert Mode"), this);
    set_insert_keys_tools->setIcon(QIcon(":icons/insertKeySmall.png"));
    set_insert_keys_tools->setCheckable(true);

    mode_tools = new QActionGroup(this);
    mode_tools->addAction(set_region_tools);
    mode_tools->addAction(set_insert_keys_tools);
    connect(mode_tools, SIGNAL(triggered(QAction*)), this, SLOT(set_region_tools_mode(QAction*)));

    fit_all_to_widget = new QAction(i18n("graph_editor", "Frame All"), this);
    fit_all_to_widget->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    fit_all_to_widget->setShortcut(Qt::Key_A);
    fit_all_to_widget->setIcon(QIcon(":icons/traxFrameAll.png"));
    fit_tools = new QActionGroup(this);
    fit_tools->addAction(fit_all_to_widget);
    connect(fit_all_to_widget, SIGNAL(triggered()), this, SLOT(fit_all_to_widget_slot()));

    fit_selection_to_widget = new QAction(i18n("graph_editor", "Frame Selection"), this);
    fit_selection_to_widget->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    fit_selection_to_widget->setShortcut(Qt::Key_F);
    connect(fit_selection_to_widget, SIGNAL(triggered()), this, SLOT(fit_selection_to_widget_slot()));

    fixed_type = new QAction(i18n("graph_editor", "Fixed"), this);
    fixed_type->setCheckable(true);
    fixed_type->setIcon(QIcon(":icons/menuIconTangents.png"));
    linear_type = new QAction(i18n("graph_editor", "Linear"), this);
    linear_type->setCheckable(true);
    linear_type->setIcon(QIcon(":icons/linearTangent.png"));
    flat_type = new QAction(i18n("graph_editor", "Flat"), this);
    flat_type->setCheckable(true);
    flat_type->setIcon(QIcon(":icons/flatTangent.png"));
    spline_type = new QAction(i18n("graph_editor", "Spline"), this);
    spline_type->setCheckable(true);
    spline_type->setIcon(QIcon(":icons/splineTangent.png"));
    stepped_type = new QAction(i18n("graph_editor", "Stepped"), this);
    stepped_type->setCheckable(true);
    stepped_type->setIcon(QIcon(":icons/stepTangent.png"));
    stepped_next_type = new QAction(i18n("graph_editor", "SteppedNext"), this);
    stepped_next_type->setCheckable(true);
    stepped_next_type->setIcon(QIcon(":icons/stepNextTangent.png"));
    plateau_type = new QAction(i18n("graph_editor", "Plateau"), this);
    plateau_type->setCheckable(true);
    plateau_type->setIcon(QIcon(":icons/plateauTangent.png"));
    clamped_type = new QAction(i18n("graph_editor", "Clamped"), this);
    clamped_type->setCheckable(true);
    clamped_type->setIcon(QIcon(":icons/clampedTangent.png"));
    auto_type = new QAction(i18n("graph_editor", "Auto"), this);
    auto_type->setCheckable(true);
    auto_type->setChecked(true);
    auto_type->setIcon(QIcon(":icons/autoTangent.png"));
    mix_type = new QAction(i18n("graph_editor", "mixType"), this);
    mix_type->setCheckable(true);

    fixed_type_out = new QAction(i18n("graph_editor", "Fixed"), this);
    linear_type_out = new QAction(i18n("graph_editor", "Linear"), this);
    flat_type_out = new QAction(i18n("graph_editor", "Flat"), this);
    spline_type_out = new QAction(i18n("graph_editor", "Spline"), this);
    stepped_type_out = new QAction(i18n("graph_editor", "Stepped"), this);
    stepped_next_type_out = new QAction(i18n("graph_editor", "SteppedNext"), this);
    plateau_type_out = new QAction(i18n("graph_editor", "Plateau"), this);
    clamped_type_out = new QAction(i18n("graph_editor", "Clamped"), this);
    auto_type_out = new QAction(i18n("graph_editor", "Auto"), this);

    fixed_type_in = new QAction(i18n("graph_editor", "Fixed"), this);
    linear_type_in = new QAction(i18n("graph_editor", "Linear"), this);
    flat_type_in = new QAction(i18n("graph_editor", "Flat"), this);
    spline_type_in = new QAction(i18n("graph_editor", "Spline"), this);
    stepped_type_in = new QAction(i18n("graph_editor", "Stepped"), this);
    stepped_next_type_in = new QAction(i18n("graph_editor", "SteppedNext"), this);
    plateau_type_in = new QAction(i18n("graph_editor", "Plateau"), this);
    clamped_type_in = new QAction(i18n("graph_editor", "Clamped"), this);
    auto_type_in = new QAction(i18n("graph_editor", "Auto"), this);

    spline_type_action_ptr_to_type_ind[mix_type] = adsk::TangentType::Global;
    spline_type_action_ptr_to_type_ind[fixed_type] = adsk::TangentType::Fixed;
    spline_type_action_ptr_to_type_ind[linear_type] = adsk::TangentType::Linear;
    spline_type_action_ptr_to_type_ind[flat_type] = adsk::TangentType::Flat;
    spline_type_action_ptr_to_type_ind[spline_type] = adsk::TangentType::Smooth;
    spline_type_action_ptr_to_type_ind[stepped_type] = adsk::TangentType::Step;
    spline_type_action_ptr_to_type_ind[stepped_next_type] = adsk::TangentType::StepNext;
    spline_type_action_ptr_to_type_ind[plateau_type] = adsk::TangentType::Plateau;
    spline_type_action_ptr_to_type_ind[clamped_type] = adsk::TangentType::Clamped;
    spline_type_action_ptr_to_type_ind[auto_type] = adsk::TangentType::Auto;

    for (auto it = spline_type_action_ptr_to_type_ind.begin(); it != spline_type_action_ptr_to_type_ind.end(); ++it)
    {
        spline_type_ind_to_type_action_ptr[it->second] = it->first;
    }

    spline_type_action_ptr_to_type_ind[fixed_type_in] = adsk::TangentType::Fixed;
    spline_type_action_ptr_to_type_ind[linear_type_in] = adsk::TangentType::Linear;
    spline_type_action_ptr_to_type_ind[flat_type_in] = adsk::TangentType::Flat;
    spline_type_action_ptr_to_type_ind[spline_type_in] = adsk::TangentType::Smooth;
    spline_type_action_ptr_to_type_ind[stepped_type_in] = adsk::TangentType::Step;
    spline_type_action_ptr_to_type_ind[stepped_next_type_in] = adsk::TangentType::StepNext;
    spline_type_action_ptr_to_type_ind[plateau_type_in] = adsk::TangentType::Plateau;
    spline_type_action_ptr_to_type_ind[clamped_type_in] = adsk::TangentType::Clamped;
    spline_type_action_ptr_to_type_ind[auto_type_in] = adsk::TangentType::Auto;

    spline_type_action_ptr_to_type_ind[fixed_type_out] = adsk::TangentType::Fixed;
    spline_type_action_ptr_to_type_ind[linear_type_out] = adsk::TangentType::Linear;
    spline_type_action_ptr_to_type_ind[flat_type_out] = adsk::TangentType::Flat;
    spline_type_action_ptr_to_type_ind[spline_type_out] = adsk::TangentType::Smooth;
    spline_type_action_ptr_to_type_ind[stepped_type_out] = adsk::TangentType::Step;
    spline_type_action_ptr_to_type_ind[stepped_next_type_out] = adsk::TangentType::StepNext;
    spline_type_action_ptr_to_type_ind[plateau_type_out] = adsk::TangentType::Plateau;
    spline_type_action_ptr_to_type_ind[clamped_type_out] = adsk::TangentType::Clamped;
    spline_type_action_ptr_to_type_ind[auto_type_out] = adsk::TangentType::Auto;

    spline_type_action_group = new QActionGroup(this);
    spline_type_action_group->addAction(auto_type);
    spline_type_action_group->addAction(spline_type);
    spline_type_action_group->addAction(clamped_type);
    spline_type_action_group->addAction(linear_type);
    spline_type_action_group->addAction(flat_type);
    spline_type_action_group->addAction(stepped_type);
    spline_type_action_group->addAction(stepped_next_type);
    spline_type_action_group->addAction(plateau_type);
    spline_type_action_group->addAction(fixed_type);
    spline_type_action_group->addAction(mix_type);
    connect(spline_type_action_group, SIGNAL(triggered(QAction*)), this, SLOT(update_tangents_type(QAction*)));

    QActionGroup* in_spline_type_action_group = new QActionGroup(this);
    in_spline_type_action_group->addAction(auto_type_in);
    in_spline_type_action_group->addAction(spline_type_in);
    in_spline_type_action_group->addAction(clamped_type_in);
    in_spline_type_action_group->addAction(linear_type_in);
    in_spline_type_action_group->addAction(flat_type_in);
    in_spline_type_action_group->addAction(stepped_type_in);
    in_spline_type_action_group->addAction(stepped_next_type_in);
    in_spline_type_action_group->addAction(plateau_type_in);
    in_spline_type_action_group->addAction(fixed_type_in);
    connect(in_spline_type_action_group, SIGNAL(triggered(QAction*)), this, SLOT(update_in_tangents_type(QAction*)));

    QActionGroup* out_spline_type_action_group = new QActionGroup(this);
    out_spline_type_action_group->addAction(auto_type_out);
    out_spline_type_action_group->addAction(spline_type_out);
    out_spline_type_action_group->addAction(clamped_type_out);
    out_spline_type_action_group->addAction(linear_type_out);
    out_spline_type_action_group->addAction(flat_type_out);
    out_spline_type_action_group->addAction(stepped_type_out);
    out_spline_type_action_group->addAction(stepped_next_type_out);
    out_spline_type_action_group->addAction(plateau_type_out);
    out_spline_type_action_group->addAction(fixed_type_out);
    connect(out_spline_type_action_group, SIGNAL(triggered(QAction*)), this, SLOT(update_out_tangents_type(QAction*)));

    break_tangents = new QAction(i18n("graph_editor", "Break tangents"), this);
    break_tangents->setCheckable(true);
    break_tangents->setIcon(QIcon(":icons/breakTangent.png"));
    unify_tangents = new QAction(i18n("graph_editor", "Unify tangents"), this);
    unify_tangents->setCheckable(true);
    unify_tangents->setChecked(true);
    unify_tangents->setIcon(QIcon(":icons/unifyTangent.png"));
    break_unify_tangents = new QActionGroup(this);
    break_unify_tangents->addAction(break_tangents);
    break_unify_tangents->addAction(unify_tangents);
    connect(break_unify_tangents, SIGNAL(triggered(QAction*)), this, SLOT(update_break_unify_mod(QAction*)));

    time_snap = new QAction(i18n("graph_editor", "Time snap"), this);
    time_snap->setCheckable(true);
    time_snap->setIcon(QIcon(":icons/snapTime.png"));
    connect(time_snap, SIGNAL(triggered(bool)), spline_widget, SLOT(set_is_auto_snap_time(bool)));
    value_snap = new QAction(i18n("graph_editor", "Value snap"), this);
    value_snap->setCheckable(true);
    value_snap->setIcon(QIcon(":icons/snapValue.png"));
    connect(value_snap, SIGNAL(triggered(bool)), spline_widget, SLOT(set_is_auto_snap_value(bool)));

    pre_infinity_cycle = new QAction(i18n("graph_editor", "Cycle"), this);
    pre_infinity_cycle_with_offset = new QAction(i18n("graph_editor", "Cycle with offset"), this);
    pre_infinity_oscillate = new QAction(i18n("graph_editor", "Oscillate"), this);
    pre_infinity_linear = new QAction(i18n("graph_editor", "Linear"), this);
    pre_infinity_constant = new QAction(i18n("graph_editor", "Constant"), this);
    connect(pre_infinity_cycle, SIGNAL(triggered()), this, SLOT(set_pre_infinity_cycle()));
    connect(pre_infinity_cycle_with_offset, SIGNAL(triggered()), this, SLOT(set_pre_infinity_cycle_with_offset()));
    connect(pre_infinity_oscillate, SIGNAL(triggered()), this, SLOT(set_pre_infinity_oscillate()));
    connect(pre_infinity_linear, SIGNAL(triggered()), this, SLOT(set_pre_infinity_linear()));
    connect(pre_infinity_constant, SIGNAL(triggered()), this, SLOT(set_pre_infinity_constant()));

    post_infinity_cycle = new QAction(i18n("graph_editor", "Cycle"), this);
    post_infinity_cycle_with_offset = new QAction(i18n("graph_editor", "Cycle with offset"), this);
    post_infinity_oscillate = new QAction(i18n("graph_editor", "Oscillate"), this);
    post_infinity_linear = new QAction(i18n("graph_editor", "Linear"), this);
    post_infinity_constant = new QAction(i18n("graph_editor", "Constant"), this);
    connect(post_infinity_cycle, SIGNAL(triggered()), this, SLOT(set_post_infinity_cycle()));
    connect(post_infinity_cycle_with_offset, SIGNAL(triggered()), this, SLOT(set_post_infinity_cycle_with_offset()));
    connect(post_infinity_oscillate, SIGNAL(triggered()), this, SLOT(set_post_infinity_oscillate()));
    connect(post_infinity_linear, SIGNAL(triggered()), this, SLOT(set_post_infinity_linear()));
    connect(post_infinity_constant, SIGNAL(triggered()), this, SLOT(set_post_infinity_constant()));

    show_pre_infinity_cycle = new QAction(QIcon(":icons/preInfinityCycle.png"), i18n("graph_editor", "Show pre-infinity cycle with offset"), this);
    connect(show_pre_infinity_cycle, SIGNAL(triggered()), this, SLOT(set_and_show_pre_infinity_cycle()));

    show_pre_infinity_cycle_with_offset = new QAction(QIcon(":icons/preInfinityCycleOffset.png"), ("Show pre-infinity cycle with offset"), this);
    connect(show_pre_infinity_cycle_with_offset, SIGNAL(triggered()), this, SLOT(set_and_show_pre_infinity_cycle_with_offset()));

    show_post_infinity_cycle = new QAction(QIcon(":icons/postInfinityCycle.png"), i18n("graph_editor", "Show post-infinity cycle"), this);
    connect(show_post_infinity_cycle, SIGNAL(triggered()), this, SLOT(set_and_show_post_infinity_cycle()));

    show_post_infinity_cycle_with_offset =
        new QAction(QIcon(":icons/postInfinityCycleOffset.png"), i18n("graph_editor", "Show post-infinity cycle with offset"), this);
    connect(show_post_infinity_cycle_with_offset, SIGNAL(triggered()), this, SLOT(set_and_show_post_infinity_cycle_with_offset()));

    delete_selection = new QAction(i18n("graph_editor", "Delete"), this);
    delete_selection->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    delete_selection->setShortcut(Qt::Key_Delete);
    connect(delete_selection, SIGNAL(triggered()), spline_widget, SLOT(delete_selected_keyframes()));

    show_infinity = new QAction(i18n("graph_editor", "Infinity"), this);
    show_infinity->setCheckable(true);
    connect(show_infinity, SIGNAL(triggered(bool)), spline_widget, SLOT(set_is_draw_infinity(bool)));

    snap_selection = new QAction(i18n("graph_editor", "Snap"), this);
    // connect(snap_selection, SIGNAL(triggered()), this, SLOT(snap_selection_slot()));
    connect(snap_selection, SIGNAL(triggered()), snap_window, SLOT(show()));

    save_on_current_layer = new QAction(i18n("graph_editor", "Save On Current Layer"), this);
    save_on_current_layer->setCheckable(true);
    connect(save_on_current_layer, &QAction::triggered, [&](bool value) {
        if (m_current_engine)
            m_current_engine->set_save_on_current_layer(value);
    });

    euler_filter = new QAction(i18n("graph_editor", "Euler Filter"), this);
    // set_key->setShortcut(QKeySequence("S"));
    euler_filter->setStatusTip(i18n("graph_editor", "Apply Euler Filter"));
    connect(euler_filter, &QAction::triggered, [&]() {
        if (m_current_engine)
            m_current_engine->euler_filter(selected_curves());
    });

    set_key = new QAction(i18n("graph_editor", "SetKey"), this);
    // set_key->setShortcut(QKeySequence("S"));
    set_key->setStatusTip(i18n("graph_editor", "Set Key on Translate, Rotate, Scale"));
    connect(set_key, &QAction::triggered, [&]() {
        if (m_current_engine)
            m_current_engine->create_animation_on_selected_prims(AnimEngine::AttributesScope::ALL);
    });

    set_key_on_translate = new QAction(i18n("graph_editor", "SetKeyOnTranslate"), this);
    // set_key_on_translate->setShortcut(QKeySequence("Shift+W"));
    set_key_on_translate->setStatusTip(i18n("graph_editor", "Set Key on Translate"));
    connect(set_key_on_translate, &QAction::triggered, [&]() {
        if (m_current_engine)
            m_current_engine->create_animation_on_selected_prims(AnimEngine::AttributesScope::TRANSLATE);
    });

    set_key_on_rotate = new QAction(i18n("graph_editor", "SetKeyOnRotate"), this);
    // set_key_on_rotate->setShortcut(QKeySequence("Shift+E"));
    set_key_on_rotate->setStatusTip(i18n("graph_editor", "Set Key on Rotate"));
    connect(set_key_on_rotate, &QAction::triggered, [&]() {
        if (m_current_engine)
            m_current_engine->create_animation_on_selected_prims(AnimEngine::AttributesScope::ROTATE);
    });

    set_key_on_scale = new QAction(i18n("graph_editor", "SetKeyOnScale"), this);
    // set_key_on_scale->setShortcut(QKeySequence("Shift+R"));
    set_key_on_scale->setStatusTip(i18n("graph_editor", "Set Key on Scale"));
    connect(set_key_on_scale, &QAction::triggered, [&]() {
        if (m_current_engine)
            m_current_engine->create_animation_on_selected_prims(AnimEngine::AttributesScope::SCALE);
    });

    /*  load_curves_from_file_action = new QAction("Load", this);
      load_curves_from_file_action->setShortcut(i18n("graph_editor", "Ctrl+O"));
      connect(load_curves_from_file_action, SIGNAL(triggered()), this, SLOT(load_curves_from_file()));*/
}

QMenu* GraphEditor::create_tangents_menus(bool add_break_tangents_buttons)
{
    QMenu* tangents_menu = new QMenu(i18n("graph_editor", "&Tangents"), this);
    tangents_menu->addAction(auto_type);
    tangents_menu->addAction(spline_type);
    tangents_menu->addAction(clamped_type);
    tangents_menu->addAction(linear_type);
    tangents_menu->addAction(flat_type);
    tangents_menu->addAction(stepped_type);
    tangents_menu->addAction(stepped_next_type);
    tangents_menu->addAction(plateau_type);
    tangents_menu->addAction(fixed_type);
    tangents_menu->addSeparator();

    QMenu* in_tangents_menu = new QMenu(i18n("graph_editor", "&In Tangents"), this);
    in_tangents_menu->addAction(auto_type_in);
    in_tangents_menu->addAction(spline_type_in);
    in_tangents_menu->addAction(clamped_type_in);
    in_tangents_menu->addAction(linear_type_in);
    in_tangents_menu->addAction(flat_type_in);
    in_tangents_menu->addAction(stepped_type_in);
    in_tangents_menu->addAction(stepped_next_type_in);
    in_tangents_menu->addAction(plateau_type_in);
    in_tangents_menu->addAction(fixed_type_in);
    tangents_menu->addMenu(in_tangents_menu);

    QMenu* out_tangents_menu = new QMenu(i18n("graph_editor", "&Out Tangents"), this);
    out_tangents_menu->addAction(auto_type_out);
    out_tangents_menu->addAction(spline_type_out);
    out_tangents_menu->addAction(clamped_type_out);
    out_tangents_menu->addAction(linear_type_out);
    out_tangents_menu->addAction(flat_type_out);
    out_tangents_menu->addAction(stepped_type_out);
    out_tangents_menu->addAction(stepped_next_type_out);
    out_tangents_menu->addAction(plateau_type_out);
    out_tangents_menu->addAction(fixed_type_out);
    tangents_menu->addMenu(out_tangents_menu);

    if (add_break_tangents_buttons)
    {
        tangents_menu->addSeparator();
        tangents_menu->addAction(break_tangents);
        tangents_menu->addAction(unify_tangents);
    }
    return tangents_menu;
}

void GraphEditor::create_toolbar()
{
    tool_bar = new QToolBar(this);
    tool_bar->setStyleSheet("QToolBar::separator { background-color: rgb(59, 59, 59); width: 1px; margin-left: 3px; margin-right: 3px }");
    tool_bar->addAction(set_region_tools);
    tool_bar->addAction(set_insert_keys_tools);
    tool_bar->addSeparator();

    tool_bar->addWidget(new QLabel(i18n("graph_editor", "  Stats ")));
    line_edit_time = new QLineEdit(this);
    line_edit_time->setMaximumWidth(70);
    tool_bar->addWidget(line_edit_time);
    connect(line_edit_time, SIGNAL(editingFinished()), this, SLOT(time_line_edit_editing_finished()));

    line_edit_value = new QLineEdit(this);
    line_edit_value->setMaximumWidth(70);
    tool_bar->addWidget(line_edit_value);
    connect(line_edit_value, SIGNAL(editingFinished()), this, SLOT(value_line_edit_editing_finished()));
    tool_bar->addSeparator();

    tool_bar->addAction(fit_all_to_widget);
    tool_bar->addSeparator();
    tool_bar->addAction(auto_type);
    tool_bar->addAction(spline_type);
    tool_bar->addAction(clamped_type);
    tool_bar->addAction(linear_type);
    tool_bar->addAction(flat_type);
    tool_bar->addAction(stepped_type);
    tool_bar->addAction(stepped_next_type);
    tool_bar->addAction(plateau_type);
    tool_bar->addAction(fixed_type);
    tool_bar->addSeparator();

    tool_bar->addAction(break_tangents);
    tool_bar->addAction(unify_tangents);
    tool_bar->addSeparator();
    tool_bar->addAction(time_snap);
    tool_bar->addAction(value_snap);
    tool_bar->addSeparator();

    tool_bar->addAction(show_pre_infinity_cycle);
    tool_bar->addAction(show_pre_infinity_cycle_with_offset);
    tool_bar->addAction(show_post_infinity_cycle);
    tool_bar->addAction(show_post_infinity_cycle_with_offset);
    tool_bar->addSeparator();
}

void GraphEditor::create_menubar()
{
    // QMenu* file_menu = new QMenu(i18n("graph_editor", "&File"), this);

    QMenu* keys_menu = new QMenu(i18n("graph_editor", "&Keys"), this);
    keys_menu->addAction(set_key);
    keys_menu->addAction(set_key_on_translate);
    keys_menu->addAction(set_key_on_rotate);
    keys_menu->addAction(set_key_on_scale);

    QMenu* edit_menu = new QMenu(i18n("graph_editor", "&Edit"), this);
    edit_menu->addAction(save_on_current_layer);
    edit_menu->addAction(euler_filter);
    edit_menu->addAction(snap_selection);
    edit_menu->addAction(delete_selection);

    QMenu* view_menu = new QMenu(i18n("graph_editor", "&View"), this);
    view_menu->addAction(fit_all_to_widget);
    view_menu->addAction(fit_selection_to_widget);
    view_menu->addAction(show_infinity);

    QMenu* curves_pre_inf = new QMenu(i18n("graph_editor", "&Pre Infinity"), this);
    curves_pre_inf->addAction(pre_infinity_cycle);
    curves_pre_inf->addAction(pre_infinity_cycle_with_offset);
    curves_pre_inf->addAction(pre_infinity_oscillate);
    curves_pre_inf->addAction(pre_infinity_linear);
    curves_pre_inf->addAction(pre_infinity_constant);

    QMenu* curves_post_inf = new QMenu(i18n("graph_editor", "&Post Infinity"), this);
    curves_post_inf->addAction(post_infinity_cycle);
    curves_post_inf->addAction(post_infinity_cycle_with_offset);
    curves_post_inf->addAction(post_infinity_oscillate);
    curves_post_inf->addAction(post_infinity_linear);
    curves_post_inf->addAction(post_infinity_constant);

    QMenu* curves_menu = new QMenu(i18n("graph_editor", "&Curves"), this);
    curves_menu->addMenu(curves_pre_inf);
    curves_menu->addMenu(curves_post_inf);

    menu_bar->addMenu(keys_menu);
    menu_bar->addMenu(edit_menu);
    menu_bar->addMenu(view_menu);
    menu_bar->addMenu(curves_menu);
    menu_bar->addMenu(create_tangents_menus(true));
}

void GraphEditor::create_context_menu()
{
    context_menu = new QMenu(this);
    context_menu->addMenu(create_tangents_menus(false));
    context_menu->addSeparator();
    context_menu->addAction(break_tangents);
    context_menu->addAction(unify_tangents);
}

void GraphEditor::update_tangents_type(QAction* emitor)
{
    spline_type_action_group->setExclusive(true);
    spline_widget->update_tangents_type(spline_type_action_ptr_to_type_ind.at(emitor), true, true);
}

void GraphEditor::update_in_tangents_type(QAction* emitor)
{
    spline_widget->update_tangents_type(spline_type_action_ptr_to_type_ind.at(emitor), true, false);
}

void GraphEditor::update_out_tangents_type(QAction* emitor)
{
    spline_widget->update_tangents_type(spline_type_action_ptr_to_type_ind.at(emitor), false, true);
}

void GraphEditor::splines_selection_changed()
{
    double time;
    double value;
    bool is_time_define;
    bool is_value_define;
    std::set<adsk::TangentType> tangents_types;

    spline_widget->get_selection_info(time, value, is_time_define, is_value_define, tangents_types);

    spline_type_action_group->blockSignals(true);

    spline_type_action_group->setExclusive(tangents_types.size() <= 1);

    for (auto it : spline_type_ind_to_type_action_ptr)
        it.second->setChecked(tangents_types.find(it.first) != tangents_types.end());

    spline_type_action_group->blockSignals(false);

    if (is_time_define)
        line_edit_time->setText(QString::number(time));
    else
        line_edit_time->setText("");

    if (is_value_define)
        line_edit_value->setText(QString::number(value));
    else
        line_edit_value->setText("");
}

void GraphEditor::time_line_edit_editing_finished()
{
    bool is_time_valid = false;
    const float time = line_edit_time->text().toFloat(&is_time_valid);
    if (is_time_valid)
    {
        spline_widget->set_time_to_selection(time);
    }
}

void GraphEditor::value_line_edit_editing_finished()
{
    bool is_value_valid = false;
    const float value = line_edit_value->text().toFloat(&is_value_valid);
    if (is_value_valid)
    {
        spline_widget->set_value_to_selection(value);
    }
}

void GraphEditor::set_region_tools_mode(QAction* emitor)
{
    if (emitor == set_region_tools)
    {
        spline_widget->set_mode(SplineWidget::Mode::RegionTools);
    }
    else if (emitor == set_insert_keys_tools)
    {
        spline_widget->set_mode(SplineWidget::Mode::InsertKeys);
    }
}

void GraphEditor::update_break_unify_mod(QAction* emitor)
{
    if (emitor == break_tangents)
    {
        spline_widget->set_is_tangents_break(true);
    }
    else
    {
        spline_widget->set_is_tangents_break(false);
    }
}

void GraphEditor::set_pre_infinity_cycle()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::Cycle);
}

void GraphEditor::set_pre_infinity_cycle_with_offset()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::CycleRelative);
}

void GraphEditor::set_pre_infinity_oscillate()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::Oscillate);
}

void GraphEditor::set_pre_infinity_linear()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::Linear);
}

void GraphEditor::set_pre_infinity_constant()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::Constant);
}

void GraphEditor::set_post_infinity_cycle()
{
    spline_widget->set_post_infinity(adsk::InfinityType::Cycle);
}

void GraphEditor::set_post_infinity_cycle_with_offset()
{
    spline_widget->set_post_infinity(adsk::InfinityType::CycleRelative);
}

void GraphEditor::set_post_infinity_oscillate()
{
    spline_widget->set_post_infinity(adsk::InfinityType::Oscillate);
}

void GraphEditor::set_post_infinity_linear()
{
    spline_widget->set_post_infinity(adsk::InfinityType::Linear);
}

void GraphEditor::set_post_infinity_constant()
{
    spline_widget->set_post_infinity(adsk::InfinityType::Constant);
}

void GraphEditor::set_and_show_pre_infinity_cycle()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::Cycle, SplineWidget::ApplyGroup::All);
    spline_widget->set_is_draw_infinity(true);
    show_infinity->setChecked(true);
}

void GraphEditor::set_and_show_pre_infinity_cycle_with_offset()
{
    spline_widget->set_pre_infinity(adsk::InfinityType::CycleRelative, SplineWidget::ApplyGroup::All);
    spline_widget->set_is_draw_infinity(true);
    show_infinity->setChecked(true);
}

void GraphEditor::set_and_show_post_infinity_cycle()
{
    spline_widget->set_post_infinity(adsk::InfinityType::Cycle, SplineWidget::ApplyGroup::All);
    spline_widget->set_is_draw_infinity(true);
    show_infinity->setChecked(true);
}

void GraphEditor::set_and_show_post_infinity_cycle_with_offset()
{
    spline_widget->set_post_infinity(adsk::InfinityType::CycleRelative, SplineWidget::ApplyGroup::All);
    spline_widget->set_is_draw_infinity(true);
    show_infinity->setChecked(true);
}

void GraphEditor::test()
{
    snap_window->show();
}

void GraphEditor::snap_selection_slot()
{
    spline_widget->snap_selection(snap_window->is_snap_time(), snap_window->is_snap_value());
}

void GraphEditor::show_context_menu(QContextMenuEvent* event)
{
    context_menu->exec(event->globalPos());
}

void GraphEditor::fit_all_to_widget_slot()
{
    spline_widget->fit_to_widget(true);
}

void GraphEditor::fit_selection_to_widget_slot()
{
    spline_widget->fit_to_widget(false);
}

void GraphEditor::set_current_time(double time)
{
    spline_widget->set_current_time(time);
}

void GraphEditor::clear()
{
    curves_list_widget->blockSignals(true);
    curves_list_widget->clear();
    curves_list_widget->blockSignals(false);
    spline_widget->clear();
}

std::set<OPENDCC_NAMESPACE::AnimEngine::CurveId> GraphEditor::selected_curves()
{
    return curves_list_widget->selected_curves_ids();
}

void GraphEditor::curves_selection_changed()
{
    spline_widget->set_displayed_curves(selected_curves());
}

void GraphEditor::update_content()
{
    if (!m_current_engine)
        return;
    clear();
    curves_list_widget->update_content();
    spline_widget->set_displayed_curves(curves_list_widget->selected_curves_ids());
}

OPENDCC_NAMESPACE_CLOSE
