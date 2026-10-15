// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/usd_live_share/live_share_edits.h"
#include <opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h>
#include <zmq.h>
#include "opendcc/usd/usd_ipc_serialization/usd_ipc_utils.h"
#include "opendcc/usd/usd_ipc_serialization/usd_edits.h"
#include "opendcc/usd/usd_live_share/live_share_state_delegate.h"
#include <queue>
#include <chrono>
#include <filesystem>
#include <pxr/base/js/json.h>
#include <fstream>
#include "opendcc/app/core/settings.h"
#include "opendcc/base/utils/process.h"

OPENDCC_NAMESPACE_OPEN

namespace
{
    uint64_t make_context_id()
    {
        // first 32 bits is pid, second 32 bits unique identifier in process.
        static uint64_t per_process_context_counter = std::numeric_limits<uint32_t>::max();
        return per_process_context_counter++ | static_cast<uint64_t>(base::utils::get_pid());
    }
};

struct SocketRAII
{
    SocketRAII(void* context, int type)
    {
        socket = zmq_socket(context, type);
        const int linger_time = 0;
        zmq_setsockopt(socket, ZMQ_LINGER, &linger_time, sizeof(int));
    }
    ~SocketRAII() { zmq_close(socket); }
    void* socket;
};

PXR_NAMESPACE_USING_DIRECTIVE

void ShareEditsContext::live_share_worker()
{
    auto socket_raii = SocketRAII(m_context, ZMQ_PUB);
    auto socket = socket_raii.socket;
    CHECK_ZMQ_ERROR_AND_RETURN(
        zmq_connect(socket, ("tcp://" + m_connection_settings.hostname + ":" + std::to_string(m_connection_settings.publisher_port)).c_str()));
    std::queue<std::unique_ptr<UsdEditBase>> edits_queue;
    while (m_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        if (edits_queue.empty())
        {
            Lock lock(m_mutex);
            if (m_staged_edits.empty())
                continue;

            edits_queue = std::move(m_staged_edits);
            m_staged_edits = std::queue<std::unique_ptr<UsdEditBase>>();
        }
        while (!edits_queue.empty())
        {
            usd_ipc_utils::send_usd_edit(socket, m_context_id, edits_queue.front().get());
            edits_queue.pop();
        };
    };
}

void ShareEditsContext::transfer_content_worker()
{
    auto socket_raii = SocketRAII(m_context, ZMQ_REP);
    void* socket = socket_raii.socket;
    CHECK_ZMQ_ERROR_AND_RETURN(zmq_connect(
        socket, ("tcp://" + m_connection_settings.hostname + ":" + std::to_string(m_connection_settings.content_transfer_sender_port)).c_str()));
    while (m_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        int recv_id = 0;
        CHECK_ZMQ_ERROR_AND_RETURN(zmq_recv(socket, &recv_id, sizeof(recv_id), 0));
        if (recv_id == 1)
            CHECK_ZMQ_ERROR_AND_RETURN(zmq_send(socket, m_layer_transfer_path.c_str(), m_layer_transfer_path.size(), 0));
    };
}

void ShareEditsContext::transfer_content()
{
    auto socket_raii = SocketRAII(m_context, ZMQ_REQ);
    auto socket = socket_raii.socket;
    CHECK_ZMQ_ERROR_AND_RETURN(zmq_connect(
        socket, ("tcp://" + m_connection_settings.hostname + ":" + std::to_string(m_connection_settings.content_transfer_receiver_port)).c_str()));
    // get transferred state
    const int req_id = 1;
    CHECK_ZMQ_ERROR_AND_RETURN(zmq_send(socket, &req_id, sizeof(req_id), 0));
    char content_path_str[255];
    CHECK_ZMQ_ERROR_AND_RETURN(zmq_recv(socket, content_path_str, sizeof(content_path_str), 0));

    const auto transferred_content_path = std::filesystem::u8path(content_path_str);
    const auto config_path = transferred_content_path / "usd_layer_transfer_content.json";

    auto input_stream = std::ifstream(config_path);
    if (!input_stream.is_open())
        return;

    const auto json_root = JsParseStream(input_stream);
    if (!json_root.IsObject())
        return;

    for (const auto& layer_path : json_root.GetJsObject())
    {
        m_event_queue.enqueue(EventType::Work, [transferred_content_path, layer_id = layer_path.first, repath = layer_path.second]() {
            auto layer = SdfLayer::FindOrOpen(layer_id);
            if (layer)
                layer->TransferContent(SdfLayer::FindOrOpen((transferred_content_path / std::filesystem::u8path(repath.GetString())).u8string()));
        });
    }
}

