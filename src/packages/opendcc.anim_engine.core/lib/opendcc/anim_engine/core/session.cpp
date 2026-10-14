// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/anim_engine/core/session.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/anim_engine/curve/curve.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

PXR_NAMESPACE_USING_DIRECTIVE;
using namespace OPENDCC_NAMESPACE;

AnimEngineSession& AnimEngineSession::instance()
{
    static AnimEngineSession engine;
    return engine;
}

AnimEnginePtr AnimEngineSession::current_engine()
{
    auto stage_id = Application::instance().get_session()->get_current_stage_id();
    return engine(stage_id);
}

AnimEnginePtr AnimEngineSession::engine(const PXR_NS::UsdStageCache::Id& stage_id)
{
    auto stage = Application::instance().get_session()->get_stage_cache().Find(stage_id);
    if (!stage)
        return nullptr;

    auto it = instance().m_engines.find(stage_id.ToLongInt());
    if (it != instance().m_engines.end())
    {
        return it->second;
    }
    else
    {
        auto new_engine = std::make_shared<AnimEngine>(stage);
        m_engines.emplace(stage_id.ToLongInt(), new_engine);
        return new_engine;
    }
}

adsk::KeyId AnimEngineSession::generate_unique_key_id()
{
    return AnimCurve::generate_unique_key_id();
}

AnimEngineSession::AnimEngineSession()
{
    m_application_event_handles[Application::EventType::SESSION_STAGE_LIST_CHANGED] = Application::instance().register_event_callback(
        Application::EventType::SESSION_STAGE_LIST_CHANGED, [this]() { session_stage_list_changed(); });
    m_application_event_handles[Application::EventType::CURRENT_STAGE_CHANGED] =
        Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this]() { current_stage_changed(); });
    m_application_event_handles[Application::EventType::CURRENT_TIME_CHANGED] =
        Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this]() { current_time_changed(); });
}

AnimEngineSession::~AnimEngineSession()
{
    for (auto& it : m_application_event_handles)
        Application::instance().unregister_event_callback(it.first, it.second);
}

void AnimEngineSession::current_stage_changed()
{
    if (m_current_engine)
    {
        for (auto& it : m_events)
            m_current_engine->unregister_event_callback(it.first, it.second);
        for (auto& it : m_keys_events)
            m_current_engine->unregister_event_callback(it.first, it.second);
        m_events.clear();
        m_keys_events.clear();
    }

    m_current_engine = current_engine();

    if (m_current_engine)
    {
        m_events[AnimEngine::EventType::CURVES_ADDED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::CURVES_ADDED, [&](const AnimEngine::CurveIdsList& ids) {
                m_dispatcher.dispatch(EventType::CURRENT_STAGE_ANIM_CHANGED);
                m_dispatcher.dispatch(EventType::CURVES_ADDED);
            });

        m_events[AnimEngine::EventType::CURVES_REMOVED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::CURVES_REMOVED, [&](const AnimEngine::CurveIdsList& ids) {
                m_dispatcher.dispatch(EventType::CURRENT_STAGE_ANIM_CHANGED);
                m_dispatcher.dispatch(EventType::CURVES_REMOVED);
            });

        m_keys_events[AnimEngine::EventType::KEYFRAMES_REMOVED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::KEYFRAMES_REMOVED, [&](const AnimEngine::CurveIdToKeysIdsMap& list) {
                m_dispatcher.dispatch(EventType::CURRENT_STAGE_ANIM_CHANGED);
            });

        m_keys_events[AnimEngine::EventType::KEYFRAMES_ADDED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::KEYFRAMES_ADDED, [&](const AnimEngine::CurveIdToKeysIdsMap& list) {
                m_dispatcher.dispatch(EventType::CURRENT_STAGE_ANIM_CHANGED);
            });

        m_keys_events[AnimEngine::EventType::KEYFRAMES_CHANGED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::KEYFRAMES_CHANGED, [&](const AnimEngine::CurveIdToKeysIdsMap& list) {
                m_dispatcher.dispatch(EventType::CURRENT_STAGE_ANIM_CHANGED);
            });

        m_current_engine->on_changed();
        m_dispatcher.dispatch(EventType::CURRENT_STAGE_ANIM_CHANGED);
    }
}

void AnimEngineSession::current_time_changed()
{
    auto engine = current_engine();
    if (engine)
    {
        engine->on_changed();
    }
}

void AnimEngineSession::session_stage_list_changed()
{
    std::vector<long int> to_delete;
    for (auto it : m_engines)
    {
        auto stage_id = UsdStageCache::Id::FromLongInt(it.first);
        auto stage = Application::instance().get_session()->get_stage_cache().Find(stage_id);
        if (!stage)
            to_delete.push_back(it.first);
    }

    for (auto id : to_delete)
        m_engines.erase(id);
}
OPENDCC_NAMESPACE_CLOSE
