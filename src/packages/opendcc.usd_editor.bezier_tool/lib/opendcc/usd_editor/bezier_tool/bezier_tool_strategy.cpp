// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/bezier_tool_strategy.h"
#include "opendcc/usd_editor/bezier_tool/add_point_to_curve_command.h"
#include "opendcc/usd_editor/bezier_tool/change_curve_point_command.h"
#include "opendcc/usd_editor/bezier_tool/close_curve_command.h"
#include "opendcc/usd_editor/bezier_tool/bezier_tool_context.h"
#include "opendcc/usd_editor/bezier_tool/bezier_curve.h"

#include "opendcc/usd_editor/bezier_tool/utils.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"

#include "opendcc/base/commands_api/core/command_interface.h"

#include <pxr/base/gf/line.h>
#include <pxr/base/gf/math.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandInterface;

//////////////////////////////////////////////////////////////////////////
// BezierToolStrategy
//////////////////////////////////////////////////////////////////////////

BezierToolStrategy::BezierToolStrategy(BezierToolContext* context)
    : m_context(context)
{
}

/* virtual */
BezierToolStrategy::~BezierToolStrategy() {}

bool BezierToolStrategy::is_finished()
{
    return m_finished;
}

void BezierToolStrategy::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    const auto& info = m_context->get_info();
    auto curve = m_context->get_curve();

    if (is_finished())
    {
        curve->set_intersect_point(info.intersect_curve_point_index);
    }
    curve->set_select_point(info.select_curve_point_index);

    if (is_finished())
    {
        curve->set_intersect_tangent(info.intersect_curve_tangent_info);
    }
    curve->set_select_tangent(info.select_curve_tangent_info);

    curve->draw(viewport_view, draw_manager);
}

BezierToolContext* BezierToolStrategy::get_context()
{
    return m_context;
}

void BezierToolStrategy::set_finished(bool finished)
{
    m_finished = finished;
}

//////////////////////////////////////////////////////////////////////////
// BezierToolNullStrategy
//////////////////////////////////////////////////////////////////////////

BezierToolNullStrategy::BezierToolNullStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
    set_finished(true);
}

bool BezierToolNullStrategy::on_mouse_press(const ViewportMouseEvent&, const ViewportViewPtr&, ViewportUiDrawManager*) /* override */
{
    return true;
}

bool BezierToolNullStrategy::on_mouse_move(const ViewportMouseEvent&, const ViewportViewPtr&, ViewportUiDrawManager*) /* override */
{
    return true;
}

bool BezierToolNullStrategy::on_mouse_release(const ViewportMouseEvent&, const ViewportViewPtr&, ViewportUiDrawManager*) /* override */
{
    return true;
}

//////////////////////////////////////////////////////////////////////////
// AddPointStrategy
//////////////////////////////////////////////////////////////////////////

AddPointStrategy::AddPointStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
}

bool AddPointStrategy::on_mouse_press(const ViewportMouseEvent&, const ViewportViewPtr&, ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();
    auto curve = context->get_curve();

    if (!curve->is_empty() && info.select_curve_point_index == BezierCurve::s_invalid_index &&
        info.intersect_curve_point_index == BezierCurve::s_invalid_index)
    {
        context->reset_curve();
        curve = context->get_curve();
        set_finished(!info.lmb_pressed || !info.intersect_curve_plane || info.modifiers);
    }
    else
    {
        set_finished(!info.lmb_pressed || !info.intersect_curve_plane || !info.select_last_point || info.modifiers);
    }

    if (is_finished())
    {
        return true;
    }

    if (curve->is_empty())
    {
        context->lock_commands();
    }

    curve->push_back(info.intersect_curve_plane_point);

    context->set_select_curve_point_index(curve->size() - 1);

    m_edit = true;

    return true;
}

bool AddPointStrategy::on_mouse_move(const ViewportMouseEvent&, const ViewportViewPtr&, ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed);
    if (is_finished())
    {
        return true;
    }

    update();

    return true;
}

