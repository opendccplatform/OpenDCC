// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "opendcc/anim_engine/core/commands.h"
#include "opendcc/anim_engine/curve/curve.h"
#include "opendcc/anim_engine/core/session.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"

#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/ui/timeline_widget/timebar_widget.h"
#include "opendcc/ui/timeline_widget/timeline_widget.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "utils.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    using base::commands::CommandRegistry;
    using base::commands::CommandSyntax;
    using base::commands::UndoCommand;

    TfType::Define<AddCurvesAndKeysCommand, TfType::Bases<UndoCommand>>();
    TfType::Define<RemoveCurvesAndKeysCommand, TfType::Bases<UndoCommand>>();
    TfType::Define<ChangeKeyframesCommand, TfType::Bases<UndoCommand>>();
    TfType::Define<ChangeInfinityTypeCommand, TfType::Bases<UndoCommand>>();

    CommandRegistry::register_command("anim_engine_add_curves_and_keys",
                                      CommandSyntax()
                                          .kwarg<AnimEngine::CurveIdsList>("curve_ids")
                                          .kwarg<std::vector<AnimEngineCurve>>("curves")
                                          .kwarg<AnimEngine::CurveIdToKeyframesMap>("keyframes"),
                                      [] { return std::make_shared<AddCurvesAndKeysCommand>(); });

    CommandRegistry::register_command("anim_engine_remove_curves_and_keys",
                                      CommandSyntax().kwarg<AnimEngine::CurveIdToKeysIdsMap>("key_ids").kwarg<AnimEngine::CurveIdsList>("curve_ids"),
                                      [] { return std::make_shared<RemoveCurvesAndKeysCommand>(); });

    CommandRegistry::register_command(
        "anim_engine_change_keyframes",
        CommandSyntax().arg<AnimEngine::CurveIdToKeyframesMap>("start_keyframes").arg<AnimEngine::CurveIdToKeyframesMap>("end_keyframes"),
        [] { return std::make_shared<ChangeKeyframesCommand>(); });

    CommandRegistry::register_command(
        "anim_engine_change_infinity_type",
        CommandSyntax().arg<AnimEngine::CurveIdsList>("curve_ids").arg<adsk::InfinityType>("end_inf").arg<bool>("is_pre_inf"),
        [] { return std::make_shared<ChangeInfinityTypeCommand>(); });
}

void update_timebar()
{
    auto main_window = ApplicationUI::instance().get_main_window();
    if (main_window)
    {
        auto stage = Application::instance().get_session()->get_current_stage();
        KeyFrameSet times_set;
        if (stage)
        {
            auto prim_paths = Application::instance().get_prim_selection();
            std::vector<UsdAttributeQuery> attr_query_list;
            for (auto prim_path : prim_paths)
            {
                auto prim = stage->GetPrimAtPath(prim_path);
                if (prim)
                {
                    auto attrs = prim.GetAuthoredAttributes();
                    for (auto attr : attrs)
                    {
                        attr_query_list.push_back(UsdAttributeQuery(attr));
                    }
                }
            }
            std::vector<double> times;
            GfInterval frame_range(stage->GetStartTimeCode(), stage->GetEndTimeCode());
            UsdAttributeQuery::GetUnionedTimeSamplesInInterval(attr_query_list, frame_range, &times);

            for (auto time : times)
            {
                times_set.insert(time);
            }
        }
        main_window->timeline_widget()->time_bar_widget()->set_keyframes(times_set);
        if (main_window->timeline_widget()->time_bar_widget()->get_keyframe_draw_mode() == KeyframeDrawMode::AnimationCurves)
        {
            emit main_window->timeline_widget()->keyframe_draw_mode_changed();
        }
    }
}

void AddCurvesAndKeysCommand::undo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
    {
        engine->remove_curves_direct(m_curves_ids);
        engine->remove_keys_direct(keyframes_to_keyIds(m_keyframes_to_add));
    }
}

void AddCurvesAndKeysCommand::redo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
    {
        engine->add_curves_direct(m_curves, m_curves_ids);
        engine->add_keys_direct(m_keyframes_to_add, false);
    }
}

void AddCurvesAndKeysCommand::set_initial_state(const AnimEngine::CurveIdsList& curve_ids, const std::vector<AnimEngineCurve>& curves,
                                                const AnimEngine::CurveIdToKeyframesMap& keyframes)
{
    m_stage_id = Application::instance().get_session()->get_current_stage_id();
    m_curves_ids = curve_ids;
    m_curves = curves;
    m_keyframes_to_add = keyframes;
}

