// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

// workaround
#include "opendcc/base/qt_python.h"

#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/core/half_edge_cache.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/base/commands_api/core/block.h"

#include <QMouseEvent>
#include <QAction>
#include "opendcc/app/viewport/viewport_widget.h"
#include "viewport_select_tool_settings_widget.h"
#include "opendcc/ui/common_widgets/round_marking_menu.h"
#include <QMenu>
#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/core/rich_selection.h"
#include "pxr/base/gf/camera.h"
#include "pxr/base/gf/frustum.h"
#include "pxr/imaging/cameraUtil/conformWindow.h"
#include "pxr/usd/usdGeom/pointBased.h"
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usdGeom/mesh.h>

namespace PXR_NS
{
    TF_DEFINE_PUBLIC_TOKENS(SelectToolTokens, ((name, "select_tool")));
};

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandInterface;
using base::commands::CommandArgs;

static SdfPath get_enclosing_model_path(const SdfPath& current, const TfToken& base_kind)
{
    auto cur_prim = Application::instance().get_session()->get_current_stage()->GetPrimAtPath(current);
    while (cur_prim)
    {
        // We use Kind here instead of prim.IsModel because point instancer
        // prototypes currently don't register as models in IsModel. See
        // bug: http://bugzilla.pixar.com/show_bug.cgi?id=117137
        if (auto model_api = UsdModelAPI(cur_prim))
        {
            TfToken kind;
            model_api.GetKind(&kind);
            if (KindRegistry::IsA(kind, base_kind))
                return cur_prim.GetPath();
        }
        cur_prim = cur_prim.GetParent();
    }
    return cur_prim.GetPath();
}

static GfFrustum get_conformed_frustum(const ViewportViewPtr& viewport_view)
{
    auto result = viewport_view->get_camera().GetFrustum();
    const auto viewport_dim = viewport_view->get_viewport_dimensions();
    CameraUtilConformWindow(&result, CameraUtilConformWindowPolicy::CameraUtilFit,
                            viewport_dim.height != 0 ? (double)viewport_dim.width / viewport_dim.height : 1.0);
    return result;
}

static GfVec3d get_world_pos(const ViewportViewPtr& viewport_view, const GfVec3d plane_origin, int x)
{
    const auto viewport_dim = viewport_view->get_viewport_dimensions();
    const auto frustum = get_conformed_frustum(viewport_view);
    auto ray = frustum.ComputeRay(GfVec2d((double)x / viewport_dim.width * 2.0 - 1, 0));
    double dist;
    ray.Intersect(GfPlane(frustum.ComputeViewDirection(), plane_origin), &dist);
    return ray.GetPoint(dist);
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
    case SelectionMode::PRIMS:
        return SelectionFlags::FULL_SELECTION;
    case SelectionMode::INSTANCES:
        return SelectionFlags::INSTANCES;
    default:
        return SelectionFlags::ALL;
    }
}

//////////////////////////////////////////////////////////////////////////
// ViewportSelectToolContext
//////////////////////////////////////////////////////////////////////////

bool ViewportSelectToolContext::s_factory_registration =
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), SelectToolTokens->name, []() { return new ViewportSelectToolContext; });