bool AddPointStrategy::on_mouse_release(const ViewportMouseEvent&, const ViewportViewPtr&, ViewportUiDrawManager*) /* override */
{
    if (is_finished())
    {
        return true;
    }
    auto context = get_context();
    const auto& info = context->get_info();
    set_finished(!info.lmb_pressed);

    return true;
}

void AddPointStrategy::update()
{
    auto context = get_context();
    const auto& info = context->get_info();

    if (!info.intersect_curve_plane || !info.select_last_point || !m_edit)
    {
        return;
    }

    context->update_point(BezierCurve::Tangent { info.select_curve_point_index, (info.last_curve_point_index ? BezierCurve::Tangent::Type::Right
                                                                                                             : BezierCurve::Tangent::Type::Left) },
                          info.intersect_curve_plane_point, BezierCurve::TangentMode::Normal);
}

void AddPointStrategy::set_finished(bool finished)
{
    if (finished && m_edit && finished != is_finished())
    {
        auto context = get_context();
        auto curve = context->get_curve();
        CommandInterface::finalize(std::make_shared<AddPointToCurveCommand>(curve, context, curve->last()));

        context->unlock_commands();

        m_edit = false;
    }

    BezierToolStrategy::set_finished(finished);
}

//////////////////////////////////////////////////////////////////////////
// ResetTangentsStrategy
//////////////////////////////////////////////////////////////////////////

ResetTangentsStrategy::ResetTangentsStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
}

bool ResetTangentsStrategy::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                           ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.ctrl_modifier || !info.lmb_pressed || !info.intersect_curve_point);
    if (is_finished())
    {
        return true;
    }

    context->set_select_curve_point_index(info.intersect_curve_point_index);

    auto curve = context->get_curve();

    const auto point = curve->get_point(info.select_curve_point_index).point;
    m_old_point = curve->get_point(info.select_curve_point_index);
    curve->update_tangents(info.select_curve_point_index, point, point);
    m_edit = true;

    m_drag_direction = info.pick_ray.GetDirection();

    const auto intersection =
        manipulator_utils::compute_plane_intersection(viewport_view, GfVec3d(m_old_point.point), m_drag_direction,
                                                      compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), m_start_move_point);

    set_finished(!intersection);

    return true;
}

bool ResetTangentsStrategy::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                          ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed);
    if (is_finished())
    {
        return true;
    }

    update(mouse_event, viewport_view);

    return true;
}

bool ResetTangentsStrategy::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                             ViewportUiDrawManager*) /* override */
{
    if (is_finished())
    {
        return true;
    }
    auto context = get_context();
    const auto& info = context->get_info();
    set_finished(!info.lmb_pressed);

    return true;
}

void ResetTangentsStrategy::update(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    GfVec3d intersection_point;
    if (!manipulator_utils::compute_plane_intersection(viewport_view, GfVec3d(m_old_point.point), m_drag_direction,
                                                       compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), intersection_point))
    {
        return;
    }

    auto diff = GfVec3f(intersection_point - m_start_move_point);
    auto new_tangent = m_old_point.point + diff;

    auto context = get_context();
    const auto& info = context->get_info();

    context->update_point(BezierCurve::Tangent { info.select_curve_point_index, (info.last_curve_point_index ? BezierCurve::Tangent::Type::Right
                                                                                                             : BezierCurve::Tangent::Type::Left) },
                          new_tangent, BezierCurve::TangentMode::Normal);
}

void ResetTangentsStrategy::set_finished(bool finished) /* override */
{
    if (finished && m_edit && finished != is_finished())
    {
        const auto context = get_context();
        const auto curve = context->get_curve();
        const auto& info = context->get_info();
        CommandInterface::finalize(
            std::make_shared<ChangeCurvePointCommand>("ResetTangents", curve, context, info.select_curve_point_index, m_old_point));

        m_edit = false;
    }

    BezierToolStrategy::set_finished(finished);
}

