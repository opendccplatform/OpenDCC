/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

#include "opendcc/usd_editor/bezier_tool/bezier_curve.h"
#include "opendcc/usd_editor/bezier_tool/bezier_tool_strategy.h"
#include "opendcc/usd_editor/bezier_tool/event_filter.h"

#include "opendcc/base/commands_api/core/block.h"

#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/app/core/stage_watcher.h"
#include "opendcc/app/core/application.h"

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/plane.h>
#include <pxr/base/tf/staticTokens.h>

#include <memory>

namespace PXR_NS
{
    TF_DECLARE_PUBLIC_TOKENS(BezierToolTokens, ((name, "bezier_tool")));
};

OPENDCC_NAMESPACE_OPEN

class BezierToolContext final : public IViewportToolContext
{
public:
    enum class ManipMode
    {
        Translate,
        Scale,
    };

    struct Info
    {
        // user input
        bool modifiers = false;
        bool lmb_pressed = false; // lmb -   left mouse button
        bool mmb_pressed = false; // mmb - middle mouse button
        bool ctrl_modifier = false;
        bool shift_modifier = false;
        bool unsupported_modifiers = false;
        PXR_NS::GfRay pick_ray;

        // plane
        bool intersect_curve_plane = false;
        PXR_NS::GfVec3f intersect_curve_plane_point = PXR_NS::GfVec3f();

        // curve info
        bool select_last_point = false;
        size_t last_curve_point_index = 0;

        // curve point
        bool intersect_curve_point = false;
        size_t intersect_curve_point_index = BezierCurve::s_invalid_index;
        size_t select_curve_point_index = BezierCurve::s_invalid_index;

        // curve tangent
        bool intersect_curve_tangent = false;
        BezierCurve::Tangent intersect_curve_tangent_info = BezierCurve::Tangent();
        bool intersect_tangent_selected_point = false;
        BezierCurve::Tangent select_curve_tangent_info = BezierCurve::Tangent();
    };

    using MouseEvent =
        std::function<bool(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)>;

    BezierToolContext();
    ~BezierToolContext();

    bool on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override;
    bool on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override;
    bool on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override;

    bool on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override;
    bool on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override;

    void draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override;

    PXR_NS::TfToken get_name() const override;

    void set_tangent_mode(BezierCurve::TangentMode mode);
    BezierCurve::TangentMode get_tangent_mode();

    const Info& get_info() const;

    void set_select_curve_point_index(size_t index);
    void set_select_curve_tangent_info(const BezierCurve::Tangent& tangent);

    BezierCurvePtr get_curve();
    void reset_curve();

    void lock_commands();
    void unlock_commands();

    void update_point(const BezierCurve::Tangent& tangent, const PXR_NS::GfVec3f& new_tangent);
    void update_point(const BezierCurve::Tangent& tangent, const PXR_NS::GfVec3f& new_tangent, BezierCurve::TangentMode mode);
    BezierCurve::TangentMode correct_mode(size_t point_index, BezierCurve::TangentMode mode);

    ManipMode get_manip_mode() const;
    void set_manip_mode(ManipMode mode);

    void set_after_event_callback(const MouseEvent& callback);
    void reset_after_event_callback();

    void update(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view);

    BezierToolStrategyPtr get_strategy();

private:
    bool intersect_curve_plane(const PXR_NS::GfRay& ray, PXR_NS::GfVec3f* intersect_point = nullptr);

    void init_stage_watcher();

    void update_info(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view);
    void update_strategy(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view);

    BezierCurvePtr m_curve;
    BezierToolStrategyPtr m_strategy;

    std::shared_ptr<base::commands::UndoCommandBlock> m_command_block;
    std::unique_ptr<StageObjectChangedWatcher> m_stage_watcher;
    Application::CallbackHandle m_stage_changed_callback;
    BezierCurve::TangentMode m_tangent_mode = BezierCurve::TangentMode::Normal;
    EventFilter m_event_filter;
    Info m_info;
    ManipMode m_manip_mode;
    MouseEvent m_after_event_callback;
};

OPENDCC_NAMESPACE_CLOSE
