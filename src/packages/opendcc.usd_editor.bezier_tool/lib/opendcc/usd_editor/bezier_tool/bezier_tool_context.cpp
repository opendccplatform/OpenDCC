// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/bezier_tool_context.h"
#include "opendcc/usd_editor/bezier_tool/utils.h"
#include "opendcc/usd_editor/bezier_tool/add_point_to_curve_command.h"
#include "opendcc/usd_editor/bezier_tool/remove_curve_point_command.h"

#include "opendcc/base/commands_api/core/command_interface.h"

#include "opendcc/app/core/undo/block.h"

#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/ui/application_ui.h"

#include <pxr/base/gf/matrix4f.h>

#include <QCoreApplication>

namespace PXR_NS
{
    TF_DEFINE_PUBLIC_TOKENS(BezierToolTokens, ((name, "bezier_tool")));
};

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommandBlock;
using base::commands::CommandInterface;

GfPlane s_curve_plane = GfPlane(GfVec3d(0.0f, 1.0f, 0.0f), GfVec3d(0.0f, 0.0f, 0.0f));

BezierToolContext::BezierToolContext()
{
    m_stage_changed_callback = Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this] {
        if (m_curve)
        {
            m_curve->clear();
        }
        m_info = Info();
    });

    m_strategy = std::make_shared<BezierToolNullStrategy>(this);

    QCoreApplication::instance()->installEventFilter(&m_event_filter);

    const auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
    {
        return;
    }

    const auto selection = Application::instance().get_prim_selection();
    for (const auto& select : selection)
    {
        const auto prim = stage->GetPrimAtPath(select);
        if (prim.IsA<UsdGeomBasisCurves>())
        {
            m_curve.reset(new BezierCurve(UsdGeomBasisCurves(prim)));
            return;
        }
    }
}

BezierToolContext::~BezierToolContext()
{
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_stage_changed_callback);

    QCoreApplication::instance()->removeEventFilter(&m_event_filter);
}

bool BezierToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                       ViewportUiDrawManager* draw_manager) /* override */
{
    const auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
    {
        return true;
    }

    if (!m_stage_watcher)
    {
        init_stage_watcher();
    }

    if (!m_curve)
    {
        m_curve.reset(new BezierCurve);
    }

    update(mouse_event, viewport_view);

    return m_strategy->on_mouse_press(mouse_event, viewport_view, draw_manager) &&
           (!m_after_event_callback || m_after_event_callback(mouse_event, viewport_view, draw_manager));
}

bool BezierToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                      ViewportUiDrawManager* draw_manager) /* override */
{
    const auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage || !m_curve)
    {
        return true;
    }

    update(mouse_event, viewport_view);

    return m_strategy->on_mouse_move(mouse_event, viewport_view, draw_manager) &&
           (!m_after_event_callback || m_after_event_callback(mouse_event, viewport_view, draw_manager));
}

bool BezierToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                         ViewportUiDrawManager* draw_manager) /* override */
{
    const auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage || !m_curve)
    {
        return true;
    }

    update(mouse_event, viewport_view);

    return m_strategy->on_mouse_release(mouse_event, viewport_view, draw_manager) &&
           (!m_after_event_callback || m_after_event_callback(mouse_event, viewport_view, draw_manager));
}

