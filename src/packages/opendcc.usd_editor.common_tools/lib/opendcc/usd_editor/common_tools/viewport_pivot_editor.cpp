// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_pivot_editor.h"
#include "opendcc/app/viewport/viewport_move_manipulator.h"
#include "opendcc/usd_editor/common_tools/viewport_change_pivot_command.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/undo/stack.h"
#include "pxr/base/gf/frustum.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "pxr/imaging/cameraUtil/conformWindow.h"
#include "pxr/base/gf/matrix4f.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/commands_api/core/command_interface.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandRegistry;
using base::commands::CommandInterface;

ViewportPivotEditor::ViewportPivotEditor(const SelectionList& selection_list, Orientation orientation)
    : m_selection(selection_list)
{
    m_command = CommandRegistry::create_command<ViewportChangePivotCommand>("change_pivot");
    m_command->set_initial_state(selection_list);
    m_manipulator = std::make_unique<ViewportMoveManipulator>();
    set_orientation(orientation);
}

bool ViewportPivotEditor::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                         ViewportUiDrawManager* draw_manager)
{
    m_manipulator->on_mouse_press(mouse_event, viewport_view, draw_manager);
    if (m_manipulator->get_move_mode() == ViewportMoveManipulator::MoveMode::NONE)
        return false;
    m_command->start_block();
    return true;
}

bool ViewportPivotEditor::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                        ViewportUiDrawManager* draw_manager)
{
    if (m_manipulator->get_move_mode() == ViewportMoveManipulator::MoveMode::NONE)
        return false;
    m_manipulator->on_mouse_move(mouse_event, viewport_view, draw_manager);
    m_command->apply_delta(m_manipulator->get_delta(), GfRotation(GfVec3d::XAxis(), 0));
    return true;
}

bool ViewportPivotEditor::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                           ViewportUiDrawManager* draw_manager)
{
    if (m_manipulator->get_move_mode() == ViewportMoveManipulator::MoveMode::NONE)
        return false;
    m_manipulator->on_mouse_release(mouse_event, viewport_view, draw_manager);
    m_command->end_block();
    CommandInterface::finalize(m_command);
    m_command = CommandRegistry::create_command<ViewportChangePivotCommand>("change_pivot");
    m_command->set_initial_state(m_selection);
    return true;
}

void ViewportPivotEditor::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    m_manipulator->draw(viewport_view, draw_manager);
    GfCamera camera = viewport_view->get_camera();
    GfFrustum frustum = camera.GetFrustum();

    const auto viewport_dim = viewport_view->get_viewport_dimensions();
    CameraUtilConformWindow(&frustum, CameraUtilConformWindowPolicy::CameraUtilFit,
                            viewport_dim.height != 0 ? (double)viewport_dim.width / viewport_dim.height : 1.0);

    const auto gizmo_center = m_manipulator->get_gizmo_matrix().ExtractTranslation();
    const double screen_factor = manipulator_utils::compute_screen_factor(viewport_view, GfVec3d(gizmo_center[0], gizmo_center[1], gizmo_center[2]));

    GfMatrix4f cached_mat = GfMatrix4f(GfMatrix4d().SetTranslate(m_manipulator->get_gizmo_matrix().ExtractTranslation()) *
                                       frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix());

    const auto up = GfVec3f(frustum.ComputeUpVector()).GetNormalized();
    const auto right = GfVec3f(up ^ frustum.ComputeViewDirection()).GetNormalized();

    const auto locate_color = GfVec4f(1, 0.75, 0.5, 1);
    const auto orig = GfVec3f(0, 0, 0);
    auto mvp_matrixf1 = GfMatrix4f().SetScale(0.025 * screen_factor) * cached_mat;
    auto mvp_matrixf2 = GfMatrix4f().SetScale(0.05 * screen_factor) * cached_mat;
    draw_utils::draw_outlined_circle(draw_manager, mvp_matrixf1, locate_color, locate_color, GfVec3f(0, 0, 0), right, up, 1.0, 0, 0);
    draw_utils::draw_circle(draw_manager, mvp_matrixf2, locate_color, GfVec3f(0, 0, 0), right, up, 1.0f, 0, 0);
    draw_manager->begin_drawable();
    draw_manager->set_prim_type(ViewportUiDrawManager::PrimitiveTypeLines);
    draw_manager->set_mvp_matrix(GfMatrix4f().SetScale(screen_factor) * cached_mat);
    draw_manager->set_paint_style(ViewportUiDrawManager::PaintStyle::FLAT);
    draw_manager->set_line_width(1.0f);
    draw_manager->set_color(locate_color);
    draw_manager->line(orig, up * 0.08);
    draw_manager->line(orig, right * 0.08);
    draw_manager->line(orig, -up * 0.08);
    draw_manager->line(orig, -right * 0.08);
    draw_manager->end_drawable();
}

bool ViewportPivotEditor::is_editing() const
{
    return m_command->is_recording();
}

void ViewportPivotEditor::set_orientation(Orientation orientation)
{
    ViewportChangePivotCommand::PivotInfo pi;
    if (m_command->get_pivot_info(pi))
    {
        if (orientation == Orientation::WORLD)
            pi.orientation.SetIdentity();
        m_manipulator->set_gizmo_matrix(GfMatrix4d(pi.orientation, pi.position));
        m_manipulator->set_locked(!m_command->can_edit());
    }
}

void ViewportPivotEditor::set_snap_strategy(std::shared_ptr<ViewportSnapStrategy> snap_strategy)
{
    m_manipulator->set_snap_strategy(snap_strategy);
}

OPENDCC_NAMESPACE_CLOSE
