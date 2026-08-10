// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_move_tool_context.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include <QMouseEvent>
#include <QAction>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/line.h>
#include <pxr/base/gf/transform.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "opendcc/usd_editor/common_tools/viewport_move_tool_command.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include <QApplication>
#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/viewport/viewport_move_manipulator.h"
#include "opendcc/usd_editor/common_tools/viewport_pivot_editor.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/usd_editor/common_tools/viewport_usd_snap_strategy.h"
#include "opendcc/app/viewport/prim_material_override.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/usdShade/shader.h"
#include "pxr/usd/sdr/shaderProperty.h"
#include "pxr/imaging/hio/glslfx.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"

namespace PXR_NS
{
    TF_DEFINE_PUBLIC_TOKENS(MoveToolTokens, ((name, "move_tool")));
};

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

ViewportMoveToolContext::ViewportMoveToolContext()
    : ViewportSelectToolContext()
{
    auto settings = Application::instance().get_settings();
    m_axis_orientation = static_cast<AxisOrientation>(settings->get("viewport.move_tool.axis_orientation", static_cast<int>(AxisOrientation::WORLD)));
    m_manipulator = std::make_unique<ViewportMoveManipulator>();
    update_gizmo_via_selection();
    set_snap_mode(static_cast<SnapMode>(settings->get("viewport.move_tool.snap_mode", 0)));
    m_selection_changed_id =
        Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [&] { update_gizmo_via_selection(); });

    m_time_changed_id =
        Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [&] { update_gizmo_via_selection(); });

    m_stage_object_changed_id = Application::instance().get_session()->register_stage_changed_callback(
        Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED, [&](UsdNotice::ObjectsChanged const& notice) { update_gizmo_via_selection(); });
    m_settings_changed_cid["viewport.move_tool.snap_mode"] = Application::instance().get_settings()->register_setting_changed(
        "viewport.move_tool.snap_mode", [this](const std::string&, const Settings::Value& val, Settings::ChangeType) {
            int mode;
            if (!val.try_get<int>(&mode))
                return;

            const auto snap_mode = static_cast<SnapMode>(mode);
            set_snap_mode(snap_mode);
        });
}

ViewportMoveToolContext::~ViewportMoveToolContext()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_id);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_TIME_CHANGED, m_time_changed_id);
    Application::instance().get_session()->unregister_stage_changed_callback(Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
                                                                             m_stage_object_changed_id);
    for (const auto& entry : m_settings_changed_cid)
        Application::instance().get_settings()->unregister_setting_changed(entry.first, entry.second);
}

bool ViewportMoveToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                             ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;
    if (is_locked() || !m_move_command)
        return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_press(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);
    }

    m_manipulator->on_mouse_press(mouse_event, viewport_view, draw_manager);
    if (m_manipulator->get_move_mode() == ViewportMoveManipulator::MoveMode::NONE)
    {
        return ViewportSelectToolContext::on_mouse_press(mouse_event, viewport_view, draw_manager);
    }
    m_move_command->start_block();
    return true;
}

bool ViewportMoveToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                            ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;

    if (is_locked() || !m_move_command)
        return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);

    if (auto screen_snap_strategy = std::dynamic_pointer_cast<ViewportUsdMeshScreenSnapStrategy>(m_snap_strategy))
    {
        screen_snap_strategy->set_viewport_data(viewport_view, GfVec2f(mouse_event.x(), mouse_event.y()), Application::instance().get_current_time());
    }

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_move(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);
    }

    if (m_manipulator->get_move_mode() == ViewportMoveManipulator::MoveMode::NONE)
        return ViewportSelectToolContext::on_mouse_move(mouse_event, viewport_view, draw_manager);

    m_manipulator->on_mouse_move(mouse_event, viewport_view, draw_manager);
    m_move_command->apply_delta(m_manipulator->get_delta());
    return true;
}