ViewportSelectToolContext::ViewportSelectToolContext()
    : m_select_rect_mode(false)
    , m_mousex(0)
    , m_mousey(0)
    , m_start_posx(0)
    , m_start_posy(0)
    , m_shift(false)
{
    using SelectionMode = Application::SelectionMode;

    const std::unordered_map<SelectionMode, QAction*> selection_modes = {
        { SelectionMode::POINTS, new QAction(i18n("tool_settings.viewport.select_tool.selection_modes", "Point")) },
        { SelectionMode::EDGES, new QAction(i18n("tool_settings.viewport.select_tool.selection_modes", "Edge")) },
        { SelectionMode::FACES, new QAction(i18n("tool_settings.viewport.select_tool.selection_modes", "Face")) },
        { SelectionMode::UV, new QAction(i18n("tool_settings.viewport.select_tool.selection_modes", "UV")) },
        { SelectionMode::INSTANCES, new QAction(i18n("tool_settings.viewport.select_tool.selection_modes", "Instance")) },
        { SelectionMode::PRIMS, new QAction(i18n("tool_settings.viewport.select_tool.selection_modes", "Prim")) },
    };

    m_selection_mode_action_group = new QActionGroup(this);
    m_selection_mode_action_group->setExclusive(true);
    m_selection_mode_menu = new QMenu(ApplicationUI::instance().get_active_view());
    auto add_action = [&selection_modes, this](SelectionMode mode, QMenu* menu) {
        auto action = selection_modes.at(mode);
        action->setCheckable(true);
        action->setData(static_cast<uint8_t>(mode));
        if (Application::instance().get_selection_mode() == mode)
        {
            action->setChecked(true);
        }
        m_selection_mode_action_group->addAction(action);
        menu->addAction(action);
    };
    add_action(SelectionMode::POINTS, m_selection_mode_menu);
    add_action(SelectionMode::EDGES, m_selection_mode_menu);
    add_action(SelectionMode::FACES, m_selection_mode_menu);
    add_action(SelectionMode::UV, m_selection_mode_menu);
    add_action(SelectionMode::INSTANCES, m_selection_mode_menu);
    add_action(SelectionMode::PRIMS, m_selection_mode_menu);

    auto add_kind_action = [&selection_modes, this](TfToken token, QMenu* menu) {
        auto action = new QAction(token.data());
        action->setCheckable(true);

        if (m_selection_kind == token)
        {
            action->setChecked(true);
        }
        m_selection_mode_action_group->addAction(action);
        menu->addAction(action);
    };

    auto selection_kinds = new QMenu("Models", ApplicationUI::instance().get_active_view());
    auto model_kinds_action = new QAction("Models");
    m_selection_mode_menu->addAction(model_kinds_action);

    std::vector<TfToken> default_kinds = {
        KindTokens->model, KindTokens->group, KindTokens->assembly, KindTokens->component, KindTokens->subcomponent,
    };

    for (const auto& kind : default_kinds)
    {
        add_kind_action(kind, selection_kinds);
    }

    auto all_kinds = KindRegistry::GetAllKinds();

    std::vector<TfToken> custom_kinds;

    std::sort(default_kinds.begin(), default_kinds.end());
    std::sort(all_kinds.begin(), all_kinds.end());

    std::set_difference(all_kinds.begin(), all_kinds.end(), default_kinds.begin(), default_kinds.end(),
                        std::inserter(custom_kinds, custom_kinds.begin()));

    for (const auto& kind : custom_kinds)
    {
        add_kind_action(kind, selection_kinds);
    }

    model_kinds_action->setMenu(selection_kinds);

    const auto is_custom_kind = [](const QVariant& variant) -> bool {
        return variant.isNull();
    };

    connect(m_selection_mode_action_group, &QActionGroup::triggered, this, [this, is_custom_kind](QAction* action) {
        if (!m_marking_menu_selection.empty())
        {
            auto prims = Application::instance().get_highlighted_prims();
            prims.push_back(m_marking_menu_selection.get_selected_paths()[0]);
            Application::instance().set_highlighted_prims(prims);
            m_marking_menu_selection = SelectionList();
        }

        const auto data = action->data();
        const auto is_custom = is_custom_kind(data);

        if (is_custom)
        {
            // TODO: it's not pretty that we use action label as value, we could improve this in the future e.g by storing std::pair in QAction::data
            set_selection_kind(TfToken(action->text().toStdString()));
            Application::instance().set_selection_mode(SelectionMode::PRIMS);
        }
        else
        {
            set_selection_kind(TfToken());
            Application::instance().set_selection_mode(static_cast<SelectionMode>(action->data().toUInt()));
        }
    });

    auto selected_mode_changed = [selection_modes, this] {
        if (m_selection_mode_marking_menu)
        {
            return;
        }

        const auto mode = Application::instance().get_selection_mode();

        // if selecting kind
        if (mode == SelectionMode::PRIMS && !m_selection_kind.IsEmpty())
        {
            for (const auto& mode : selection_modes)
            {
                mode.second->setChecked(false);
            }
            for (const auto& action : m_selection_mode_action_group->actions())
            {
                if (action->text().toStdString() == m_selection_kind)
                {
                    action->setChecked(true);
                    break;
                }
            }
        }
        else
        {
            set_selection_kind(TfToken());
            selection_modes.at(mode)->setChecked(true);
        }
    };
    selected_mode_changed();

    m_selection_mode_changed_cid =
        Application::instance().register_event_callback(Application::EventType::SELECTION_MODE_CHANGED, selected_mode_changed);

    const auto settings = Application::instance().get_settings();
    m_selection_kind_changed = settings->register_setting_changed("session.viewport.select_tool.kind",
                                                                  [this](const std::string&, const Settings::Value& val, Settings::ChangeType) {
                                                                      const auto str = val.get<std::string>();

                                                                      if (str != m_selection_kind)
                                                                      {
                                                                          m_selection_kind = PXR_NS::TfToken(str);
                                                                      }
                                                                  });

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
    auto new_selection = RichSelection(m_falloff_fn, m_falloff_color_fn);
    new_selection.set_soft_selection(Application::instance().get_rich_selection().get_selection_list());
    Application::instance().set_rich_selection(new_selection);
}

