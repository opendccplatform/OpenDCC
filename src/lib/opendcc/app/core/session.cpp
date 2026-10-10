// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/session.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/stage_watcher.h"
#include <pxr/usd/usd/stageCacheContext.h>
#include "opendcc/app/viewport/viewport_refine_manager.h"
#include "opendcc/app/core/usd_edit_undo_command.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/app/core/usd_edit_undo_watcher.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h"
#include "opendcc/usd/usd_live_share/live_share_edits.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

Session::Session() {}

Session::~Session()
{
    UsdUtilsStageCache::Get().Clear();
    m_stage_list_ids.clear();
}

PXR_NS::UsdStageCache &Session::get_stage_cache()
{
    return UsdUtilsStageCache::Get();
}

void Session::set_current_stage(const PXR_NS::UsdStageRefPtr &stage)
{
    if (!get_stage_cache().Contains(stage))
    {
        get_stage_cache().Insert(stage);
    }
    m_current_stage_id = get_stage_cache().GetId(stage);
    const auto &app_config = Application::instance().get_app_config();
    if (stage && app_config.get("sentry.track_stage", false))
    {
        // inspired by https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usd/usd/wrapStage.cpp#L90
        auto sdf_layer_repr = [](const SdfLayerHandle &self) -> std::string {
            return "SdfLayer(" + self->GetIdentifier() + ")";
        };
        std::string result = TfStringPrintf("UsdStage(rootLayer=%s)", sdf_layer_repr(stage->GetRootLayer()).c_str());

        CrashHandler::set_extra("current_stage", result);
    }

    auto find_id = std::find(m_stage_list_ids.begin(), m_stage_list_ids.end(), m_current_stage_id);
    if (find_id == m_stage_list_ids.end())
    {
        m_stage_list_ids.push_back(m_current_stage_id);
        Application::instance().m_event_dispatcher.dispatch(Application::EventType::SESSION_STAGE_LIST_CHANGED);
    }

    update_watcher();
    update_current_stage_bbox_cache_time();
    update_current_stage_xform_cache_time();

    Application::instance().m_event_dispatcher.dispatch(Application::EventType::CURRENT_STAGE_CHANGED);
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_CHANGED);
    create_stage_half_edge_cache(m_current_stage_id);
}

void Session::set_current_stage(PXR_NS::UsdStageCache::Id id)
{
    m_current_stage_id = id;
    auto find_id = std::find(m_stage_list_ids.begin(), m_stage_list_ids.end(), m_current_stage_id);
    if (find_id == m_stage_list_ids.end())
    {
        m_stage_list_ids.push_back(m_current_stage_id);
        Application::instance().m_event_dispatcher.dispatch(Application::EventType::SESSION_STAGE_LIST_CHANGED);
    }

    update_watcher();
    update_current_stage_bbox_cache_time();
    update_current_stage_xform_cache_time();

    Application::instance().m_event_dispatcher.dispatch(Application::EventType::CURRENT_STAGE_CHANGED);
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_CHANGED);
    create_stage_half_edge_cache(m_current_stage_id);
}

UsdStageRefPtr Session::get_current_stage()
{
    return get_stage_cache().Find(m_current_stage_id);
}

PXR_NS::UsdStageRefPtr Session::open_stage(const std::string &asset_path)
{
    PXR_NS::UsdStageRefPtr stage;
    SdfLayerRefPtr rootLayer = SdfLayer::Find(asset_path);
    if (rootLayer)
    {
        rootLayer->Reload();
    }
    else
    {
        rootLayer = SdfLayer::FindOrOpen(asset_path);
    }

    if (rootLayer)
    {
        stage = open_stage(rootLayer);
    }
    return stage;
}

bool Session::close_stage(UsdStageCache::Id id)
{
    const bool was_current = id == m_current_stage_id;
    if (was_current)
    {
        Application::instance().m_event_dispatcher.dispatch(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED);
        m_current_stage_id = UsdStageCache::Id();
        update_watcher();
        Application::instance().m_event_dispatcher.dispatch(Application::EventType::CURRENT_STAGE_CHANGED);
        Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_CHANGED);
    }

    clear_stage_bbox_cache(id);
    clear_stage_xform_cache(id);
    clear_stage_topology_cache(id);
    clear_stage_half_edge_cache(id);

    bool result = get_stage_cache().Erase(id);
    if (!result)
    {
        return false;
    }
    else
    {
        m_stage_list_ids.erase(
            std::remove_if(m_stage_list_ids.begin(), m_stage_list_ids.end(), [&](const UsdStageCache::Id &found_id) { return id == found_id; }),
            m_stage_list_ids.end());
        auto stages = get_stage_list();
        if (was_current && stages.size() > 0)
        {
            m_current_stage_id = get_stage_cache().GetId(stages[0]);
            update_watcher();

            Application::instance().m_event_dispatcher.dispatch(Application::EventType::CURRENT_STAGE_CHANGED);
            Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_CHANGED);
        }
    }
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::SESSION_STAGE_LIST_CHANGED);
    return result;
}

bool Session::close_stage(const PXR_NS::UsdStageRefPtr &stage)
{
    auto id = get_stage_cache().GetId(stage);
    bool result = close_stage(id);
    return result;
}