//////////////////////////////////////////////////////////////////////////
// MovePointStrategy
//////////////////////////////////////////////////////////////////////////

MovePointStrategy::MovePointStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
}

bool MovePointStrategy::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                       ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed || !info.intersect_curve_point);
    if (is_finished())
    {
        return true;
    }

    context->set_select_curve_point_index(info.intersect_curve_point_index);

    auto curve = context->get_curve();
    m_old_point = curve->get_point(info.intersect_curve_point_index);

    m_drag_direction = info.pick_ray.GetDirection();

    const auto intersection =
        manipulator_utils::compute_plane_intersection(viewport_view, GfVec3d(m_old_point.point), m_drag_direction,
                                                      compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), m_start_move_point);

    set_finished(!intersection);

    return true;
}

bool MovePointStrategy::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                      ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed);
    if (is_finished())
    {
        return true;
    }

    update(mouse_event, viewport_view);

    return true;
}

bool MovePointStrategy::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                         ViewportUiDrawManager*) /* override */
{
    if (is_finished())
    {
        return true;
    }
    auto context = get_context();
    const auto& info = context->get_info();
    set_finished(!info.lmb_pressed);

    return true;
}

void MovePointStrategy::update(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    auto context = get_context();
    const auto& info = context->get_info();

    GfVec3d intersection_point;
    if (manipulator_utils::compute_plane_intersection(viewport_view, GfVec3d(m_old_point.point), m_drag_direction,
                                                      compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), intersection_point))
    {
        auto diff = GfVec3f(intersection_point - m_start_move_point);
        auto point = m_old_point;
        point.point += diff;
        point.ltangent += diff;
        point.rtangent += diff;

        auto curve = context->get_curve();
        curve->set_point(info.select_curve_point_index, point);

        m_edit = true;
    }
}

void MovePointStrategy::set_finished(bool finished) /* override */
{
    if (finished && m_edit && finished != is_finished())
    {
        const auto context = get_context();
        const auto curve = context->get_curve();
        const auto& info = context->get_info();
        CommandInterface::finalize(std::make_shared<ChangeCurvePointCommand>("Move", curve, context, info.select_curve_point_index, m_old_point));

        m_edit = false;
    }

    BezierToolStrategy::set_finished(finished);
}

//////////////////////////////////////////////////////////////////////////
// EditTangentStrategy
//////////////////////////////////////////////////////////////////////////

EditTangentStrategy::EditTangentStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
    m_mode = context->get_tangent_mode();
}

bool EditTangentStrategy::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                         ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed || !info.intersect_tangent_selected_point);
    if (is_finished())
    {
        return true;
    }
    context->set_select_curve_tangent_info(info.intersect_curve_tangent_info);
    m_mode = context->correct_mode(info.intersect_curve_tangent_info.point_index, context->get_tangent_mode());
    m_point_index = info.intersect_curve_tangent_info.point_index;
    auto curve = context->get_curve();
    m_old_point = curve->get_point(info.intersect_curve_tangent_info.point_index);

    m_drag_direction = info.pick_ray.GetDirection();

    m_plane_point =
        GfVec3d(info.intersect_curve_tangent_info.type == BezierCurve::Tangent::Type::Right ? m_old_point.rtangent : m_old_point.ltangent);

    const auto intersection = manipulator_utils::compute_plane_intersection(
        viewport_view, m_plane_point, m_drag_direction, compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), m_start_move_point);

    set_finished(!intersection);

    return true;
}

bool EditTangentStrategy::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                        ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed);
    if (is_finished())
    {
        return true;
    }

    update(mouse_event, viewport_view);

    return true;
}

bool EditTangentStrategy::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                           ViewportUiDrawManager*) /* override */
{
    if (is_finished())
    {
        return true;
    }
    auto context = get_context();
    const auto& info = context->get_info();
    set_finished(!info.lmb_pressed);

    return true;
}

