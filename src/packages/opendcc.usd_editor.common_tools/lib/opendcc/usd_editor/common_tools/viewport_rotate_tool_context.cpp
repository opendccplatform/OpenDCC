// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_context.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include <QAction>
#include <QMouseEvent>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/pxr.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/xformable.h>
#include "opendcc/app/viewport/viewport_rotate_manipulator.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_command.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/command_registry.h"

namespace PXR_NS
{
    TF_DEFINE_PUBLIC_TOKENS(RotateToolTokens, ((name, "rotate_tool")));
};

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandInterface;
using base::commands::CommandRegistry;

ViewportRotateToolContext::ViewportRotateToolContext()
    : ViewportSelectToolContext()
{
    auto settings = Application::instance().get_settings();
    const auto orientation_setting =
        Application::instance().get_settings()->get("viewport.rotate_tool.orientation", static_cast<int>(Orientation::OBJECT));
    m_orientation = static_cast<Orientation>(orientation_setting);

    m_manipulator = std::make_unique<ViewportRotateManipulator>();
    m_manipulator->set_step(settings->get("viewport.rotate_tool.step", 10.0));
    m_manipulator->enable_step_mode(settings->get("viewport.rotate_tool.step_mode", false));
    update_gizmo_via_selection();
    m_selection_changed_id =
        Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] { update_gizmo_via_selection(); });
    m_time_changed_id =
        Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this] { update_gizmo_via_selection(); });
    m_stage_object_changed_id = Application::instance().get_session()->register_stage_changed_callback(
        Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED, [this](const UsdNotice::ObjectsChanged&) { update_gizmo_via_selection(); });
}

ViewportRotateToolContext::~ViewportRotateToolContext()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_id);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_TIME_CHANGED, m_time_changed_id);
    Application::instance().get_session()->unregister_stage_changed_callback(Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
                                                                             m_stage_object_changed_id);
}

bool ViewportRotateToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                               ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;
    if (is_locked() || !m_rotate_command)
        return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_press(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);
    }

    m_manipulator->on_mouse_press(mouse_event, viewport_view, draw_manager);
    if (m_manipulator->get_rotate_mode() == ViewportRotateManipulator::RotateMode::NONE)
        return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);

    if (m_orientation == Orientation::GIMBAL && m_manipulator->get_rotate_mode() == ViewportRotateManipulator::RotateMode::XYZ)
        return true;

    m_rotate_command->start_block();
    return true;
}

bool ViewportRotateToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                              ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;
    if (is_locked() || !m_rotate_command)
        return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_move(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);
    }

    if (m_manipulator->get_rotate_mode() == ViewportRotateManipulator::RotateMode::NONE ||
        (m_orientation == Orientation::GIMBAL && m_manipulator->get_rotate_mode() == ViewportRotateManipulator::RotateMode::XYZ))
        return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);

    m_manipulator->on_mouse_move(mouse_event, viewport_view, draw_manager);
    m_rotate_command->apply_delta(m_manipulator->get_delta());
    return true;
}

bool ViewportRotateToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                                 ViewportUiDrawManager* draw_manager)
{
    if (is_locked() || !m_rotate_command)
        return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_release(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);
    }

    if (m_manipulator->get_rotate_mode() != ViewportRotateManipulator::RotateMode::NONE)
    {
        m_manipulator->on_mouse_release(mouse_event, viewport_view, draw_manager);
        m_rotate_command->end_block();
        CommandInterface::finalize(m_rotate_command);
        m_rotate_command = nullptr;
        update_gizmo_via_selection();
        return true;
    }
    else
    {
        return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);
    }
}

void ViewportRotateToolContext::update_gizmo_via_selection()
{
    if ((m_rotate_command && m_rotate_command->is_recording()) || (m_pivot_editor && m_pivot_editor->is_editing()))
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
            m_pivot_editor = std::make_unique<ViewportPivotEditor>(Application::instance().get_selection(),
                                                                   m_orientation == Orientation::WORLD ? ViewportPivotEditor::Orientation::WORLD
                                                                                                       : ViewportPivotEditor::Orientation::OBJECT);
        }
    }

    m_rotate_command = CommandRegistry::create_command<ViewportRotateToolCommand>("rotate");
    m_rotate_command->set_initial_state(Application::instance().get_selection(), m_orientation);
    ViewportRotateManipulator::GizmoData gizmo_data;
    if (m_rotate_command->get_start_gizmo_data(gizmo_data))
    {
        m_manipulator->set_gizmo_data(gizmo_data);
        m_manipulator->set_orientation(m_orientation);
        m_manipulator->set_gizmo_locked(m_rotate_command->affects_components());
        m_manipulator->set_locked(!m_rotate_command->can_edit());
    }
    else
    {
        m_rotate_command = nullptr;
    }
}

void ViewportRotateToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_pivot_editor)
    {
        m_pivot_editor->draw(viewport_view, draw_manager);
        return;
    }

    if (m_rotate_command && Application::instance().get_selection_mode() != Application::SelectionMode::UV)
    {
        m_manipulator->draw(viewport_view, draw_manager);
    }

    ViewportSelectToolContext::draw(viewport_view, draw_manager);
}

TfToken ViewportRotateToolContext::get_name() const
{
    return RotateToolTokens->name;
}

void ViewportRotateToolContext::set_orientation(Orientation orientation)
{
    if (m_orientation == orientation)
        return;

    Application::instance().get_settings()->set("viewport.rotate_tool.orientation", static_cast<int>(orientation));
    m_orientation = orientation;
    m_manipulator->set_orientation(m_orientation);
    update_gizmo_via_selection();
}

void ViewportRotateToolContext::set_edit_pivot(bool is_edit)
{
    if (is_edit)
    {
        m_pivot_editor = std::make_unique<ViewportPivotEditor>(Application::instance().get_selection(),
                                                               m_orientation == Orientation::WORLD ? ViewportPivotEditor::Orientation::WORLD
                                                                                                   : ViewportPivotEditor::Orientation::OBJECT);
    }
    else
    {
        m_pivot_editor = nullptr;
        update_gizmo_via_selection();
    }
    edit_pivot_mode_enabled(is_edit);
}

void ViewportRotateToolContext::reset_pivot()
{
    manipulator_utils::reset_pivot(Application::instance().get_selection());
}

bool ViewportRotateToolContext::is_step_mode_enabled() const
{
    return m_manipulator->is_step_mode_enabled();
}

void ViewportRotateToolContext::enable_step_mode(bool enable)
{
    if (m_manipulator->is_step_mode_enabled() == enable)
        return;

    Application::instance().get_settings()->set("viewport.rotate_tool.step_mode", enable);
    m_manipulator->enable_step_mode(enable);
}

double ViewportRotateToolContext::get_step() const
{
    return m_manipulator->get_step();
}

void ViewportRotateToolContext::set_step(double step)
{
    if (GfIsClose(step, m_manipulator->get_step(), 0.000001) || step < 0.000001)
        return;

    Application::instance().get_settings()->set("viewport.rotate_tool.step", step);
    m_manipulator->set_step(step);
}

bool ViewportRotateToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_rotate_command && m_rotate_command->is_recording())
        return true;

    if (key_event->key() == Qt::Key_J)
    {
        enable_step_mode(!m_manipulator->is_step_mode_enabled());
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

bool ViewportRotateToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_rotate_command && m_rotate_command->is_recording())
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