ViewportSelectToolContext::~ViewportSelectToolContext()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_MODE_CHANGED, m_selection_mode_changed_cid);
    Application::instance().get_settings()->unregister_setting_changed("session.viewport.select_tool.kind", m_selection_kind_changed);
}

bool ViewportSelectToolContext::on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                               ViewportUiDrawManager* draw_manager)
{
    Qt::MouseButton button = mouse_event.button();
    m_shift = mouse_event.modifiers() & Qt::ShiftModifier;
    m_ctrl = mouse_event.modifiers() & Qt::ControlModifier;

    m_start_posx = mouse_event.x();
    m_start_posy = mouse_event.y();
    m_mousex = mouse_event.x();
    m_mousey = mouse_event.y();
    if (button == Qt::LeftButton)
    {
        if (m_adjust_soft_selection_radius && viewport_view->get_scene_context_type() == TfToken("USD"))
        {
            m_draw_soft_selection_radius = true;
            m_start_falloff_radius = Application::instance().get_settings()->get("soft_selection.falloff_radius", 5.0f);
            m_cur_falloff_radius = m_start_falloff_radius;
            size_t vert_count = 0;
            m_centroid = std::make_unique<GfVec3f>();
            for (const auto& prim_sel : Application::instance().get_selection())
            {
                auto prim = Application::instance().get_session()->get_current_stage()->GetPrimAtPath(prim_sel.first);
                auto point_based = UsdGeomPointBased(prim);
                if (!point_based)
                    continue;

                auto world_transform = point_based.ComputeLocalToWorldTransform(Application::instance().get_current_time());
                VtVec3fArray points;
                point_based.GetPointsAttr().Get<VtVec3fArray>(&points, Application::instance().get_current_time());
                for (const auto& ind : prim_sel.second.get_point_indices())
                {
                    *m_centroid += GfVec3f(world_transform.Transform(points[ind]));
                    vert_count++;
                }
            }
            if (vert_count != 0)
            {
                *m_centroid /= vert_count;
                m_start_world_pos = get_world_pos(viewport_view, *m_centroid, m_start_posx);
            }
            else
            {
                m_centroid = nullptr;
            }
            m_key_press_timepoint = static_cast<unsigned long long>(-1);
        }
        else
        {
            m_select_rect_mode = true;
        }
        return true;
    }
    else if (m_shift && button == Qt::RightButton)
    {
        const auto selection = viewport_view->pick_single_prim(GfVec2f(m_start_posx, m_start_posy), SelectionList::SelectionFlags::FULL_SELECTION);
        if (selection.empty() && Application::instance().get_selection().empty() && Application::instance().get_highlighted_prims().empty())
            return true;

        m_marking_menu_selection = selection;
        m_selection_mode_marking_menu = new RoundMarkingMenu(mouse_event.global_pos(), ApplicationUI::instance().get_active_view());
        m_selection_mode_marking_menu->set_extended_menu(m_selection_mode_menu);
        m_selection_mode_marking_menu->showFullScreen();
        if (auto gl_widget = ApplicationUI::instance().get_active_view()->get_gl_widget())
            gl_widget->activateWindow();
        return true;
    }
    return false;
}