void EditTangentStrategy::update(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    auto context = get_context();
    const auto& info = context->get_info();

    GfVec3d intersection_point;
    if (!manipulator_utils::compute_plane_intersection(viewport_view, m_plane_point, m_drag_direction, compute_view_projection(viewport_view),
                                                       mouse_event.x(), mouse_event.y(), intersection_point))
    {
        return;
    }

    auto diff = GfVec3f(intersection_point - m_start_move_point);
    auto old_tangent = info.select_curve_tangent_info.type == BezierCurve::Tangent::Type::Right ? m_old_point.rtangent : m_old_point.ltangent;

    auto mode = context->get_tangent_mode();

    auto new_tangent = old_tangent + diff;

    if (info.shift_modifier)
    {
        auto curve = context->get_curve();
        const auto point = curve->get_point(info.select_curve_tangent_info.point_index);

        const auto direction =
            point.point - (info.select_curve_tangent_info.type == BezierCurve::Tangent::Type::Right ? point.rtangent : point.ltangent);

        GfLine line(point.point, direction.GetNormalized());
        GfVec3d ray_point, line_point;
        if (!GfFindClosestPoints(info.pick_ray, line, &ray_point, &line_point))
        {
            return;
        }
        new_tangent = GfVec3f(line_point);
    }

    if (info.ctrl_modifier)
    {
        m_mode = BezierCurve::TangentMode::Tangent;
    }

    context->update_point(info.select_curve_tangent_info, new_tangent, m_mode);

    m_edit = true;
}

void EditTangentStrategy::set_finished(bool finished) /* override */
{
    if (finished && m_edit && finished != is_finished())
    {
        const auto context = get_context();
        const auto curve = context->get_curve();
        context->set_select_curve_tangent_info(BezierCurve::Tangent());
        CommandInterface::finalize(std::make_shared<ChangeCurvePointCommand>("EditTangent", curve, context, m_point_index, m_old_point));

        m_edit = false;
    }

    BezierToolStrategy::set_finished(finished);
}

//////////////////////////////////////////////////////////////////////////
// CloseCurveStrategy
//////////////////////////////////////////////////////////////////////////

CloseCurveStrategy::CloseCurveStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
}

bool CloseCurveStrategy::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                        ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();
    auto curve = context->get_curve();

    set_finished(!info.ctrl_modifier || !info.shift_modifier || !info.lmb_pressed || !info.intersect_curve_point || curve->size() < 2 ||
                 curve->is_close() || !info.select_last_point);
    if (is_finished())
    {
        return true;
    }

    context->set_select_curve_point_index(info.intersect_curve_point_index);

    m_old_point = curve->first();
    curve->close();

    m_drag_direction = info.pick_ray.GetDirection();

    const auto intersection =
        manipulator_utils::compute_plane_intersection(viewport_view, GfVec3d(m_old_point.point), m_drag_direction,
                                                      compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), m_start_move_point);

    set_finished(!intersection);

    m_edit = true;

    return true;
}

bool CloseCurveStrategy::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                       ViewportUiDrawManager*) /* override */
{
    auto context = get_context();
    const auto& info = context->get_info();

    set_finished(!info.lmb_pressed);
    if (is_finished())
    {
        return true;
    }

    if (info.select_curve_point_index != info.intersect_curve_point_index)
    {
        update(mouse_event, viewport_view);
    }

    return true;
}

bool CloseCurveStrategy::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                          ViewportUiDrawManager*) /* override */
{
    if (is_finished())
    {
        return true;
    }
    auto context = get_context();
    const auto& info = context->get_info();
    set_finished(!info.lmb_pressed);

    return true;
}

void CloseCurveStrategy::update(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    auto context = get_context();
    const auto& info = context->get_info();

    GfVec3d intersection_point;
    if (!manipulator_utils::compute_plane_intersection(viewport_view, GfVec3d(m_old_point.point), m_drag_direction,
                                                       compute_view_projection(viewport_view), mouse_event.x(), mouse_event.y(), intersection_point))
    {
        return;
    }

    auto diff = GfVec3f(intersection_point - m_start_move_point);
    auto old_tangent = m_old_point.point;

    auto mode = context->get_tangent_mode();

    auto new_tangent = old_tangent + diff;

    context->update_point(BezierCurve::Tangent { info.select_curve_point_index, (info.last_curve_point_index ? BezierCurve::Tangent::Type::Right
                                                                                                             : BezierCurve::Tangent::Type::Left) },
                          new_tangent, BezierCurve::TangentMode::Normal);
}