void Session::force_update_stage_list()
{
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::SESSION_STAGE_LIST_CHANGED);
}

std::vector<PXR_NS::UsdStageRefPtr> Session::get_stage_list()
{
    std::vector<PXR_NS::UsdStageRefPtr> result;
    for (auto &&id : m_stage_list_ids)
    {
        auto stage = get_stage_cache().Find(id);
        if (stage)
        {
            result.push_back(stage);
        }
    }
    return result;
}

void Session::close_all()
{
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::BEFORE_CURRENT_STAGE_CLOSED);

    m_per_stage_bbox_cache.clear();
    m_per_stage_xform_cache.clear();
    m_half_edge_cache.clear();
    m_current_stage_id = UsdStageCache::Id();
    m_stage_list_ids.clear();
    get_stage_cache().Clear();
    update_watcher();
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::CURRENT_STAGE_CHANGED);
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_CHANGED);
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::SESSION_STAGE_LIST_CHANGED);
}

PXR_NS::UsdStageCache::Id Session::get_stage_id(const PXR_NS::UsdStageRefPtr &stage)
{
    return get_stage_cache().GetId(stage);
}

std::shared_ptr<LayerTreeWatcher> Session::get_current_stage_layer_tree() const
{
    return m_layer_tree_watcher;
}

PXR_NS::UsdStageCache::Id Session::get_current_stage_id()
{
    return m_current_stage_id;
}

PXR_NS::UsdStageRefPtr Session::open_stage(const SdfLayerHandle &layer)
{
    UsdStageCacheContext ctx(get_stage_cache());
    UsdStage::InitialLoadSet loadOperation = UsdStage::LoadAll;
    auto stage = UsdStage::Open(layer, loadOperation);
    Application::instance().m_event_dispatcher.dispatch(Application::EventType::SESSION_STAGE_LIST_CHANGED);
    set_current_stage(stage);
    return stage;
}

PXR_NS::UsdGeomBBoxCache &Session::get_stage_bbox_cache(PXR_NS::UsdStageCache::Id id)
{
    auto find_it = m_per_stage_bbox_cache.find(id);
    if (find_it == m_per_stage_bbox_cache.end())
    {
        TfTokenVector tokens;
        tokens.push_back(UsdGeomTokens->default_);
        auto time = UsdTimeCode(Application::instance().get_current_time());
        UsdGeomBBoxCache bbox_cache(time, tokens, true);
        m_per_stage_bbox_cache.insert(std::make_pair(id, bbox_cache));
        return m_per_stage_bbox_cache.at(id);
    }
    else
    {
        return find_it->second;
    }
}

void Session::create_stage_half_edge_cache(PXR_NS::UsdStageCache::Id id)
{
    m_half_edge_cache[id];
}

void Session::clear_stage_bbox_cache(PXR_NS::UsdStageCache::Id id)
{
    auto bbox_it = m_per_stage_bbox_cache.find(id);
    if (bbox_it != m_per_stage_bbox_cache.end())
    {
        m_per_stage_bbox_cache.erase(bbox_it);
    }
}

void Session::update_watcher()
{
    if (m_stage_watcher)
    {
        m_stage_watcher = nullptr;
    }
    if (m_edit_target_watcher)
    {
        m_edit_target_watcher = nullptr;
    }

    if (m_edit_target_dirty_watcher)
    {
        m_edit_target_dirty_watcher = nullptr;
    }

    if (m_undo_stack_watcher)
    {
        m_undo_stack_watcher = nullptr;
    }

    auto stage = get_current_stage();
    if (stage)
    {
        m_stage_watcher = std::make_unique<StageObjectChangedWatcher>(stage, [this](PXR_NS::UsdNotice::ObjectsChanged const &notice) {
            m_stage_changed_event_dispatcher.dispatch(StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED, notice);
        });
        m_edit_target_watcher =
            std::make_unique<StageEditTargetChangedWatcher>(stage, [this, stage](PXR_NS::UsdNotice::StageEditTargetChanged const &notice) {
                auto layer = stage->GetEditTarget().GetLayer();
                m_edit_target_dirty_watcher =
                    std::make_unique<SdfLayerDirtinessChangedWatcher>(layer, [this](PXR_NS::SdfNotice::LayerDirtinessChanged const &notice) {
                        Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_DIRTINESS_CHANGED);
                    });

                Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_CHANGED);
            });

        auto layer = stage->GetEditTarget().GetLayer();
        m_edit_target_dirty_watcher =
            std::make_unique<SdfLayerDirtinessChangedWatcher>(layer, [this](PXR_NS::SdfNotice::LayerDirtinessChanged const &notice) {
                Application::instance().m_event_dispatcher.dispatch(Application::EventType::EDIT_TARGET_DIRTINESS_CHANGED);
            });

        m_layer_tree_watcher = std::make_shared<LayerTreeWatcher>(stage);
        m_layer_state_delegates = std::make_shared<LayerStateDelegatesHolder>(m_layer_tree_watcher);
        if (Application::instance().is_ui_available())
        {
            m_undo_stack_watcher = std::make_unique<UsdEditUndoStackChangedWatcher>(
                m_layer_tree_watcher, m_layer_state_delegates, [this](std::shared_ptr<commands::UndoInverse> inverse) {
                    auto cmd = CommandRegistry::create_command<UsdEditUndoCommand>("usd_edit_undo");
                    cmd->set_state(inverse);
                    CommandInterface::finalize(cmd, CommandArgs().arg(inverse));
                });
        }
    }
}