bool ViewportSelectToolContext::on_mouse_double_click(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                                      ViewportUiDrawManager* draw_manager)
{
    const auto button = mouse_event.button();
    const auto modifiers = mouse_event.modifiers();
    m_shift = mouse_event.modifiers() & Qt::ShiftModifier;
    m_ctrl = mouse_event.modifiers() & Qt::ControlModifier;
    m_mousex = mouse_event.x();
    m_mousey = mouse_event.y();
    m_double_click = true;
    //

    if (m_last_selection.empty())
    {
        return false;
    }

    const auto paths = m_last_selection.get_selected_paths();
    if (paths.size() != 1)
    {
        return false;
    }

    const auto path = paths[0];
    const auto data = m_last_selection[path];
    const auto indices = data.get_edge_indices();

    auto& application = Application::instance();
    const auto selection_mode = application.get_selection_mode();
    if (selection_mode == Application::SelectionMode::EDGES && indices.size() == 1)
    {
        return edge_loop_selection();
    }
    else
    {
        return topology_selection();
    }
}

bool ViewportSelectToolContext::on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                              ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;

    if (m_selection_mode_marking_menu)
    {
        m_selection_mode_marking_menu->on_mouse_move(mouse_event.global_pos());
        return true;
    }

    m_mousex = mouse_event.x();
    m_mousey = mouse_event.y();

    if (m_draw_soft_selection_radius && viewport_view->get_scene_context_type() == TfToken("USD"))
    {
        const auto viewport_dim = viewport_view->get_viewport_dimensions();
        if (m_centroid)
        {
            auto new_world_pos = get_world_pos(viewport_view, *m_centroid, m_mousex);
            m_cur_falloff_radius = m_start_falloff_radius + GfSgn(m_mousex - m_start_posx) * (new_world_pos - m_start_world_pos).GetLength();
            Application::instance().get_settings()->set("soft_selection.falloff_radius", std::max(m_cur_falloff_radius, 0.0f));
        }
        else
        {
            m_cur_falloff_radius = m_start_falloff_radius + static_cast<float>(m_mousex - m_start_posx) / viewport_dim.width * 4.6;
            Application::instance().get_settings()->set(
                "soft_selection.falloff_radius",
                std::max(m_start_falloff_radius + static_cast<float>(m_mousex - m_start_posx) / viewport_view->get_viewport_dimensions().width * 4.6f,
                         0.0f));
        }
        if (m_cur_falloff_radius < 0)
            m_cur_falloff_radius = 0;
    }

    return false;
}

static void update_rich_selection(SelectionList& list, const std::function<float(float)>& falloff_fn,
                                  const std::function<PXR_NS::GfVec3f(float)>& falloff_color_fn)
{
    RichSelection rich_selection(falloff_fn, falloff_color_fn);
    if (Application::instance().is_soft_selection_enabled() && Application::instance().get_settings()->get("soft_selection.enable_color", true))
    {
        rich_selection.set_soft_selection(list);
    }
    Application::instance().set_rich_selection(rich_selection);
};

