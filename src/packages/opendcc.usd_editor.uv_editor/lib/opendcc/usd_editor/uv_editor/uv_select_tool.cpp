// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/uv_editor/uv_select_tool.h"
#include "opendcc/usd_editor/uv_editor/uv_editor_gl_widget.h"
#include "opendcc/usd_editor/uv_editor/utils.h"

#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/ui/common_widgets/round_marking_menu.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/core/application.h"

#include <pxr/base/gf/vec4f.h>

#include <QActionGroup>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandInterface;
using base::commands::CommandArgs;

static const int min_rect_size = 2;
static const auto rect_color = GfVec4f(1.0f, 1.0f, 1.0f, 1.0f);
static const auto rect_paint_style = ViewportUiDrawManager::PaintStyle::STIPPLED;
static const auto rect_prim_type = ViewportUiDrawManager::PrimitiveTypeLinesStrip;

static bool degeneracy_rect(const int x0, const int y0, const int x1, const int y1)
{
    return std::abs(x0 - x1) <= min_rect_size || std::abs(y0 - y1) <= min_rect_size;
}

static SelectionList::SelectionMask convert_to_selection_mask(Application::SelectionMode selection_mode)
{
    using SelectionMode = Application::SelectionMode;
    using SelectionFlags = SelectionList::SelectionFlags;
    switch (selection_mode)
    {
    case SelectionMode::POINTS:
    case SelectionMode::UV:
        return SelectionFlags::POINTS;
    case SelectionMode::EDGES:
        return SelectionFlags::EDGES;
    case SelectionMode::FACES:
        return SelectionFlags::ELEMENTS;
    default:
        return SelectionFlags::NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
// UvSelectTool
//////////////////////////////////////////////////////////////////////////

UvSelectTool::UvSelectTool(UVEditorGLWidget* w)
    : UvTool(w)
{
    auto widget = get_widget();

    m_selection_mode_action_group = new QActionGroup(widget);

    m_selection_mode_menu = new QMenu(widget);

    const std::unordered_map<Application::SelectionMode, QAction*> modes = {
        { Application::SelectionMode::POINTS, new QAction(i18n("uveditor.round_marking_menu", "Point")) },
        { Application::SelectionMode::EDGES, new QAction(i18n("uveditor.round_marking_menu", "Edge")) },
        { Application::SelectionMode::FACES, new QAction(i18n("uveditor.round_marking_menu", "Face")) },
        { Application::SelectionMode::UV, new QAction(i18n("uveditor.round_marking_menu", "UV")) },
    };

    for (const auto mode_pair : modes)
    {
        const auto& mode = mode_pair.first;
        const auto& action = mode_pair.second;

        action->setCheckable(true);
        action->setData(static_cast<uint8_t>(mode));

        if (Application::instance().get_selection_mode() == mode)
        {
            action->setChecked(true);
        }

        m_selection_mode_action_group->addAction(action);
        m_selection_mode_menu->addAction(action);
    }

    QObject::connect(m_selection_mode_action_group, &QActionGroup::triggered, [this](QAction* action) {
        const auto data = action->data();
        Application::instance().set_selection_mode(static_cast<Application::SelectionMode>(action->data().toUInt()));
    });

    m_selection_mode_changed_cid = Application::instance().register_event_callback(Application::EventType::SELECTION_MODE_CHANGED, [this, modes] {
        for (const auto mode : modes)
        {
            mode.second->setChecked(false);
        }

        if (working_application_selection_mode())
        {
            const auto mode = Application::instance().get_selection_mode();
            modes.at(mode)->setChecked(true);
        }
    });

    const auto settings = Application::instance().get_settings();
    const auto CV_data = settings->get("soft_selection.falloff_curve", std::vector<double>());
    using CurveRamp = Ramp<float>;
    m_falloff_curve_ramp = std::make_shared<CurveRamp>();
    for (int i = 0; i < CV_data.size(); i += 3)
    {
        int intep_type = CV_data[i + 2];
        m_falloff_curve_ramp->add_point(CV_data[i], static_cast<float>(CV_data[i + 1]), (CurveRamp::InterpType)(intep_type));
    }
    if (m_falloff_curve_ramp->cv().size() == 2)
    {
        m_falloff_curve_ramp->add_point(0, 1, CurveRamp::InterpType::kSmooth);
        m_falloff_curve_ramp->add_point(1, 0, CurveRamp::InterpType::kSmooth);
    }
    m_falloff_curve_ramp->prepare_points();

    const auto color_data = settings->get("soft_selection.falloff_color", std::vector<double>());
    using ColorRamp = Ramp<GfVec3f>;
    m_falloff_color_ramp = std::make_shared<ColorRamp>();
    for (int i = 0; i < color_data.size(); i += 5)
    {
        int interp_type = color_data[i + 4];
        m_falloff_color_ramp->add_point(
            color_data[i],
            GfVec3f(static_cast<float>(color_data[i + 1]), static_cast<float>(color_data[i + 2]), static_cast<float>(color_data[i + 3])),
            static_cast<ColorRamp::InterpType>(interp_type));
    }
    if (m_falloff_color_ramp->cv().size() == 2)
    {
        m_falloff_color_ramp->add_point(0, GfVec3f(0, 0, 0), ColorRamp::InterpType::kLinear);
        m_falloff_color_ramp->add_point(0.5, GfVec3f(1, 0, 0), ColorRamp::InterpType::kLinear);
        m_falloff_color_ramp->add_point(1, GfVec3f(1, 1, 0), ColorRamp::InterpType::kLinear);
    }
    m_falloff_color_ramp->prepare_points();

    m_falloff_fn = [falloff_curve_map = m_falloff_curve_ramp](float dist) {
        const auto rad = Application::instance().get_settings()->get("soft_selection.falloff_radius", 5.0f);
        float t;
        if (GfIsClose(dist, 0.0, 0.00001) && GfIsClose(rad, 0.0, 0.00001))
            t = 0.0;
        else
            t = dist / rad;

        if (t > 1)
            return 0.0f;

        return falloff_curve_map->value_at(t);
    };
    m_falloff_color_fn = [falloff_color_ramp = m_falloff_color_ramp](float weight) {
        return falloff_color_ramp->value_at(weight);
    };
}

UvSelectTool::~UvSelectTool()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_MODE_CHANGED, m_selection_mode_changed_cid);
}