OPENDCC_API HalfEdgeCache &Session::get_half_edge_cache(PXR_NS::UsdStageCache::Id id)
{
    return m_half_edge_cache[id];
}

void Session::update_current_stage_bbox_cache_time()
{
    if (m_current_stage_id.IsValid())
    {
        auto &bbox_cache = get_stage_bbox_cache(m_current_stage_id);
        bbox_cache.SetTime(UsdTimeCode(Application::instance().get_current_time()));
    }
}

Session::StageChangedCallbackHandle Session::register_stage_changed_callback(
    const StageChangedEventType &event_type, const std::function<void(PXR_NS::UsdNotice::ObjectsChanged const &notice)> callback)
{
    return m_stage_changed_event_dispatcher.appendListener(event_type, callback);
}

void Session::unregister_stage_changed_callback(const StageChangedEventType &event_type, StageChangedCallbackHandle &handle)
{
    m_stage_changed_event_dispatcher.removeListener(event_type, handle);
}

Session::CallbackHandle Session::register_event_callback(EventType event_type, std::function<void()> callback)
{
    return m_event_dispatcher.appendListener(event_type, callback);
}

void Session::unregister_event_callback(EventType event_type, const CallbackHandle &handle)
{
    m_event_dispatcher.removeListener(event_type, handle);
}

PXR_NS::UsdGeomXformCache &Session::get_stage_xform_cache(PXR_NS::UsdStageCache::Id id)
{
    auto find_it = m_per_stage_xform_cache.find(id);
    if (find_it == m_per_stage_xform_cache.end())
    {
        TfTokenVector tokens;
        tokens.push_back(UsdGeomTokens->default_);
        auto time = UsdTimeCode(Application::instance().get_current_time());
        UsdGeomXformCache xform_cache(time);
        m_per_stage_xform_cache.insert(std::make_pair(id, xform_cache));
        return m_per_stage_xform_cache.at(id);
    }
    else
    {
        return find_it->second;
    }
}

TopologyCache &Session::get_stage_topology_cache(PXR_NS::UsdStageCache::Id id)
{
    return m_per_stage_topology_cache[id];
}

void Session::clear_stage_xform_cache(PXR_NS::UsdStageCache::Id id)
{
    auto find_it = m_per_stage_xform_cache.find(id);
    if (find_it != m_per_stage_xform_cache.end())
    {
        m_per_stage_xform_cache.erase(find_it);
    }
}

void Session::clear_stage_topology_cache(PXR_NS::UsdStageCache::Id id)
{
    m_per_stage_topology_cache.erase(id);
}

void Session::clear_stage_half_edge_cache(PXR_NS::UsdStageCache::Id id)
{
    m_half_edge_cache.erase(id);
}

void Session::update_current_stage_xform_cache_time()
{
    if (m_current_stage_id.IsValid())
    {
        auto &xform_cache = get_stage_xform_cache(m_current_stage_id);
        xform_cache.SetTime(UsdTimeCode(Application::instance().get_current_time()));
    }
}

void Session::enable_live_sharing(bool enable)
{
    if (!enable || !m_layer_tree_watcher)
    {
        m_share_context = nullptr;
        m_event_dispatcher.dispatch(EventType::LIVE_SHARE_STATE_CHANGED);
        return;
    }

    if (m_share_context)
        return;

    auto settings = Application::instance().get_settings();
    ShareEditsContext::ConnectionSettings connection_settings;
    connection_settings.hostname = settings->get<std::string>("live_share.hostname", "127.0.0.1");
    connection_settings.listener_port = settings->get<uint32_t>("live_share.listener_port", 5561);
    connection_settings.publisher_port = settings->get<uint32_t>("live_share.publisher_port", 5562);
    connection_settings.content_transfer_sender_port = settings->get<uint32_t>("live_share.sync_sender_port", 5560);
    connection_settings.content_transfer_receiver_port = settings->get<uint32_t>("live_share.sync_receiver_port", 5559);

    m_share_context = std::make_unique<ShareEditsContext>(get_current_stage(), m_transferred_content_dir, m_layer_tree_watcher,
                                                          m_layer_state_delegates, connection_settings);
    m_event_dispatcher.dispatch(EventType::LIVE_SHARE_STATE_CHANGED);
}

bool Session::is_live_sharing_enabled()
{
    return m_share_context != nullptr;
}

void Session::process_events()
{
    if (m_share_context)
        m_share_context->process();
}

void Session::set_transferred_content_dir(const std::string &dir)
{
    m_transferred_content_dir = dir;
}

std::string Session::get_transferred_content_dir() const
{
    return m_transferred_content_dir;
}

OPENDCC_NAMESPACE_CLOSE