bool ViewportSelectToolContext::on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view,
                                                 ViewportUiDrawManager* draw_manager)
{
    if (!viewport_view)
        return false;

    if (m_select_rect_mode && !m_double_click)
    {
        m_mousex = mouse_event.x();
        m_mousey = mouse_event.y();
        m_select_rect_mode = false;
        SelectionList target_selection;

        const auto app_selection_mode = Application::instance().get_selection_mode();
        auto selection_mask = convert_to_selection_mask(app_selection_mode);

        if (std::abs(m_mousex - m_start_posx) > 2 && std::abs(m_mousey - m_start_posy) > 2)
        {
            target_selection = viewport_view->pick_multiple_prims(GfVec2f(m_start_posx, m_start_posy), GfVec2f(m_mousex, m_mousey),
                                                                  selection_mask | SelectionList::SelectionFlags::FULL_SELECTION);
        }
        else
        {
            target_selection =
                viewport_view->pick_single_prim(GfVec2f(m_start_posx, m_start_posy), selection_mask | SelectionList::SelectionFlags::FULL_SELECTION);
        }

        if (!m_selection_kind.IsEmpty())
        {
            SelectionList selected_models;
            for (const auto& entry : target_selection)
            {
                auto path = get_enclosing_model_path(entry.first, m_selection_kind);

                if (!path.IsEmpty())
                {
                    selected_models.set_full_selection(path, true);
                }
            }
            target_selection = selected_models;
        }
        m_last_selection = target_selection;
        target_selection = target_selection.extract(selection_mask);

        auto is_components_edited = [](const SelectionList& list, const SdfPathVector& selected_paths) {
            return !list.empty() || std::any_of(selected_paths.begin(), selected_paths.end(), [](const SdfPath& p) {
                const auto& prims = Application::instance().get_highlighted_prims();
                return std::find(prims.begin(), prims.end(), p) != prims.end();
            });
        };
        if (m_shift)
        {
            auto merged_selection = Application::instance().get_selection();
            ;
            auto final_selection = target_selection.extract(Application::instance().get_highlighted_prims(), selection_mask);
            if (is_components_edited(final_selection, target_selection.get_selected_paths()))
            {
                update_rich_selection(merged_selection, m_falloff_fn, m_falloff_color_fn);
                merged_selection.merge(final_selection);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("add", true));
                viewport_view->set_selected(merged_selection, Application::instance().get_rich_selection());
            }
            else if (selection_mask == SelectionList::INSTANCES)
            {
                final_selection = target_selection.extract(selection_mask);
                merged_selection.merge(final_selection, SelectionList::INSTANCES);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("add", true));
                viewport_view->set_selected(merged_selection, RichSelection());
            }
            else
            {
                final_selection = target_selection.extract(SelectionList::FULL_SELECTION);
                merged_selection.merge(final_selection);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("add", true));
                viewport_view->set_selected(merged_selection, Application::instance().get_rich_selection());
            }
        }
        else if (m_ctrl)
        {
            auto diff_selection = Application::instance().get_selection();
            auto final_selection = target_selection.extract(Application::instance().get_highlighted_prims(), selection_mask);
            if (is_components_edited(final_selection, target_selection.get_selected_paths()))
            {
                update_rich_selection(diff_selection, m_falloff_fn, m_falloff_color_fn);
                diff_selection.difference(final_selection);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("remove", true));
                viewport_view->set_selected(diff_selection, Application::instance().get_rich_selection());
            }
            else if (selection_mask == SelectionList::INSTANCES)
            {
                final_selection = target_selection.extract(selection_mask);
                diff_selection.difference(final_selection, SelectionList::INSTANCES);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("remove", true));
                viewport_view->set_selected(diff_selection, Application::instance().get_rich_selection());
            }
            else
            {
                final_selection = target_selection.extract(SelectionList::FULL_SELECTION);
                diff_selection.difference(final_selection);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("remove", true));
                viewport_view->set_selected(diff_selection, Application::instance().get_rich_selection());
            }
        }
        else
        {
            auto final_selection = target_selection.extract(Application::instance().get_highlighted_prims(), selection_mask);
            if (is_components_edited(final_selection, target_selection.get_selected_paths()) || target_selection.empty())
            {
                update_rich_selection(final_selection, m_falloff_fn, m_falloff_color_fn);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("replace", true));
                viewport_view->set_selected(final_selection, Application::instance().get_rich_selection());
            }
            else if (selection_mask == SelectionList::INSTANCES)
            {
                final_selection = target_selection.extract(selection_mask);
                CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("replace", true));
                viewport_view->set_selected(final_selection, RichSelection());
            }
            else
            {
                final_selection = target_selection.extract(SelectionList::FULL_SELECTION);
                if (selection_mask != SelectionList::FULL_SELECTION)
                {
                    Application::instance().set_highlighted_prims(SdfPathVector());
                    Application::instance().set_prim_selection(final_selection.get_selected_paths());
                }
                else
                {
                    CommandInterface::execute("select", CommandArgs().arg(final_selection).kwarg("replace", true));
                }
                viewport_view->set_selected(final_selection, RichSelection());
            }
        }

        m_shift = false;
        return true;
    }
    else if (m_draw_soft_selection_radius && viewport_view->get_scene_context_type() == TfToken("USD") && !m_double_click)
    {
        if (m_centroid)
        {
            const auto new_world_pos = get_world_pos(viewport_view, *m_centroid, m_mousex);
            m_cur_falloff_radius = m_start_falloff_radius + GfSgn(m_mousex - m_start_posx) * (new_world_pos - m_start_world_pos).GetLength();
            Application::instance().get_settings()->set("soft_selection.falloff_radius", std::max(m_cur_falloff_radius, 0.0f));
        }
        else
        {
            Application::instance().get_settings()->set(
                "soft_selection.falloff_radius",
                std::max(m_start_falloff_radius + static_cast<float>(m_mousex - m_start_posx) / viewport_view->get_viewport_dimensions().width * 4.6f,
                         0.0f));
        }
        m_centroid = nullptr;
        m_draw_soft_selection_radius = false;
        return true;
    }
    else if (m_selection_mode_marking_menu && !m_double_click)
    {
        if (auto action = m_selection_mode_marking_menu->get_hovered_action())
        {
            action->trigger();
        }
        m_selection_mode_marking_menu->deleteLater();
        m_selection_mode_marking_menu = nullptr;
        m_shift = false;
        return true;
    }
    else if (!m_double_click_selection.empty())
    {
        update_rich_selection(m_double_click_selection, m_falloff_fn, m_falloff_color_fn);
        if (m_shift)
        {
            CommandInterface::execute("select", CommandArgs().arg(m_double_click_selection).kwarg("add", true));
        }
        else if (m_ctrl)
        {
            CommandInterface::execute("select", CommandArgs().arg(m_double_click_selection).kwarg("remove", true));
        }
        else
        {
            CommandInterface::execute("select", CommandArgs().arg(m_double_click_selection).kwarg("replace", true));
        }
        m_double_click_selection.clear();
    }
    m_double_click = false;
    m_shift = false;
    return false;
}