void CloseCurveStrategy::set_finished(bool finished) /* override */
{
    if (finished && m_edit && finished != is_finished())
    {
        const auto context = get_context();
        const auto curve = context->get_curve();
        CommandInterface::finalize(std::make_shared<CloseCurveCommand>(curve, context, m_old_point));

        m_edit = false;
    }

    BezierToolStrategy::set_finished(finished);
}

//////////////////////////////////////////////////////////////////////////
// GizmoMovePointStrategy
//////////////////////////////////////////////////////////////////////////

GizmoMovePointStrategy::GizmoMovePointStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
    set_finished(false);
}

bool GizmoMovePointStrategy::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                            ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto info = context->get_info();
    if ((!info.lmb_pressed && !info.mmb_pressed) || info.modifiers)
    {
        return true;
    }

    auto curve = context->get_curve();
    m_old_point = curve->get_point(info.select_curve_point_index);
    m_point_index = info.select_curve_point_index;

    m_manipulator.set_gizmo_matrix(GfMatrix4d().SetTranslate(m_old_point.point));
    m_manipulator.on_mouse_press(mouse_event, viewport_view, draw_manager);

    if (!m_manipulator.is_picked() && info.lmb_pressed && !info.mmb_pressed)
    {
        set_finished(true);
        context->set_after_event_callback(
            [context](const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) {
                context->update(mouse_event, viewport_view);
                const auto result = context->get_strategy()->on_mouse_press(mouse_event, viewport_view, draw_manager);
                context->reset_after_event_callback();
                return result;
            });
    }

    return true;
}

bool GizmoMovePointStrategy::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                           ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto& info = context->get_info();
    if (!info.lmb_pressed && !info.mmb_pressed)
    {
        return true;
    }

    m_manipulator.on_mouse_move(mouse_event, viewport_view, draw_manager);

    if (!m_manipulator.is_picked())
    {
        return true;
    }

    const auto delta = GfVec3f(m_manipulator.get_delta());
    auto point = m_old_point;
    point.ltangent += delta;
    point.point += delta;
    point.rtangent += delta;

    auto curve = context->get_curve();
    curve->set_point(m_point_index, point);

    m_edit = true;

    return true;
}

bool GizmoMovePointStrategy::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                              ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto info = context->get_info();

    m_manipulator.on_mouse_release(mouse_event, viewport_view, draw_manager);

    if (m_edit && !m_manipulator.is_picked())
    {
        auto curve = context->get_curve();
        CommandInterface::finalize(std::make_shared<ChangeCurvePointCommand>("GizmoMove", curve, context, m_point_index, m_old_point));
        m_edit = false;
    }

    return true;
}

void GizmoMovePointStrategy::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto info = context->get_info();
    if (info.select_curve_point_index != BezierCurve::s_invalid_index)
    {
        m_manipulator.draw(viewport_view, draw_manager);
    }

    auto curve = context->get_curve();

    if (!info.lmb_pressed)
    {
        curve->set_intersect_point(info.intersect_curve_point_index);
    }

    if (!info.lmb_pressed)
    {
        curve->set_intersect_tangent(info.intersect_curve_tangent_info);
    }

    BezierToolStrategy::draw(viewport_view, draw_manager);
}

ViewportMoveManipulator& GizmoMovePointStrategy::get_manipulator()
{
    return m_manipulator;
}

//////////////////////////////////////////////////////////////////////////
// GizmoScalePointStrategy
//////////////////////////////////////////////////////////////////////////

GizmoScalePointStrategy::GizmoScalePointStrategy(BezierToolContext* context)
    : BezierToolStrategy(context)
{
    set_finished(false);
}