ShareEditsContext::ShareEditsContext(PXR_NS::UsdStageRefPtr stage, const std::string& layer_transfer_path,
                                     std::shared_ptr<LayerTreeWatcher> layer_tree_watcher,
                                     std::shared_ptr<LayerStateDelegatesHolder> layer_state_delegates, const ConnectionSettings& connection_settings)
    : m_layer_transfer_path(layer_transfer_path)
    , m_layer_tree_watcher(layer_tree_watcher)
    , m_layer_state_delegates(layer_state_delegates)
    , m_future(m_stop_signal.get_future())
    , m_connection_settings(connection_settings)
{
    m_edit_target_watcher = std::make_unique<StageWatcher>(stage, this);

    m_context = zmq_ctx_new();
    LayerStateDelegateRegistry::register_state_delegate(LiveShareStateDelegate::get_name(), [this](LayerStateDelegateProxyPtr proxy) {
        return std::make_shared<LiveShareStateDelegate>(proxy, this);
    });
    m_sublayer_changed_key = layer_tree_watcher->register_sublayers_changed_callback(
        [this](std::string layer, std::string parent, LayerTreeWatcher::SublayerChangeType change_type) {
            if (change_type == LayerTreeWatcher::SublayerChangeType::Added)
                m_layer_state_delegates->add_delegate(LiveShareStateDelegate::get_name(), layer);
        });
    m_context_id = make_context_id();

    m_edit_share_thread = std::thread([this] { live_share_worker(); });
    m_layer_transfer_thread = std::thread([this] { transfer_content_worker(); });
    m_layer_state_delegates->add_delegate(LiveShareStateDelegate::get_name());

    m_event_queue.appendListener(EventType::Work, [](const std::function<void()>& fn) { fn(); });

    m_listener_thread = std::thread([this] {
        auto socket_raii = SocketRAII(m_context, ZMQ_SUB);
        auto socket = socket_raii.socket;
        CHECK_ZMQ_ERROR_AND_RETURN(
            zmq_connect(socket, ("tcp://" + m_connection_settings.hostname + ":" + std::to_string(m_connection_settings.listener_port)).c_str()));
        CHECK_ZMQ_ERROR_AND_RETURN(zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0));

        transfer_content();

        std::queue<UsdEditLayerDependent*> edits;
        while (m_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            uint64_t context_id;
            auto edit = usd_ipc_utils::receive_usd_edit(socket, &context_id);
            if (!edit || context_id == m_context_id)
                continue;

            if (dynamic_cast<UsdEditChangeBlockClosed*>(edit.get()))
            {
                m_event_queue.enqueue(EventType::Work, [edits]() mutable {
                    SdfChangeBlock block;
                    while (!edits.empty())
                    {
                        auto& edit = edits.front();
                        auto layer = SdfLayer::FindOrOpen(edit->get_layer_id());
                        if (layer)
                            edits.front()->apply(layer->GetStateDelegate());

                        edits.pop();
                        delete edit;
                    }
                });

                edits = std::queue<UsdEditLayerDependent*>();
            }
            else if (auto layer_dependent = dynamic_cast<UsdEditLayerDependent*>(edit.get()))
            {
                edits.push(layer_dependent);
                edit.release();
            }
        };
    });
}

ShareEditsContext::~ShareEditsContext()
{
    m_stop_signal.set_value();
    zmq_term(m_context);
    if (m_edit_share_thread.joinable())
        m_edit_share_thread.join();
    if (m_layer_transfer_thread.joinable())
        m_layer_transfer_thread.join();
    if (m_listener_thread.joinable())
        m_listener_thread.join();
    m_edit_target_watcher = nullptr;
    m_layer_state_delegates->remove_delegate(LiveShareStateDelegate::get_name());
    LayerStateDelegateRegistry::unregister_state_delegate(LiveShareStateDelegate::get_name());
    zmq_ctx_destroy(m_context);
}

void ShareEditsContext::send_edit(std::unique_ptr<UsdEditBase> edit)
{
    Lock lock(m_mutex);
    m_staged_edits.push(std::move(edit));
}

void ShareEditsContext::process()
{
    m_is_processing_incoming_edits = true;
    m_event_queue.process();
    m_is_processing_incoming_edits = false;
}

bool ShareEditsContext::is_processing_incoming_edits() const
{
    return m_is_processing_incoming_edits;
}

ShareEditsContext::StageWatcher::StageWatcher(PXR_NS::UsdStageRefPtr stage, ShareEditsContext* edit_context)
    : m_context(edit_context)
{
    m_key = TfNotice::Register(TfWeakPtr<StageWatcher>(this), &StageWatcher::callback, stage);
}

void ShareEditsContext::StageWatcher::callback(PXR_NS::UsdNotice::ObjectsChanged const& notice, PXR_NS::UsdStageWeakPtr const& sender)
{
    auto change_block_closed = std::make_unique<UsdEditChangeBlockClosed>();
    m_context->send_edit(std::move(change_block_closed));
}
OPENDCC_NAMESPACE_CLOSE