/* virtual */
bool UvSelectTool::on_mouse_press(QMouseEvent* event) /* override */
{
    const auto modifiers = event->modifiers();
    const auto shift = modifiers & Qt::ShiftModifier;
    const auto ctrl = modifiers & Qt::ControlModifier;

    const auto buttons = event->buttons();
    const auto left = buttons & Qt::LeftButton;
    const auto right = buttons & Qt::RightButton;

    if (left && shift)
    {
        m_mode = SelectionMode::Add;
    }
    else if (left && ctrl)
    {
        m_mode = SelectionMode::Remove;
    }
    else if (left)
    {
        m_mode = SelectionMode::Replace;
    }
    else if (right && shift)
    {
        m_selection_mode_marking_menu = new RoundMarkingMenu(event->screenPos().toPoint(), get_widget());
        m_selection_mode_marking_menu->set_extended_menu(m_selection_mode_menu);
        m_selection_mode_marking_menu->showFullScreen();

        return true;
    }
    else
    {
        m_mode = SelectionMode::None;
        return false;
    }

    const auto pos = event->pos();

    m_current_pos_x = m_start_pos_x = pos.x();
    m_current_pos_y = m_start_pos_y = pos.y();

    return true;
}

/* virtual */
bool UvSelectTool::on_mouse_move(QMouseEvent* event) /* override */
{
    if (m_selection_mode_marking_menu)
    {
        m_selection_mode_marking_menu->on_mouse_move(event->screenPos().toPoint());
        return true;
    }

    const auto pos = event->pos();

    m_current_pos_x = pos.x();
    m_current_pos_y = pos.y();

    return true;
}

