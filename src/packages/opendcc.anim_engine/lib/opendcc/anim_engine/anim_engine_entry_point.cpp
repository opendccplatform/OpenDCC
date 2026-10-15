// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/base/tf/type.h>
#include "opendcc/anim_engine/anim_engine_entry_point.h"
#include "opendcc/anim_engine/ui/graph_editor/graph_editor.h"
#include "opendcc/app/ui/panel_factory.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/main_window.h"

#include "opendcc/ui/timeline_widget/timebar_widget.h"
#include "opendcc/ui/timeline_widget/timeline_widget.h"

#include "opendcc/app/core/undo/stack.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::packaging::Package;
using namespace OPENDCC_NAMESPACE; // TODO use macros
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(AnimEngineEntryPoint);

void AnimEngineEntryPoint::initialize(const Package& package)
{
    PanelFactory::instance().register_panel(
        "graph_editor", []() { return new GraphEditor(); }, i18n("graph_editor", "Graph Editor").toStdString(), false, ":icons/panel_graph_editor");

    PanelFactory::instance().register_panel(
        "channel_editor", []() { return new ChannelEditor(false); }, i18n("graph_editor", "Channel Editor").toStdString(), false,
        ":icons/panel_channel_editor");

    m_timeline_selection_changed_callback_id =
        Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [&]() { timeline_widget_update(); });
    Application::instance().register_event_callback(Application::EventType::AFTER_UI_LOAD, [this]() {
        auto main_window = ApplicationUI::instance().get_main_window();
        if (!main_window)
            return;
        auto timeline_widget = main_window->timeline_widget();
        if (!timeline_widget)
            return;
        ApplicationUI::instance().get_main_window()->connect(timeline_widget, &TimelineWidget::keyframe_draw_mode_changed,
                                                             [&]() { timeline_widget_update(); });

        auto timebar_widget = timeline_widget->time_bar_widget();
        QObject::connect(timebar_widget, &TimeBarWidget::time_selection_begin, [&](double start, double end) {
            m_time_selection_begin_start = start;
            m_time_selection_begin_end = end;
            transform_keys_begin();
        });
        QObject::connect(timebar_widget, &TimeBarWidget::time_selection_move, [&](double start, double end) {
            m_time_selection_end_start = start;
            m_time_selection_end_end = end;
            transform_keys_move();
        });
        QObject::connect(timebar_widget, &TimeBarWidget::time_selection_end, [&](double start, double end) {
            m_time_selection_end_start = start;
            m_time_selection_end_end = end;
            transform_keys_command();
        });
    });

    m_anim_changed_id = AnimEngineSession::instance().register_event_callback(AnimEngineSession::EventType::CURRENT_STAGE_ANIM_CHANGED,
                                                                              [&]() { timeline_widget_update(); });
}

void AnimEngineEntryPoint::uninitialize(const Package& package)
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_timeline_selection_changed_callback_id);
    AnimEngineSession::instance().unregister_event_callback(AnimEngineSession::EventType::CURRENT_STAGE_ANIM_CHANGED, m_anim_changed_id);
}

void AnimEngineEntryPoint::timeline_widget_update()
{
    auto main_window = ApplicationUI::instance().get_main_window();
    if (!main_window)
        return;
    auto timeline_widget = main_window->timeline_widget();
    if (!timeline_widget)
        return;

    if (timeline_widget->time_bar_widget()->get_keyframe_draw_mode() == KeyframeDrawMode::AnimationCurves)
    {
        KeyFrameSet keyframes = AnimEngineSession::instance().current_engine()->selected_prims_keys_times();
        timeline_widget->time_bar_widget()->set_keyframes(keyframes);
    }
}

inline double remap(double a1, double a2, double b1, double b2, double s)
{
    return b1 + (s - a1) * (b2 - b1) / (a2 - a1);
}

void AnimEngineEntryPoint::transform_keys_begin()
{
    auto engine = AnimEngineSession::instance().current_engine();
    auto prim_paths = Application::instance().get_prim_selection();

    m_end_key_list.clear();
    m_start_key_list.clear();

    m_transform_keys_change = false;

    for (auto prim_path : prim_paths)
    {
        auto curves_id = engine->curves(prim_path);
        for (auto curve_id : curves_id)
        {
            auto curve = engine->get_curve(curve_id);
            auto& start_list = m_end_key_list[curve_id];
            auto& end_list = m_start_key_list[curve_id];
            auto map = curve->compute_id_to_idx_map();
            for (size_t key_idx = 0; key_idx < curve->keyframeCount(); ++key_idx)
            {
                adsk::Keyframe keyframe = (*curve)[map[curve->at(key_idx).id]];
                const double t = keyframe.time;
                if (t >= m_time_selection_begin_start && t < m_time_selection_begin_end)
                {
                    m_transform_keys_change = true;
                    start_list.push_back(keyframe);
                    end_list.push_back(keyframe);
                }
            }
        }
    }
}

void AnimEngineEntryPoint::transform_keys_update()
{
    for (auto const& item : m_start_key_list)
    {
        auto& start_list = m_start_key_list[item.first];
        auto& end_list = m_end_key_list[item.first];

        for (int i = 0; i < start_list.size(); i++)
        {
            auto& start_keyframe = start_list[i];
            auto& end_keyframe = end_list[i];
            end_keyframe.time = remap(m_time_selection_begin_start, m_time_selection_begin_end, m_time_selection_end_start, m_time_selection_end_end,
                                      start_keyframe.time);
        }
    }
}

void AnimEngineEntryPoint::transform_keys_move()
{
    if (m_transform_keys_change)
    {
        transform_keys_update();

        auto stage_id = Application::instance().get_session()->get_current_stage_id();
        auto engine = AnimEngineSession::instance().engine(stage_id);
        if (engine)
            engine->set_keys_direct(m_end_key_list);
    }
}

void AnimEngineEntryPoint::transform_keys_command()
{
    if (m_transform_keys_change)
    {
        transform_keys_update();

        auto cmd = base::commands::CommandRegistry::create_command<ChangeKeyframesCommand>("anim_engine_change_keyframes");
        cmd->set_start_keyframes(m_start_key_list);
        cmd->set_end_keyframes(m_end_key_list);
        base::commands::CommandInterface::finalize(cmd, base::commands::CommandArgs().arg(m_start_key_list).arg(m_end_key_list));
    }

    m_time_selection_begin_start = m_time_selection_end_start;
    m_time_selection_begin_end = m_time_selection_end_end;
    transform_keys_begin();
}
OPENDCC_NAMESPACE_CLOSE