bool ViewportMoveToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                               ViewportUiDrawManager* draw_manager)
{
    if (is_locked() || !m_move_command)
        return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);

    if (m_pivot_editor)
    {
        if (m_pivot_editor->on_mouse_release(mouse_event, viewport_view, draw_manager))
            return true;
        else
            return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);
    }

    if (m_manipulator->get_move_mode() != ViewportMoveManipulator::MoveMode::NONE)
    {
        m_manipulator->on_mouse_release(mouse_event, viewport_view, draw_manager);
        m_move_command->end_block();
        CommandInterface::finalize(m_move_command);
        m_move_command = nullptr;
        update_gizmo_via_selection();
        return true;
    }
    else
    {
        return ViewportSelectToolContext::on_mouse_release(mouse_event, viewport_view, draw_manager);
    }
}

void ViewportMoveToolContext::update_gizmo_via_selection()
{
    if ((m_move_command && m_move_command->is_recording()) || (m_pivot_editor && m_pivot_editor->is_editing()))
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
            m_pivot_editor = std::make_unique<ViewportPivotEditor>(selection, m_axis_orientation == AxisOrientation::OBJECT
                                                                                  ? ViewportPivotEditor::Orientation::OBJECT
                                                                                  : ViewportPivotEditor::Orientation::WORLD);
        }
    }

    m_move_command = CommandRegistry::create_command<ViewportMoveToolCommand>("move");
    m_move_command->set_initial_state(Application::instance().get_selection(), m_axis_orientation);
    GfMatrix4d gizmo_matrix;
    if (m_move_command->get_start_gizmo_matrix(gizmo_matrix))
    {
        m_manipulator->set_gizmo_matrix(gizmo_matrix);
        m_manipulator->set_locked(!m_move_command->can_edit());
    }
    else
    {
        m_move_command = nullptr;
    }
    update_snap_strategy();
}

void ViewportMoveToolContext::update_snap_strategy()
{
    switch (m_snap_mode)
    {
    case SnapMode::RELATIVE_MODE:
    {
        const auto step = get_step();
        m_snap_strategy = std::make_shared<ViewportRelativeSnapStrategy>(step);
        break;
    }
    case SnapMode::ABSOLUTE_MODE:
    {
        const auto step = get_step();
        m_snap_strategy = std::make_shared<ViewportAbsoluteSnapStrategy>(step);
        break;
    }
    case SnapMode::GRID:
    {
        const auto grid_enabled = Application::instance().get_settings()->get("viewport.grid.enable", false);
        if (grid_enabled)
        {
            const auto step = Application::instance().get_settings()->get("viewport.grid.min_step", 1.0);
            m_snap_strategy = std::make_shared<ViewportAbsoluteSnapStrategy>(step);
        }
        else
        {
            m_snap_strategy = nullptr;
        }
        break;
    }
    case SnapMode::VERTEX:
    {
        m_snap_strategy = m_move_command ? std::make_shared<ViewportUsdVertexScreenSnapStrategy>(m_move_command->get_selection()) : nullptr;
        break;
    }
    case SnapMode::EDGE:
    {
        m_snap_strategy = m_move_command ? std::make_shared<ViewportUsdEdgeScreenSnapStrategy>(m_move_command->get_selection(), false) : nullptr;
        break;
    }
    case SnapMode::EDGE_CENTER:
    {
        m_snap_strategy = m_move_command ? std::make_shared<ViewportUsdEdgeScreenSnapStrategy>(m_move_command->get_selection(), true) : nullptr;
        break;
    }
    case SnapMode::FACE_CENTER:
    {
        m_snap_strategy = m_move_command ? std::make_shared<ViewportUsdFaceScreenSnapStrategy>(m_move_command->get_selection(), true) : nullptr;
        break;
    }
    case SnapMode::OBJECT_SURFACE:
    {
        m_snap_strategy = m_move_command ? std::make_shared<ViewportUsdFaceScreenSnapStrategy>(m_move_command->get_selection(), false) : nullptr;
        break;
    }
    default:
        m_snap_strategy = nullptr;
    }
    m_manipulator->set_snap_strategy(m_snap_strategy);
    if (m_pivot_editor)
        m_pivot_editor->set_snap_strategy(m_snap_strategy);
}

void ViewportMoveToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_pivot_editor)
    {
        m_pivot_editor->draw(viewport_view, draw_manager);
        return;
    }

    if (m_move_command && Application::instance().get_selection_mode() != Application::SelectionMode::UV)
    {
        m_manipulator->draw(viewport_view, draw_manager);
    }

    ViewportSelectToolContext::draw(viewport_view, draw_manager);
}

TfToken ViewportMoveToolContext::get_name() const
{
    return MoveToolTokens->name;
}

void ViewportMoveToolContext::set_axis_orientation(AxisOrientation axis_orientation)
{
    if (m_axis_orientation == axis_orientation)
        return;

    Application::instance().get_settings()->set("viewport.move_tool.axis_orientation", static_cast<int>(axis_orientation));
    m_axis_orientation = axis_orientation;
    update_gizmo_via_selection();
}

void ViewportMoveToolContext::set_edit_pivot(bool is_edit)
{
    if (is_edit)
    {
        m_pivot_editor = std::make_unique<ViewportPivotEditor>(
            Application::instance().get_selection(),
            m_axis_orientation == AxisOrientation::OBJECT ? ViewportPivotEditor::Orientation::OBJECT : ViewportPivotEditor::Orientation::WORLD);
        m_pivot_editor->set_snap_strategy(m_snap_strategy);
    }
    else
    {
        m_pivot_editor = nullptr;
        update_gizmo_via_selection();
    }
    edit_pivot_mode_enabled(is_edit);
}

void ViewportMoveToolContext::reset_pivot()
{
    manipulator_utils::reset_pivot(Application::instance().get_selection());
}

ViewportMoveToolContext::SnapMode ViewportMoveToolContext::get_snap_mode() const
{
    return m_snap_mode;
}

void ViewportMoveToolContext::set_snap_mode(SnapMode mode)
{
    if (m_snap_mode == mode)
        return;

    m_snap_mode = mode;
    update_snap_strategy();
}

double ViewportMoveToolContext::get_step() const
{
    return Application::instance().get_settings()->get("viewport.move_tool.step", 1.0);
}

void ViewportMoveToolContext::set_step(double step)
{
    const auto cur_step = get_step();
    if (GfIsClose(step, cur_step, 0.000001) || step < 0.000001)
        return;

    Application::instance().get_settings()->set("viewport.move_tool.step", step);
    // Recreate snap strategy with new params
    if (get_snap_mode() == SnapMode::ABSOLUTE_MODE || get_snap_mode() == SnapMode::RELATIVE_MODE)
        update_snap_strategy();
}

bool ViewportMoveToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_move_command && m_move_command->is_recording())
        return true;

    if (key_event->key() == Qt::Key_J)
    {
        if (get_snap_mode() == SnapMode::OFF || get_snap_mode() == SnapMode::GRID)
        {
            Application::instance().get_settings()->set(
                "viewport.move_tool.snap_mode",
                Application::instance().get_settings()->get("viewport.move_tool.last_snap_mode", static_cast<int>(SnapMode::RELATIVE_MODE)));
        }
        else
        {
            Application::instance().get_settings()->set("viewport.move_tool.last_snap_mode", static_cast<int>(get_snap_mode()));
            Application::instance().get_settings()->set("viewport.move_tool.snap_mode", static_cast<int>(SnapMode::OFF));
        }
        return true;
    }
    else if (key_event->key() == Qt::Key_X)
    {
        if (get_snap_mode() == SnapMode::GRID)
        {
            Application::instance().get_settings()->set("viewport.move_tool.snap_mode", static_cast<int>(SnapMode::OFF));
        }
        else
        {
            Application::instance().get_settings()->set("viewport.move_tool.snap_mode", static_cast<int>(SnapMode::GRID));
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

bool ViewportMoveToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_move_command && m_move_command->is_recording())
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
