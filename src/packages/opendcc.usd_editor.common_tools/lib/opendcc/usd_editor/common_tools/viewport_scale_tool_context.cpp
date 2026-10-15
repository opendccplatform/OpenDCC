// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_scale_tool_context.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include <QMouseEvent>
#include <QAction>
#include <pxr/pxr.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/transform.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <iostream>
#include "opendcc/app/viewport/viewport_widget.h"
#include "pxr/base/gf/line.h"
#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_scale_manipulator.h"
#include "opendcc/usd_editor/common_tools/viewport_scale_tool_command.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/usd_editor/common_tools/viewport_pivot_editor.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"

namespace PXR_NS
{
    TF_DEFINE_PUBLIC_TOKENS(ScaleToolTokens, ((name, "scale_tool")));
};

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandInterface;
using base::commands::CommandRegistry;

ViewportScaleToolContext::ViewportScaleToolContext()
    : ViewportSelectToolContext()
{
    auto settings = Application::instance().get_settings();
    m_manipulator = std::make_unique<ViewportScaleManipulator>();
    m_manipulator->set_step(settings->get("viewport.scale_tool.step", 1.0));
    m_manipulator->set_step_mode(static_cast<StepMode>(settings->get("viewport.scale_tool.step_mode", 0)));
    update_gizmo_via_selection();
    m_selection_changed_id =
        Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] { update_gizmo_via_selection(); });
    m_time_changed_id =
        Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this] { update_gizmo_via_selection(); });

    m_stage_object_changed_id = Application::instance().get_session()->register_stage_changed_callback(
        Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
        [this](UsdNotice::ObjectsChanged const& notice) { update_gizmo_via_selection(); });
}

ViewportScaleToolContext::~ViewportScaleToolContext()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_id);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_TIME_CHANGED, m_time_changed_id);
    Application::instance().get_session()->unregister_stage_changed_callback(Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
                                                                             m_stage_object_changed_id);
}

bool ViewportScaleToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                              ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;
    if (is_locked() || !m_scale_command)
        return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_press(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);
    }

    m_manipulator->on_mouse_press(mouse_event, viewport_view, draw_manager);
    if (m_manipulator->get_scale_mode() == ViewportScaleManipulator::ScaleMode::NONE)
        return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);
    m_scale_command->start_block();
    return true;
}

bool ViewportScaleToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                             ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;

    if (is_locked() || !m_scale_command)
        return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_move(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);
    }

    if (m_manipulator->get_scale_mode() == ViewportScaleManipulator::ScaleMode::NONE)
        return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);

    m_manipulator->on_mouse_move(mouse_event, viewport_view, draw_manager);
    m_scale_command->apply_delta(m_manipulator->get_delta());
    return true;
}

bool ViewportScaleToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                                ViewportUiDrawManager* draw_manager)
{
    if (is_locked() || !m_scale_command)
        return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_release(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);
    }

    if (m_manipulator->get_scale_mode() != ViewportScaleManipulator::ScaleMode::NONE)
    {
        m_manipulator->on_mouse_release(mouse_event, viewport_view, draw_manager);
        m_scale_command->end_block();
        CommandInterface::finalize(m_scale_command);
        m_scale_command = nullptr;
        update_gizmo_via_selection();
        return true;
    }
    else
    {
        return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);
    }
}

void ViewportScaleToolContext::update_gizmo_via_selection()
{
    if ((m_scale_command && m_scale_command->is_recording()) || (m_pivot_editor && m_pivot_editor->is_editing()))
        return;

    if (m_pivot_editor)
    {
        auto selection = Application::instance().get_selection();
        if (selection.empty())
        {
            set_edit_pivot(false);
            return;
        }
        else
        {
            m_pivot_editor = std::make_unique<ViewportPivotEditor>(Application::instance().get_selection(), ViewportPivotEditor::Orientation::OBJECT);
        }
    }

    m_scale_command = CommandRegistry::create_command<ViewportScaleToolCommand>("scale");
    m_scale_command->set_initial_state(Application::instance().get_selection());
    ViewportScaleManipulator::GizmoData gizmo_data;
    if (m_scale_command->get_start_gizmo_data(gizmo_data))
    {
        m_manipulator->set_gizmo_data(gizmo_data);
        m_manipulator->set_locked(!m_scale_command->can_edit());
    }
    else
    {
        m_scale_command = nullptr;
    }
}

void ViewportScaleToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_pivot_editor)
    {
        m_pivot_editor->draw(viewport_view, draw_manager);
        return;
    }

    if (m_scale_command && Application::instance().get_selection_mode() != Application::SelectionMode::UV)
    {
        m_manipulator->draw(viewport_view, draw_manager);
    }

    ViewportSelectToolContext::draw(viewport_view, draw_manager);
}

TfToken ViewportScaleToolContext::get_name() const
{
    return ScaleToolTokens->name;
}

void ViewportScaleToolContext::set_edit_pivot(bool is_edit)
{
    if (is_edit)
    {
        m_pivot_editor = std::make_unique<ViewportPivotEditor>(Application::instance().get_selection(), ViewportPivotEditor::Orientation::OBJECT);
    }
    else
    {
        m_pivot_editor = nullptr;
        update_gizmo_via_selection();
    }
    edit_pivot_mode_enabled(is_edit);
}

void ViewportScaleToolContext::reset_pivot()
{
    manipulator_utils::reset_pivot(Application::instance().get_selection());
}

ViewportScaleToolContext::StepMode ViewportScaleToolContext::get_step_mode() const
{
    return m_manipulator->get_step_mode();
}

void ViewportScaleToolContext::set_step_mode(StepMode mode)
{
    if (m_manipulator->get_step_mode() == mode)
        return;

    Application::instance().get_settings()->set("viewport.scale_tool.step_mode", static_cast<int>(mode));
    m_manipulator->set_step_mode(mode);
}

double ViewportScaleToolContext::get_step() const
{
    return m_manipulator->get_step();
}

void ViewportScaleToolContext::set_step(double step)
{
    if (GfIsClose(step, m_manipulator->get_step(), 0.000001) || step < 0.000001)
        return;

    Application::instance().get_settings()->set("viewport.scale_tool.step", step);
    m_manipulator->set_step(step);
}

bool ViewportScaleToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_scale_command && m_scale_command->is_recording())
        return true;

    if (key_event->key() == Qt::Key_J)
    {
        if (m_manipulator->get_step_mode() == StepMode::OFF)
        {
            set_step_mode(static_cast<StepMode>(Application::instance().get_settings()->get("viewport.scale_tool.last_step_mode", 1)));
        }
        else
        {
            Application::instance().get_settings()->set("viewport.scale_tool.last_step_mode", static_cast<int>(m_manipulator->get_step_mode()));
            set_step_mode(StepMode::OFF);
        }
        return true;
    }
    else if (key_event->key() == Qt::Key_D) // TODO: make separate class with hotkey D logic to get rid of copypast in move, rotate and scale tools
    {
        if (!m_edit_pivot)
        {
            set_edit_pivot(!m_pivot_editor);
        }

        if (!key_event->isAutoRepeat())
        {
            m_key_press_timepoint = key_event->timestamp();
        }
        m_edit_pivot = true;
    }
    return ViewportSelectToolContext::on_key_press(key_event, viewport_view, draw_manager);
}

bool ViewportScaleToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_scale_command && m_scale_command->is_recording())
        return true;

    if (key_event->key() == Qt::Key_D)
    {
        if (key_event->isAutoRepeat())
        {
            return ViewportSelectToolContext::on_key_release(key_event, viewport_view, draw_manager);
        }

        if ((key_event->timestamp() - m_key_press_timepoint) >= 300)
        {
            set_edit_pivot(!m_pivot_editor);
        }
        m_edit_pivot = false;
    }

    return ViewportSelectToolContext::on_key_release(key_event, viewport_view, draw_manager);
}

OPENDCC_NAMESPACE_CLOSE