CommandResult AddCurvesAndKeysCommand::execute(const CommandArgs& args)
{
    m_stage_id = Application::instance().get_session()->get_current_stage_id();

    if (auto curve_ids_arg = args.get_kwarg<AnimEngine::CurveIdsList>("curve_ids"))
        m_curves_ids = *curve_ids_arg;
    if (auto curves_arg = args.get_kwarg<std::vector<AnimEngineCurve>>("curves"))
        m_curves = *curves_arg;
    if (auto keyframes_arg = args.get_kwarg<AnimEngine::CurveIdToKeyframesMap>("keyframes"))
        m_keyframes_to_add = *keyframes_arg;

    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void RemoveCurvesAndKeysCommand::undo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
    {
        engine->add_curves_direct(m_curves, m_curves_ids);
        engine->add_keys_direct(m_keyframes, false);
        update_timebar();
    }
}

void RemoveCurvesAndKeysCommand::redo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
    {
        engine->remove_keys_direct(m_keys_ids);
        engine->remove_curves_direct(m_curves_ids);
        update_timebar();
    }
}

CommandResult RemoveCurvesAndKeysCommand::execute(const CommandArgs& args)
{
    m_stage_id = Application::instance().get_session()->get_current_stage_id();

    AnimEngine::CurveIdsList all_curves_ids;
    if (auto curve_ids_arg = args.get_kwarg<AnimEngine::CurveIdsList>("curve_ids"))
        all_curves_ids = *curve_ids_arg;
    else
        all_curves_ids = AnimEngine::CurveIdsList();

    if (auto key_ids_arg = args.get_kwarg<AnimEngine::CurveIdToKeysIdsMap>("key_ids"))
        m_keys_ids = *key_ids_arg;
    else
        m_keys_ids = AnimEngine::CurveIdToKeysIdsMap();

    AnimEnginePtr anim_engin = AnimEngineSession::instance().engine(m_stage_id);
    for (auto it : m_keys_ids)
    {
        auto curve_id = it.first;
        auto curve = anim_engin->get_curve(curve_id);
        auto id_to_idx_map = curve->compute_id_to_idx_map();
        auto& set = m_keyframes[curve_id];
        if (it.second.size() < curve->keyframeCount())
        {
            for (auto key_id : it.second)
            {
                set.push_back(curve->at(id_to_idx_map[key_id]));
            }
            m_keys_ids[curve_id] = it.second;
        }
        else
        {
            all_curves_ids.push_back(it.first);
        }
    }
    std::set<AnimEngine::CurveId> unique_curve_ids;
    for (auto& curve_id : all_curves_ids)
    {
        if (unique_curve_ids.find(curve_id) == unique_curve_ids.end())
        {
            unique_curve_ids.insert(curve_id);
            auto curve = anim_engin->get_curve(curve_id);
            m_curves.push_back(*curve);
            m_curves_ids.push_back(curve_id);
        }
    }

    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void ChangeKeyframesCommand::set_start_keyframes(const AnimEngine::CurveIdToKeyframesMap& start_key_frames)
{
    m_start_keyframes_list = start_key_frames;
}

void ChangeKeyframesCommand::set_end_keyframes(const AnimEngine::CurveIdToKeyframesMap& end_key_frames)
{
    m_end_keyframes_list = end_key_frames;
}

void ChangeKeyframesCommand::undo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
        engine->set_keys_direct(m_start_keyframes_list);

    update_timebar();
}

void ChangeKeyframesCommand::redo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
        engine->set_keys_direct(m_end_keyframes_list);

    update_timebar();
}

CommandResult ChangeKeyframesCommand::execute(const CommandArgs& args)
{
    m_stage_id = Application::instance().get_session()->get_current_stage_id();
    m_start_keyframes_list = *args.get_arg<AnimEngine::CurveIdToKeyframesMap>(0);
    m_end_keyframes_list = *args.get_arg<AnimEngine::CurveIdToKeyframesMap>(1);

    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

void ChangeInfinityTypeCommand::undo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
        engine->set_infinity_type_direct(m_start_infinity_values, m_is_pre_infinity);
}

void ChangeInfinityTypeCommand::redo()
{
    auto engine = AnimEngineSession::instance().engine(m_stage_id);
    if (engine)
        engine->set_infinity_type_direct(m_curve_ids, m_end_infinity, m_is_pre_infinity);
}

CommandResult ChangeInfinityTypeCommand::execute(const CommandArgs& args)
{
    m_stage_id = Application::instance().get_session()->get_current_stage_id();
    m_curve_ids = *args.get_arg<AnimEngine::CurveIdsList>(0);
    m_end_infinity = *args.get_arg<adsk::InfinityType>(1);
    m_is_pre_infinity = *args.get_arg<bool>(2);
    if (auto engine = AnimEngineSession::instance().engine(m_stage_id))
    {
        for (auto curve_id : m_curve_ids)
        {
            if (m_is_pre_infinity)
                m_start_infinity_values[curve_id] = engine->get_curve(curve_id)->pre_infinity_type();
            else
                m_start_infinity_values[curve_id] = engine->get_curve(curve_id)->post_infinity_type();
        }
    }
    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}
OPENDCC_NAMESPACE_CLOSE