/* virtual */
bool UvSelectTool::on_mouse_release(QMouseEvent* event) /* override */
{
    if (m_selection_mode_marking_menu)
    {
        if (auto action = m_selection_mode_marking_menu->get_hovered_action())
        {
            action->trigger();
        }
        m_selection_mode_marking_menu->deleteLater();
        m_selection_mode_marking_menu = nullptr;
        return true;
    }

    if (!is_working())
    {
        return false;
    }

    if (!working_application_selection_mode())
    {
        m_mode = SelectionMode::None;
        return false;
    }

    auto widget = get_widget();

    const auto pos = event->pos();

    SelectionList selection;
    auto& app = Application::instance();
    const auto app_selection_mode = app.get_selection_mode();
    auto selection_mask = convert_to_selection_mask(app_selection_mode);
    if (degeneracy_rect(m_start_pos_x, m_start_pos_y, m_current_pos_x, m_current_pos_y))
    {
        selection = widget->pick_single_prim(GfVec2f(m_start_pos_x, m_start_pos_y), selection_mask | SelectionList::SelectionFlags::FULL_SELECTION);
    }
    else
    {
        selection = widget->pick_multiple_prims(GfVec2f(m_start_pos_x, m_start_pos_y), GfVec2f(m_current_pos_x, m_current_pos_y),
                                                selection_mask | SelectionList::SelectionFlags::FULL_SELECTION);
    }

    const auto extract = selection.extract(selection_mask);

    if (app_selection_mode == Application::SelectionMode::UV)
    {
        widget->ignore_next_selection_changed();

        auto current = widget->get_uv_selection();
        switch (m_mode)
        {
        case SelectionMode::Add:
        {
            current.merge(extract);
            break;
        }
        case SelectionMode::Remove:
        {
            current.difference(extract);
            break;
        }
        case SelectionMode::Replace:
        {
            current = extract;
            break;
        }
        }
        widget->set_uv_selection(current);
    }

    selection = uv_to_mesh(extract, widget->get_prims_info());
    RichSelection rich_selection(m_falloff_fn, m_falloff_color_fn);
    if (Application::instance().is_soft_selection_enabled() && Application::instance().get_settings()->get("soft_selection.enable_color", true))
    {
        rich_selection.set_soft_selection(selection);
    }
    Application::instance().set_rich_selection(rich_selection);

    CommandInterface::execute("select", CommandArgs().arg(selection).kwarg(selection_mode_to_string(m_mode), true));

    m_mode = SelectionMode::None;

    return true;
}

/* virtual */
void UvSelectTool::draw(ViewportUiDrawManager* draw_manager)
{
    if (is_working() && !degeneracy_rect(m_start_pos_x, m_start_pos_y, m_current_pos_x, m_current_pos_y))
    {
        auto widget = get_widget();

        const auto pixel_ratio = widget->devicePixelRatio();

        const auto viewport_width = widget->width() * pixel_ratio;
        const auto viewport_height = widget->height() * pixel_ratio;

        const GfVec2f start = screen_to_clip(m_start_pos_x, m_start_pos_y, viewport_width, viewport_height);
        const GfVec2f end = screen_to_clip(m_current_pos_x, m_current_pos_y, viewport_width, viewport_height);

        draw_manager->begin_drawable();
        draw_manager->set_color(rect_color);
        draw_manager->set_paint_style(rect_paint_style);
        draw_manager->set_prim_type(rect_prim_type);
        draw_manager->rect2d(start, end);
        draw_manager->end_drawable();
    }
}

bool UvSelectTool::is_working() const
{
    return m_mode != SelectionMode::None;
}

/* static */
std::string UvSelectTool::selection_mode_to_string(const SelectionMode mode)
{
    switch (mode)
    {
    case SelectionMode::Add:
    {
        return "add";
    }
    case SelectionMode::Remove:
    {
        return "remove";
    }
    case SelectionMode::Replace:
    {
        return "replace";
    }
    default:
    {
        return "unknown";
    }
    }
}

OPENDCC_NAMESPACE_CLOSE