bool BezierToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) /* override */
{
    if (!m_curve || !key_event)
    {
        return true;
    }

    const auto key = static_cast<Qt::Key>(key_event->key());

    if (key == Qt::Key::Key_Return)
    {
        m_curve.reset();
        ApplicationUI::instance().set_current_viewport_tool(ViewportToolContextRegistry::create_tool_context(TfToken("USD"), TfToken("select_tool")));
    }
    else if (key == Qt::Key::Key_Delete && m_info.select_curve_point_index != BezierCurve::s_invalid_index)
    {
        UndoCommandBlock* block = nullptr;
        commands::UsdEditsUndoBlock* undo_block = nullptr;

        bool last = m_curve->size() == 1;

        if (last)
        {
            block = new UndoCommandBlock("RemoveCurveCommand");
            undo_block = new commands::UsdEditsUndoBlock;
        }

        const auto to_delete = m_info.select_curve_point_index;
        const auto point = m_curve->get_point(to_delete);
        bool close = m_curve->is_close();
        m_curve->remove_point(to_delete);
        const auto size = m_curve->size();
        set_select_curve_point_index(size ? qBound(size_t(0), to_delete, size - 1) : BezierCurve::s_invalid_index);
        CommandInterface::finalize(std::make_shared<RemoveCurvePointCommand>(m_curve, this, to_delete, point, close != m_curve->is_close()));

        if (last)
        {
            const auto stage = Application::instance().get_session()->get_current_stage();
            stage->RemovePrim(m_curve->get_path());
            delete undo_block;
            delete block;
        }
    }

    return true;
}

bool BezierToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) /* override */
{
    return true;
}

void BezierToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) /* override */
{
    if (!m_curve || m_curve->is_empty())
    {
        return;
    }

    m_strategy->draw(viewport_view, draw_manager);
}

PXR_NS::TfToken BezierToolContext::get_name() const /* override */
{
    return BezierToolTokens->name;
}

void BezierToolContext::set_tangent_mode(BezierCurve::TangentMode mode)
{
    m_tangent_mode = mode;
}

BezierCurve::TangentMode BezierToolContext::get_tangent_mode()
{
    return m_tangent_mode;
}

const BezierToolContext::Info& BezierToolContext::get_info() const
{
    return m_info;
}

void BezierToolContext::set_select_curve_point_index(size_t index)
{
    m_info.select_curve_point_index = index;
}

void BezierToolContext::set_select_curve_tangent_info(const BezierCurve::Tangent& tangent)
{
    m_info.select_curve_tangent_info = tangent;
}

BezierCurvePtr BezierToolContext::get_curve()
{
    return m_curve;
}

void BezierToolContext::reset_curve()
{
    m_curve.reset(new BezierCurve);
}

void BezierToolContext::lock_commands()
{
    m_command_block.reset(new UndoCommandBlock("CreateBezierCurve"));
}

void BezierToolContext::unlock_commands()
{
    m_command_block.reset();
}

void BezierToolContext::update_point(const BezierCurve::Tangent& tangent, const PXR_NS::GfVec3f& new_tangent)
{
    update_point(tangent, new_tangent, m_tangent_mode);
}

/*static */
void BezierToolContext::update_point(const BezierCurve::Tangent& tangent, const PXR_NS::GfVec3f& new_tangent, BezierCurve::TangentMode mode)
{
    m_curve->update_point(tangent, new_tangent, mode);
}

BezierCurve::TangentMode BezierToolContext::correct_mode(size_t point_index, BezierCurve::TangentMode mode)
{
    switch (mode)
    {
    case OPENDCC_NAMESPACE::BezierCurve::TangentMode::Normal:
    case OPENDCC_NAMESPACE::BezierCurve::TangentMode::Weighted:
    {
        const auto point = m_curve->get_point(point_index);

        if (!lie_on_one_line(point.ltangent, point.point, point.rtangent))
        {
            return OPENDCC_NAMESPACE::BezierCurve::TangentMode::Tangent;
        }
        else if (!GfIsClose((point.ltangent - point.point).GetLength(), (point.rtangent - point.point).GetLength(), epsilon))
        {
            return OPENDCC_NAMESPACE::BezierCurve::TangentMode::Weighted;
        }
        else
        {
            return mode;
        }
    }
    case OPENDCC_NAMESPACE::BezierCurve::TangentMode::Tangent:
    default:
    {
        return mode;
    }
    }
}

BezierToolContext::ManipMode BezierToolContext::get_manip_mode() const
{
    return m_manip_mode;
}