void ViewportSelectToolContext::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (m_select_rect_mode && std::abs(m_mousex - m_start_posx) > 2 && std::abs(m_mousey - m_start_posy) > 2)
    {
        const auto viewport_dim = viewport_view->get_viewport_dimensions();
        GfVec2f start(2 * float(m_start_posx) / viewport_dim.width - 1, 1 - 2 * float(m_start_posy) / viewport_dim.height);

        GfVec2f end(2 * float(m_mousex) / viewport_dim.width - 1, 1 - 2 * float(m_mousey) / viewport_dim.height);

        draw_manager->begin_drawable();
        draw_manager->set_color(GfVec4f(1, 1, 1, 1));
        draw_manager->set_paint_style(ViewportUiDrawManager::PaintStyle::STIPPLED);
        draw_manager->set_prim_type(ViewportUiDrawManager::PrimitiveTypeLinesStrip);
        draw_manager->rect2d(start, end);
        draw_manager->end_drawable();
    }
    else if (m_draw_soft_selection_radius && viewport_view->get_scene_context_type() == TfToken("USD"))
    {
        const auto frustum = get_conformed_frustum(viewport_view);
        if (m_centroid)
        {
            const auto up = frustum.ComputeUpVector().GetNormalized();
            const auto right = (up ^ frustum.ComputeViewDirection()).GetNormalized();
            auto model_mat = GfMatrix4d().SetScale(GfVec3f(m_cur_falloff_radius));
            model_mat.SetTranslateOnly(*m_centroid);
            draw_utils::draw_circle(draw_manager,
                                    GfMatrix4f(model_mat * frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix()), // mvp
                                    GfVec4f(0, 255, 255, 1), // color
                                    GfVec3f(0, 0, 0), // orig
                                    GfVec3f(right), // vx
                                    GfVec3f(up), // vy
                                    1.0f, // line_width
                                    2); // depth
        }
        else
        {
            draw_utils::draw_circle(draw_manager,
                                    GfMatrix4f(1), // mvp
                                    GfVec4f(0, 255, 255, 1), // color
                                    GfVec3f(0, 0, 0), // orig
                                    GfVec3f(m_cur_falloff_radius / 2.6, 0, 0), // vx
                                    GfVec3f(0, m_cur_falloff_radius * frustum.ComputeAspectRatio() / 2.6, 0), // vy
                                    1.0f, // line_width
                                    2); // depth
        }
    }
}

