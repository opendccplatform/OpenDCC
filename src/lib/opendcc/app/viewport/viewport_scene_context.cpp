// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_scene_context.h"
#include "opendcc/base/logging/logger.h"
#include <array>
#include "viewport_scene_delegate.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/viewport/hydra_render_settings.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.scene");

PXR_NAMESPACE_USING_DIRECTIVE

static const auto s_register_usd_delegate = ViewportSceneContextRegistry::get_instance().register_scene_context(
    TfToken("USD"), [] { return std::make_shared<ViewportUsdSceneContext>(TfToken("USD")); });

ViewportSceneContext::ViewportSceneContext(const TfToken& context_name)
    : m_context_name(context_name)
{
}

const TfToken& ViewportSceneContext::get_context_name() const
{
    return m_context_name;
}

bool ViewportSceneContext::use_hydra2() const
{
    return false;
}

ViewportSceneContext::DispatcherHandle ViewportSceneContext::register_event_handler(EventType event, const std::function<void()>& callback)
{
    return m_dispatcher.appendListener(event, callback);
}

void ViewportSceneContext::unregister_event_handler(EventType event, DispatcherHandle handle)
{
    m_dispatcher.removeListener(event, handle);
}

void ViewportSceneContext::dispatch(EventType event)
{
    m_dispatcher.dispatch(event);
}

bool ViewportSceneContextRegistry::register_scene_context(const TfToken& context_name, SceneContextCreateFn fn)
{
    const auto insert_result = m_registry.insert({ context_name, fn }).second;
    if (!insert_result)
        OPENDCC_ERROR("Failed to register scene context '{}': another context with the same name was already registered.", context_name.GetText());
    return insert_result;
}

bool ViewportSceneContextRegistry::unregister_scene_context(const TfToken& context_name)
{
    const auto erase_result = m_registry.erase(context_name);
    if (erase_result == 0)
        OPENDCC_WARN("Failed to unregister scene context '{}': the context with specified name was not found.", context_name.GetText());
    return static_cast<bool>(erase_result);
}

ViewportSceneContextPtr ViewportSceneContextRegistry::create_scene_context(const TfToken& context_name) const
{
    auto iter = m_registry.find(context_name);
    if (iter == m_registry.end())
    {
        OPENDCC_ERROR("Failed to create scene context '{}': factory function is not registered.", context_name.GetText());
        return nullptr;
    }
    return iter->second();
}

ViewportSceneContextRegistry& ViewportSceneContextRegistry::get_instance()
{
    static ViewportSceneContextRegistry instance;
    return instance;
}

ViewportUsdSceneContext::ViewportUsdSceneContext(const TfToken& context_name)
    : ViewportSceneContext(context_name)
{
    std::set<TfType> registered_delegates;
    TfType::Find<ViewportSceneDelegate>().GetAllDerivedTypes(&registered_delegates);

    for (const auto& delegate : registered_delegates)
    {
        if (auto factory = delegate.GetFactory<ViewportSceneDelegateFactoryBase>())
        {
            if (factory->get_context_type() == context_name)
                m_delegates.insert(delegate);
        }
    }

    auto dirty_settings = [this] {
        m_render_settings =
            UsdHydraRenderSettings::create(Application::instance().get_session()->get_current_stage(), Application::instance().get_current_time());

        dispatch(EventType::DirtyRenderSettings);
    };

    auto on_stage_changed = [this, dirty_settings] {
        dirty_settings();
        update_stage_watcher();
    };
    m_current_stage_changed_cid =
        Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [on_stage_changed] { on_stage_changed(); });
    m_time_changed_cid = Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, dirty_settings);
    on_stage_changed();
}

ViewportUsdSceneContext::~ViewportUsdSceneContext()
{
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_current_stage_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_TIME_CHANGED, m_time_changed_cid);
}

ViewportUsdSceneContext::SceneDelegateCollection ViewportUsdSceneContext::get_delegates() const
{
    return m_delegates;
}

SelectionList ViewportUsdSceneContext::get_selection() const
{
    return Application::instance().get_selection();
}

void ViewportUsdSceneContext::resolve_picking(PXR_NS::HdxPickHitVector& pick_hits)
{
    for (auto& hit : pick_hits)
    {
        hit.objectId = hit.objectId.ReplacePrefix(hit.delegateId, SdfPath::AbsoluteRootPath());
    }
}

std::shared_ptr<HydraRenderSettings> ViewportUsdSceneContext::get_render_settings() const
{
    return m_render_settings;
}

void ViewportUsdSceneContext::update_stage_watcher()
{
    auto stage = Application::instance().get_session()->get_current_stage();
    if (stage)
    {
        m_stage_watcher = std::make_unique<StageObjectChangedWatcher>(stage, [this](PXR_NS::UsdNotice::ObjectsChanged const& notice) {
            bool need_to_update = false;
            std::string render_settings_path;
            notice.GetStage()->GetMetadata(TfToken("renderSettingsPrimPath"), &render_settings_path);
            if (m_render_settings_path.GetString() != render_settings_path)
            {
                need_to_update = true;
                m_render_settings_path = render_settings_path.empty() ? SdfPath::EmptyPath() : SdfPath(render_settings_path);
            }

            SdfPath cam_path = m_render_settings ? m_render_settings->get_camera_path() : SdfPath::EmptyPath();
            if (!cam_path.IsEmpty() && notice.HasChangedFields(cam_path))
            {
                need_to_update = true;
            }

            std::array<UsdNotice::ObjectsChanged::PathRange, 2> paths = { notice.GetResyncedPaths(), notice.GetChangedInfoOnlyPaths() };
            for (int i = 0; i < paths.size() && !need_to_update; i++)
            {
                for (const auto& path : paths[i])
                {
                    if (path.HasPrefix(m_render_settings_path) || (m_render_settings && m_render_settings->has_setting(path)))
                    {
                        need_to_update = true;
                        break;
                    }
                }
            }

            if (need_to_update)
            {
                m_render_settings = UsdHydraRenderSettings::create(Application::instance().get_session()->get_current_stage(),
                                                                   Application::instance().get_current_time());
                dispatch(EventType::DirtyRenderSettings);
            }
        });
    }
    else
    {
        m_stage_watcher.reset();
        m_render_settings.reset();
        dispatch(EventType::DirtyRenderSettings);
    }
}

OPENDCC_NAMESPACE_CLOSE