void BezierToolContext::set_manip_mode(ManipMode mode)
{
    m_manip_mode = mode;

    if (std::dynamic_pointer_cast<GizmoMovePointStrategy>(m_strategy) && m_manip_mode == ManipMode::Scale)
    {
        auto strategy = std::make_shared<GizmoScalePointStrategy>(this);
        const auto point = m_curve->get_point(m_info.select_curve_point_index).point;
        strategy->get_manipulator().set_gizmo_data({ GfMatrix4d().SetTranslate(point), GfVec3d(1) });
        m_strategy = strategy;
        ViewportWidget::update_all_gl_widget();
    }
    else if (std::dynamic_pointer_cast<GizmoScalePointStrategy>(m_strategy) && m_manip_mode == ManipMode::Translate)
    {
        auto strategy = std::make_shared<GizmoMovePointStrategy>(this);
        const auto point = m_curve->get_point(m_info.select_curve_point_index).point;
        strategy->get_manipulator().set_gizmo_matrix(GfMatrix4d().SetTranslate(point));
        m_strategy = strategy;
        ViewportWidget::update_all_gl_widget();
    }
}

void BezierToolContext::set_after_event_callback(const MouseEvent& callback)
{
    m_after_event_callback = callback;
}

void BezierToolContext::reset_after_event_callback()
{
    m_after_event_callback = MouseEvent();
}

bool BezierToolContext::intersect_curve_plane(const GfRay& ray, GfVec3f* intersect_point)
{
    double distance = 0.0f;
    const bool intersect = ray.Intersect(s_curve_plane, &distance);
    if (intersect && intersect_point)
    {
        *intersect_point = GfVec3f(ray.GetPoint(distance));
    }

    return intersect;
}

void BezierToolContext::init_stage_watcher()
{
    const auto stage = Application::instance().get_session()->get_current_stage();

    m_stage_watcher = std::make_unique<StageObjectChangedWatcher>(stage, [this, stage](PXR_NS::UsdNotice::ObjectsChanged const& notice) {
        if (!m_curve)
        {
            return;
        }

        for (auto& path : notice.GetResyncedPaths())
        {
            if (path.IsPrimPath() && path == m_curve->get_path())
            {
                if (!stage->GetPrimAtPath(path))
                {
                    m_curve->clear();
                }
            }
        }
    });
}

void BezierToolContext::update(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    update_info(mouse_event, viewport_view);
    update_strategy(mouse_event, viewport_view);
}

BezierToolStrategyPtr BezierToolContext::get_strategy()
{
    return m_strategy;
}

void BezierToolContext::update_info(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    // user input
    const auto modifiers = mouse_event.modifiers();
    const auto buttons = mouse_event.buttons();
    m_info.modifiers = modifiers;
    m_info.lmb_pressed = buttons & Qt::MouseButton::LeftButton;
    m_info.mmb_pressed = buttons & Qt::MouseButton::MiddleButton;
    m_info.ctrl_modifier = modifiers & Qt::ControlModifier;
    m_info.shift_modifier = modifiers & Qt::ShiftModifier;
    m_info.unsupported_modifiers = (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) != modifiers;
    m_info.pick_ray = manipulator_utils::compute_pick_ray(viewport_view, mouse_event.x(), mouse_event.y());

    // plane
    m_info.intersect_curve_plane = intersect_curve_plane(m_info.pick_ray, &m_info.intersect_curve_plane_point);

    // curve info
    m_info.last_curve_point_index = m_curve->size() - 1;
    m_info.select_last_point = m_info.select_curve_point_index == m_info.last_curve_point_index;

    // curve point
    m_info.intersect_curve_point = m_curve->intersect_curve_point(m_info.pick_ray, viewport_view, &m_info.intersect_curve_point_index);

    // curve tangent
    m_info.intersect_curve_tangent = m_curve->intersect_curve_tangent(m_info.pick_ray, viewport_view, &m_info.intersect_curve_tangent_info);
    m_info.intersect_tangent_selected_point = m_info.intersect_curve_tangent_info.point_index == m_info.select_curve_point_index;
    if (m_strategy->is_finished())
    {
        m_info.select_curve_tangent_info = BezierCurve::Tangent();
    }
}