TfToken ViewportSelectToolContext::get_name() const
{
    return SelectToolTokens->name;
}

std::shared_ptr<Ramp<float>> ViewportSelectToolContext::get_falloff_curve_ramp() const
{
    return m_falloff_curve_ramp;
}

std::shared_ptr<Ramp<PXR_NS::GfVec3f>> ViewportSelectToolContext::get_falloff_color_ramp() const
{
    return m_falloff_color_ramp;
}

void ViewportSelectToolContext::update_falloff_curve_ramp()
{
    std::vector<double> CV_data;
    CV_data.reserve((m_falloff_curve_ramp->cv().size() - 2) * 3);
    for (int i = 1; i < m_falloff_curve_ramp->cv().size() - 1; i++)
    {
        const auto& cv = m_falloff_curve_ramp->cv()[i];
        CV_data.push_back(cv.position);
        CV_data.push_back(cv.value);
        CV_data.push_back(cv.interp_type);
    }
    Application::instance().get_settings()->set("soft_selection.falloff_curve", CV_data);
}

void ViewportSelectToolContext::update_falloff_color_ramp()
{
    std::vector<double> color_data;
    color_data.reserve((m_falloff_color_ramp->cv().size() - 2) * 5);
    for (int i = 1; i < m_falloff_color_ramp->cv().size() - 1; i++)
    {
        const auto& cv = m_falloff_color_ramp->cv()[i];
        color_data.push_back(cv.position);
        color_data.push_back(cv.value[0]);
        color_data.push_back(cv.value[1]);
        color_data.push_back(cv.value[2]);
        color_data.push_back(cv.interp_type);
    }
    Application::instance().get_settings()->set("soft_selection.falloff_color", color_data);
}

QActionGroup* ViewportSelectToolContext::get_selection_mode_action_group()
{
    return m_selection_mode_action_group;
}

const PXR_NS::TfToken& ViewportSelectToolContext::get_selection_kind() const
{
    return m_selection_kind;
}

void ViewportSelectToolContext::set_selection_kind(const PXR_NS::TfToken& selection_kind)
{
    if (m_selection_kind == selection_kind)
    {
        return;
    }

    m_selection_kind = selection_kind;

    const auto settings = Application::instance().get_settings();
    settings->set("session.viewport.select_tool.kind", m_selection_kind.data());
}

bool ViewportSelectToolContext::is_locked() const
{
    return m_adjust_soft_selection_radius || m_draw_soft_selection_radius || m_select_rect_mode || m_selection_mode_marking_menu;
}

bool ViewportSelectToolContext::edge_loop_selection()
{
    auto& application = Application::instance();
    const auto session = application.get_session();
    const auto stage = session->get_current_stage();
    const auto paths = m_last_selection.get_selected_paths();
    const auto path = paths[0];
    const auto prim = stage->GetPrimAtPath(path);

    const auto time = application.get_current_time();
    const auto stage_id = session->get_current_stage_id();
    auto topology = session->get_stage_topology_cache(stage_id);
    auto edge_index_table = EdgeIndexTable(&topology.get_topology(prim, time)->mesh_topology);

    const auto data = m_last_selection[path];
    const auto indices = data.get_edge_indices();
    const auto edge_index = *indices.begin();
    const auto edge_vertices_tuple = edge_index_table.get_vertices_by_edge_id(edge_index);
    if (!std::get<bool>(edge_vertices_tuple))
    {
        return false;
    }

    auto& half_edge_cache = session->get_half_edge_cache(stage_id);
    const auto half_edge = half_edge_cache.get_half_edge(prim, time);
    if (!half_edge)
    {
        return false;
    }

    const auto selected_edge = std::get<GfVec2i>(edge_vertices_tuple);
    m_double_click_selection.clear();
    m_double_click_selection.merge(half_edge->edge_loop_selection(selected_edge));

    return true;
}