bool GizmoScalePointStrategy::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                             ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto info = context->get_info();
    if ((!info.lmb_pressed && !info.mmb_pressed) || info.modifiers)
    {
        return true;
    }

    auto curve = context->get_curve();
    m_old_point = curve->get_point(info.select_curve_point_index);
    m_point_index = info.select_curve_point_index;

    m_manipulator.set_gizmo_data({ GfMatrix4d().SetScale(m_sgn_scale) * GfMatrix4d().SetTranslate(m_old_point.point), m_manipulator.get_delta() });
    m_manipulator.on_mouse_press(mouse_event, viewport_view, draw_manager);

    if (!m_manipulator.is_picked() && info.lmb_pressed && !info.mmb_pressed)
    {
        set_finished(true);
        context->set_after_event_callback(
            [context](const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) {
                context->update(mouse_event, viewport_view);
                const auto result = context->get_strategy()->on_mouse_press(mouse_event, viewport_view, draw_manager);
                context->reset_after_event_callback();
                return result;
            });
    }

    return true;
}

bool GizmoScalePointStrategy::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                            ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto& info = context->get_info();
    if (!info.lmb_pressed && !info.mmb_pressed)
    {
        return true;
    }

    m_manipulator.on_mouse_move(mouse_event, viewport_view, draw_manager);

    if (!m_manipulator.is_picked())
    {
        return true;
    }

    auto point = m_old_point;
    point.ltangent -= point.point;
    point.rtangent -= point.point;

    GfMatrix4f scale;
    scale.SetScale(GfVec3f(m_manipulator.get_delta()));

    auto new_ltangent = scale * GfVec4f(point.ltangent[0], point.ltangent[1], point.ltangent[2], 1.0f);
    auto new_rtangent = scale * GfVec4f(point.rtangent[0], point.rtangent[1], point.rtangent[2], 1.0f);

    point.ltangent = GfVec3f(new_ltangent[0], new_ltangent[1], new_ltangent[2]) / new_ltangent[3] + point.point;
    point.rtangent = GfVec3f(new_rtangent[0], new_rtangent[1], new_rtangent[2]) / new_rtangent[3] + point.point;

    auto curve = context->get_curve();
    curve->set_point(m_point_index, point);

    m_edit = true;

    return true;
}

bool GizmoScalePointStrategy::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                               ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto info = context->get_info();

    m_manipulator.on_mouse_release(mouse_event, viewport_view, draw_manager);

    if (m_edit && !m_manipulator.is_picked())
    {
        const auto delta = m_manipulator.get_delta();
        m_sgn_scale = GfCompMult(m_sgn_scale, GfVec3d(GfSgn(delta[0]), GfSgn(delta[1]), GfSgn(delta[2])));
        m_manipulator.set_gizmo_data({ GfMatrix4d().SetScale(m_sgn_scale) * GfMatrix4d().SetTranslate(m_old_point.point), delta });
        auto curve = context->get_curve();
        CommandInterface::finalize(std::make_shared<ChangeCurvePointCommand>("GizmoScale", curve, context, m_point_index, m_old_point));
        m_edit = false;
    }

    return true;
}

void GizmoScalePointStrategy::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    auto context = get_context();
    const auto info = context->get_info();
    if (info.select_curve_point_index != BezierCurve::s_invalid_index)
    {
        m_manipulator.draw(viewport_view, draw_manager);
    }

    auto curve = context->get_curve();

    if (!info.lmb_pressed)
    {
        curve->set_intersect_point(info.intersect_curve_point_index);
    }

    if (!info.lmb_pressed)
    {
        curve->set_intersect_tangent(info.intersect_curve_tangent_info);
    }

    BezierToolStrategy::draw(viewport_view, draw_manager);
}

ViewportScaleManipulator& GizmoScalePointStrategy::get_manipulator()
{
    return m_manipulator;
}

OPENDCC_NAMESPACE_CLOSE