void BezierToolContext::update_strategy(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view)
{
    // TODO! This feature is not implemented because it is not possible to select a curve when the refine level is greater than 0.
    // const auto screen = GfVec2f(mouse_event.x(), mouse_event.y());
    // const auto selection = viewport_view->pick_single_prim(screen, SelectionList::FULL_SELECTION);

    if (!m_strategy->is_finished())
    {
        return;
    }
    else if (m_info.unsupported_modifiers)
    {
        m_strategy = std::dynamic_pointer_cast<BezierToolNullStrategy>(m_strategy) ? m_strategy : std::make_shared<BezierToolNullStrategy>(this);
    }
    else if (!get_curve())
    {
        m_strategy = std::dynamic_pointer_cast<BezierToolNullStrategy>(m_strategy) ? m_strategy : std::make_shared<BezierToolNullStrategy>(this);
    }
    else if (m_info.mmb_pressed && m_manip_mode == ManipMode::Translate && m_info.select_curve_point_index != BezierCurve::s_invalid_index)
    {
        m_strategy = std::dynamic_pointer_cast<GizmoMovePointStrategy>(m_strategy) ? m_strategy : std::make_shared<GizmoMovePointStrategy>(this);
    }
    else if (m_info.mmb_pressed && m_manip_mode == ManipMode::Scale && m_info.select_curve_point_index != BezierCurve::s_invalid_index)
    {
        m_strategy = std::dynamic_pointer_cast<GizmoScalePointStrategy>(m_strategy) ? m_strategy : std::make_shared<GizmoScalePointStrategy>(this);
    }
    else if (m_info.ctrl_modifier && m_info.lmb_pressed && m_info.shift_modifier && m_info.intersect_curve_point)
    {
        m_strategy = std::dynamic_pointer_cast<CloseCurveStrategy>(m_strategy) ? m_strategy : std::make_shared<CloseCurveStrategy>(this);
    }
    else if (m_info.ctrl_modifier && m_info.lmb_pressed && m_info.intersect_curve_point)
    {
        m_strategy = std::dynamic_pointer_cast<ResetTangentsStrategy>(m_strategy) ? m_strategy : std::make_shared<ResetTangentsStrategy>(this);
    }
    else if (m_info.lmb_pressed && m_info.intersect_curve_point)
    {
        m_strategy = std::dynamic_pointer_cast<MovePointStrategy>(m_strategy) ? m_strategy : std::make_shared<MovePointStrategy>(this);
    }
    else if (m_info.lmb_pressed && m_info.intersect_curve_tangent)
    {
        m_strategy = std::dynamic_pointer_cast<EditTangentStrategy>(m_strategy) ? m_strategy : std::make_shared<EditTangentStrategy>(this);
    }
    // else if (!selection[m_curve->get_path()].empty())
    // {
    //     m_strategy = std::dynamic_pointer_cast<BezierToolNullStrategy>(m_strategy) ? m_strategy : std::make_shared<BezierToolNullStrategy>(this);
    // }
    else if (m_info.intersect_curve_plane && !m_curve->is_close())
    {
        m_strategy = std::dynamic_pointer_cast<AddPointStrategy>(m_strategy) ? m_strategy : std::make_shared<AddPointStrategy>(this);
    }
    else
    {
        m_strategy = std::dynamic_pointer_cast<BezierToolNullStrategy>(m_strategy) ? m_strategy : std::make_shared<BezierToolNullStrategy>(this);
    }
}

OPENDCC_NAMESPACE_CLOSE