bool ViewportSelectToolContext::topology_selection()
{
    auto& application = Application::instance();
    const auto session = application.get_session();
    const auto stage = session->get_current_stage();
    const auto paths = m_last_selection.get_selected_paths();
    const auto path = paths[0];
    const auto prim = stage->GetPrimAtPath(path);
    if (!prim)
    {
        return false;
    }

    const auto time = application.get_current_time();
    const auto stage_id = session->get_current_stage_id();
    auto& half_edge_cache = session->get_half_edge_cache(stage_id);
    const auto half_edge = half_edge_cache.get_half_edge(prim, time);
    if (!half_edge)
    {
        return false;
    }

    m_double_click_selection.clear();
    m_double_click_selection.merge(half_edge->topology_selection(m_last_selection));

    return true;
}

bool ViewportSelectToolContext::on_key_press(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    const auto modifiers = key_event->modifiers();
    const auto key = key_event->key();

    if (key == Qt::Key_B)
    {
        if (m_select_rect_mode || m_selection_mode_marking_menu)
            return true;
        if (!key_event->isAutoRepeat())
        {
            m_key_press_timepoint = key_event->timestamp();
        }
        m_adjust_soft_selection_radius = true;
        return true;
    }
    else if (key == Qt::Key_Greater)
    {
        auto& application = Application::instance();
        const auto session = application.get_session();
        if (!session)
        {
            return false;
        }

        const auto stage = session->get_current_stage();
        if (!stage)
        {
            return false;
        }

        const auto stage_id = session->get_current_stage_id();
        auto& half_edge_cache = session->get_half_edge_cache(stage_id);
        const auto selection = application.get_selection();
        const auto time = application.get_current_time();

        const auto selected_path = selection.get_selected_paths();
        SelectionList additional_select;
        for (const auto path : selected_path)
        {
            const auto prim = stage->GetPrimAtPath(path);
            if (!prim)
            {
                continue;
            }

            const auto half_edge = half_edge_cache.get_half_edge(prim, time);
            if (!half_edge)
            {
                continue;
            }
            additional_select.merge(half_edge->grow_selection(selection));
        }

        if (!additional_select.empty())
        {
            auto selection = Application::instance().get_selection();
            selection.merge(additional_select);
            update_rich_selection(selection, m_falloff_fn, m_falloff_color_fn);
            CommandInterface::execute("select", CommandArgs().arg(additional_select).kwarg("add", true));
            viewport_view->set_selected(Application::instance().get_selection(), Application::instance().get_rich_selection());
        }
    }
    else if (key == Qt::Key_Less)
    {
        auto& application = Application::instance();
        const auto session = application.get_session();
        if (!session)
        {
            return false;
        }

        const auto stage = session->get_current_stage();
        if (!stage)
        {
            return false;
        }

        const auto stage_id = session->get_current_stage_id();
        auto& half_edge_cache = session->get_half_edge_cache(stage_id);
        const auto selection = application.get_selection();
        const auto time = application.get_current_time();

        const auto selected_path = selection.get_selected_paths();
        SelectionList remove_select;
        for (const auto path : selected_path)
        {
            const auto prim = stage->GetPrimAtPath(path);
            if (!prim)
            {
                continue;
            }

            const auto half_edge = half_edge_cache.get_half_edge(prim, time);
            if (!half_edge)
            {
                continue;
            }
            remove_select.merge(half_edge->decrease_selection(selection));
        }

        if (!remove_select.empty())
        {
            auto selection = Application::instance().get_selection();
            selection.difference(remove_select);
            update_rich_selection(selection, m_falloff_fn, m_falloff_color_fn);
            CommandInterface::execute("select", CommandArgs().arg(remove_select).kwarg("remove", true));
            viewport_view->set_selected(Application::instance().get_selection(), Application::instance().get_rich_selection());
        }
    }

    return false;
}

bool ViewportSelectToolContext::on_key_release(QKeyEvent* key_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    if (key_event->key() == Qt::Key_B)
    {
        if (m_select_rect_mode || m_selection_mode_marking_menu)
            return true;
        if (key_event->isAutoRepeat())
            return true;

        if (m_key_press_timepoint != static_cast<unsigned long long>(-1) && (key_event->timestamp() - m_key_press_timepoint) < 300)
        {
            auto enabled = Application::instance().is_soft_selection_enabled();
            Application::instance().enable_soft_selection(!enabled);
        }
        m_adjust_soft_selection_radius = false;
        return true;
    }
    return false;
}

OPENDCC_NAMESPACE_CLOSE
