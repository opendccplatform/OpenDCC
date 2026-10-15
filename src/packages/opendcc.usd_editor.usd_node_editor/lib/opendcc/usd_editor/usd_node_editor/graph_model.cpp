// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/graph_model.h"
#include "usd_fallback_proxy/core/usd_prim_fallback_proxy.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/commands_api/core/block.h"
#include <pxr/usd/usdUI/nodeGraphNodeAPI.h>
#include <pxr/base/tf/hash.h>
#include <QVector>
#include <queue>
#include "opendcc/usd_editor/usd_node_editor/move_items_command.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/undo/block.h"

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN

using base::commands::CommandInterface;
using base::commands::CommandArgs;
using base::commands::UndoCommandBlock;

namespace
{
    class UsdMoveAction : public MoveAction
    {
    public:
        UsdMoveAction(std::weak_ptr<StageObjectChangedWatcher> watcher, UsdGraphModel& model, const NodeId& node_id, const QPointF& new_pos)
            : m_watcher(watcher)
            , m_model(model)
        {
            commands::UsdEditsBlock change_block;

            const auto prim = m_model.get_prim_for_node(node_id);
            if (!prim || !is_valid())
                return;
            m_watcher.lock()->block_notifications(true);
            auto node_api = UsdUINodeGraphNodeAPI(prim);
            if (!node_api)
                UsdUINodeGraphNodeAPI::Apply(prim);

            node_api.CreatePosAttr(VtValue(GfVec2f(new_pos.x(), new_pos.y())));
            m_watcher.lock()->block_notifications(false);
            m_inverse = change_block.take_edits();
        }

        void undo() override
        {
            if (is_valid() && m_inverse)
                m_inverse->invert();
        }
        void redo() override
        {
            if (is_valid() && m_inverse)
                m_inverse->invert();
        }

    private:
        bool is_valid() const { return !m_watcher.expired(); }
        std::weak_ptr<StageObjectChangedWatcher> m_watcher;
        UsdGraphModel& m_model;
        std::unique_ptr<commands::UndoInverse> m_inverse;
    };
};

UsdGraphModel::UsdGraphModel(QObject* parent /*= nullptr*/)
    : GraphModel(parent)
{
    m_node_provider = std::make_unique<NodeProvider>(*this);
    m_selection_changed_cid = Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] {
        if (!m_updating_selection)
            on_selection_changed();
    });
}

UsdGraphModel::~UsdGraphModel()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_cid);
}

std::string UsdGraphModel::get_property_name(const PortId& port_id) const
{
    auto delimiter = port_id.rfind('.');
    if (delimiter == std::string::npos)
        return std::string();
    return port_id.substr(delimiter + 1);
}

QPointF UsdGraphModel::get_node_position(const NodeId& node_id) const
{
    const auto prim = get_prim_for_node(node_id);
    if (auto node_api = UsdUINodeGraphNodeAPI(prim))
    {
        auto pos_attr = node_api.GetPosAttr();
        if (pos_attr.IsAuthored())
        {
            GfVec2f pos;
            if (pos_attr.Get<GfVec2f>(&pos))
                return QPointF(pos[0], pos[1]);
        }
    }
    return QPointF();
}

NodeId UsdGraphModel::get_parent_path(const PortId& port_id)
{
    return port_id.substr(0, port_id.rfind('/'));
}

void UsdGraphModel::on_nodes_moved(const QVector<std::string>& node_ids, const QVector<QPointF>& old_pos, const QVector<QPointF>& new_pos)
{
    std::vector<std::shared_ptr<MoveAction>> move_actions;
    move_actions.reserve(node_ids.size());
    for (int i = 0; i < node_ids.size(); ++i)
    {
        auto action = on_node_moved(node_ids[i], old_pos[i], new_pos[i]);
        if (action)
            move_actions.emplace_back(std::move(action));
    }

    auto cmd = std::make_shared<MoveItemsCommand>(*this, std::move(move_actions));

    CommandInterface::finalize(cmd, CommandArgs());
}

std::unique_ptr<MoveAction> UsdGraphModel::on_node_moved(const NodeId& node_id, const QPointF& old_pos, const QPointF& new_pos)
{
    return std::make_unique<UsdMoveAction>(get_node_provider().get_watcher(), *this, node_id, new_pos);
}

void UsdGraphModel::on_node_resized(const std::string& node_id, const float old_width, const float old_height, const float new_width,
                                    const float new_height)
{
    if (!get_node_provider().get_stage())
        return;

    static const auto startup_width = 1;
    if (old_width == startup_width || (old_width == new_width && old_height == new_height))
        return;

    auto prim = UsdUINodeGraphNodeAPI(get_prim_for_node(node_id));
    assert(prim);

    UndoCommandBlock block("Resize node");
    commands::UsdEditsUndoBlock undo_block;
    prim.CreateSizeAttr(VtValue(GfVec2f(new_width, new_height)));

    if (old_width == new_width)
        return;

    // We need to balance the value of the pos attribute after changing the width of the Backdrop
    // because the position on the scene depends on the changing width value.
    auto new_model_pos = get_node_position(node_id) * ((int)old_width / (float)((int)new_width));
    prim.CreatePosAttr(VtValue(GfVec2f(new_model_pos.x(), new_model_pos.y())));
}

void UsdGraphModel::on_selection_set(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections)
{
    if (!get_node_provider().get_stage())
        return;
    m_updating_selection = true;
    SelectionList sel_list;
    for (const auto& node_id : nodes)
    {
        if (SdfPath::IsValidPathString(node_id))
            sel_list.set_full_selection(SdfPath(node_id), true);
    }
    CommandInterface::execute("select", CommandArgs().arg(std::move(sel_list)));
    m_updating_selection = false;
}

bool UsdGraphModel::can_connect(const Port& start_port, const Port& end_port) const
{
    auto stage = get_node_provider().get_stage();
    if (!stage)
        return false;

    if (start_port.type == end_port.type)
        return false;

    if (!SdfPath::IsValidPathString(start_port.id) || !SdfPath::IsValidPathString(end_port.id))
        return false;

    const auto start_path = SdfPath(start_port.id);
    const auto end_path = SdfPath(end_port.id);
    auto start_prim = stage->GetPrimAtPath(start_path.GetPrimPath());
    if (!start_prim)
        return false;
    auto end_prim = stage->GetPrimAtPath(end_path.GetPrimPath());
    if (!end_prim)
        return false;
    UsdPrimFallbackProxy start_prim_proxy(start_prim);
    UsdPrimFallbackProxy end_prim_proxy(end_prim);

    auto start_prop = start_prim_proxy.get_property_proxy(start_path.GetNameToken());
    auto end_prop = end_prim_proxy.get_property_proxy(end_path.GetNameToken());
    if (!start_prop || !end_prop)
        return false;

    if (start_path.IsPrimPath())
    {
        // Incoming connections to prims.
        if (start_port.type == Port::Type::Input)
            return false;
        // Prim-to-prim.
        if (end_path.IsPrimPath())
            return false;

        if (!end_prim_proxy.get_property_proxy(end_path.GetNameToken()))
            return false;

        return true;
    }

    if (!start_prim_proxy.get_property_proxy(start_path.GetNameToken()))
        return false;

    if (end_path.IsPrimPath())
    {
        // Incoming connections to prims
        if (end_port.type == Port::Type::Input)
            return false;
        return true;
    }

    if (!end_prim_proxy.get_property_proxy(end_path.GetNameToken()))
        return false;

    return true;
}

bool UsdGraphModel::connect_ports(const Port& start_port, const Port& end_port)
{
    if (!can_connect(start_port, end_port))
        return false;

    auto stage = get_node_provider().get_stage();
    const auto start_path = SdfPath(start_port.id);
    const auto end_path = SdfPath(end_port.id);
    auto start_prim = stage->GetPrimAtPath(start_path.GetPrimPath());
    auto end_prim = stage->GetPrimAtPath(end_path.GetPrimPath());
    auto start_prim_proxy = UsdPrimFallbackProxy(start_prim);
    auto end_prim_proxy = UsdPrimFallbackProxy(end_prim);
    const auto start_prop = start_prim_proxy.get_property_proxy(start_path.GetNameToken());
    const auto end_prop = end_prim_proxy.get_property_proxy(end_path.GetNameToken());
    commands::UsdEditsUndoBlock block;

    // todo fallback proxy api for connections
    // hack get/set to make an attribute on stage
    // set both start and end ports just in case
    if (start_port.type == Port::Type::Input)
    {
        VtValue val;
        if (!start_prop->is_authored())
        {
            if (!start_prop->get(&val))
            {
                start_prop->get_default(&val);
            }
            start_prop->set(val);
        }

        if (start_prop->get_attribute())
            start_prop->get_attribute().AddConnection(end_path);
        else
            start_prop->get_relationship().AddTarget(end_path);
    }
    else
    {
        VtValue val;
        if (!end_prop->is_authored())
        {
            if (!end_prop->get(&val))
            {
                end_prop->get_default(&val);
            }
            end_prop->set(val);
        }

        if (end_prop->get_attribute())
            end_prop->get_attribute().AddConnection(start_path);
        else
            end_prop->get_relationship().AddTarget(start_path);
    }
    return true;
}

bool UsdGraphModel::has_port(const PortId& port) const
{
    if (!get_stage())
        return false;
    auto prim = get_prim_for_node(get_node_id_from_port(port));
    if (!prim)
        return false;
    UsdPrimFallbackProxy proxy(prim);
    auto prop = proxy.get_property_proxy(TfToken(get_property_name(port)));
    return prop != nullptr;
}

void UsdGraphModel::block_usd_notifications(bool block)
{
    get_node_provider().block_notifications(block);
}

UsdPrim UsdGraphModel::create_usd_prim(const TfToken& name, const TfToken& type, const SdfPath& parent_path, bool change_selection)
{
    using base::commands::CommandResult;

    auto stage = get_node_provider().get_stage();
    if (!stage)
        return UsdPrim();

    const auto result = CommandInterface::execute(
        "create_prim", CommandArgs().arg(name).arg(type).kwarg("parent", parent_path).kwarg("change_selection", change_selection));

    if (result.get_status() == CommandResult::Status::SUCCESS)
        return stage->GetPrimAtPath(*result.get_result<SdfPath>());
    return UsdPrim();
}

NodeId UsdGraphModel::get_node_path(const PortId& port_id)
{
    return port_id.substr(0, port_id.rfind('.'));
}

std::vector<ConnectionId> UsdGraphModel::get_connections_for_prim(const UsdPrim& prim) const
{
    assert(prim);
    const auto props = prim.GetAuthoredProperties();
    std::vector<ConnectionId> result;
    result.reserve(props.size());
    for (const auto& prop : props)
    {
        SdfPathVector targets;
        if (prop.Is<UsdRelationship>())
            prop.As<UsdRelationship>().GetTargets(&targets);
        else
            prop.As<UsdAttribute>().GetConnections(&targets);

        for (const auto& target : targets)
            result.emplace_back(ConnectionId { target.GetString(), prop.GetPath().GetString() });
    }
    return std::move(result);
}

void UsdEditorGraphModel::on_selection_changed()
{
    const auto sel_paths = Application::instance().get_prim_selection();
    QVector<NodeId> nodes;
    nodes.reserve(sel_paths.size());
    for (const auto path : sel_paths)
    {
        if (m_nodes.find(path) != m_nodes.end())
            nodes.push_back(path.GetString());
    }
    Q_EMIT selection_changed(std::move(nodes), QVector<ConnectionId>());
}

NodeId UsdEditorGraphModel::get_node_id_from_port(const PortId& port) const
{
    return SdfPath(port).GetPrimPath().GetString();
}

UsdPrim UsdGraphModel::get_prim_for_node(const NodeId& node_id) const
{
    auto stage = get_node_provider().get_stage();
    if (!stage || !SdfPath::IsValidPathString(node_id))
        return UsdPrim();

    return stage->GetPrimAtPath(SdfPath(node_id));
}

UsdStageRefPtr UsdGraphModel::get_stage() const
{
    return get_node_provider().get_stage();
}

bool UsdGraphModel::can_rename(const NodeId& old_name, const NodeId& new_name) const
{
    auto stage = get_node_provider().get_stage();
    if (!stage)
        return false;

    const auto old_path = SdfPath(old_name);
    const auto new_path = SdfPath(new_name);
    if (old_path.GetParentPath() != new_path.GetParentPath())
        return false;

    return !stage->GetPrimAtPath(new_path);
}

bool UsdGraphModel::rename(const NodeId& old_name, const NodeId& new_name) const
{
    if (!can_rename(old_name, new_name))
        return false;

    const auto old_path = SdfPath(old_name);
    const auto new_path = SdfPath(new_name);
    const auto result = CommandInterface::execute("rename_prim", CommandArgs().arg(new_path.GetNameToken()).kwarg("path", old_path));
    return true;
}

PXR_NS::SdfPath UsdEditorGraphModel::to_usd_path(const PortId& node_id) const
{
    return SdfPath(node_id);
}

NodeId UsdEditorGraphModel::from_usd_path(const PXR_NS::SdfPath& path, const PXR_NS::SdfPath& root) const
{
    return path.GetString();
}

void UsdEditorGraphModel::set_root(const PXR_NS::SdfPath& path) {}

PXR_NS::SdfPath UsdEditorGraphModel::get_root() const
{
    return SdfPath::AbsoluteRootPath();
}

void UsdEditorGraphModel::on_rename() {}

UsdEditorGraphModel::UsdEditorGraphModel(QObject* parent /*= nullptr*/)
    : UsdGraphModel(parent)
{
}

void UsdEditorGraphModel::stage_changed_impl()
{
    m_nodes.clear();
    Q_EMIT model_reset();
}

QVector<NodeId> UsdEditorGraphModel::get_nodes() const
{
    QVector<NodeId> result(m_nodes.size());
    std::transform(m_nodes.begin(), m_nodes.end(), result.begin(), [](const SdfPath& p) { return p.GetString(); });
    return std::move(result);
}

void UsdEditorGraphModel::add_node_to_graph(const PXR_NS::SdfPath& path)
{
    auto stage = get_stage();
    if (!stage)
        return;

    std::queue<SdfPath> prims;
    prims.push(path);

    std::vector<NodeId> node_ids;
    std::vector<ConnectionId> connection_ids;
    while (!prims.empty())
    {
        const auto cur_path = prims.front();
        prims.pop();
        if (m_nodes.find(cur_path) != m_nodes.end())
            continue;

        auto prim = stage->GetPrimAtPath(cur_path);
        if (!prim)
            continue;

        auto connections = get_connections_for_prim(prim);
        for (const auto& con : connections)
        {
            if (SdfPath(con.start_port).GetPrimPath() != cur_path)
                prims.push(SdfPath(con.start_port).GetPrimPath());
            else if (SdfPath(con.end_port).GetPrimPath() != cur_path)
                prims.push(SdfPath(con.end_port).GetPrimPath());

            connection_ids.push_back(con);
            m_connections_cache.emplace(con);
        }

        node_ids.push_back(cur_path.GetString());
        m_nodes.emplace(cur_path.GetString());
    }
    for (const auto& node : node_ids)
        Q_EMIT node_created(node);
    for (const auto& con : connection_ids)
        Q_EMIT connection_created(con);
}

void UsdEditorGraphModel::remove_node_from_graph(const PXR_NS::SdfPath& path)
{
    auto node_iter = m_nodes.find(path);
    if (node_iter == m_nodes.end())
        return;

    auto connections = get_connections_for_node(path.GetString());
    for (const auto connection : connections)
    {
        m_connections_cache.erase(connection);
        Q_EMIT connection_removed(connection);
    }

    m_nodes.erase(node_iter);
    Q_EMIT node_removed(path.GetString());
}

QVector<ConnectionId> UsdEditorGraphModel::get_connections_for_node(const NodeId& node_id) const
{
    QVector<ConnectionId> result;
    const auto node = SdfPath(node_id);
    for (const auto& connection : m_connections_cache)
    {
        if (SdfPath(connection.start_port).GetPrimPath() == node || SdfPath(connection.end_port).GetPrimPath() == node)
            result.push_back(connection);
    }
    return std::move(result);
}

void UsdEditorGraphModel::delete_connection(const ConnectionId& connection)
{
    if (!get_stage())
        return;

    auto prop = get_stage()->GetPropertyAtPath(SdfPath(connection.end_port));
    if (!prop)
        return;
    if (prop.Is<UsdAttribute>())
        prop.As<UsdAttribute>().RemoveConnection(SdfPath(connection.start_port));
    else
        prop.As<UsdRelationship>().RemoveTarget(SdfPath(connection.start_port));
    m_connections_cache.erase(connection);
    Q_EMIT connection_removed(connection);
}

void UsdEditorGraphModel::remove(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections)
{
    if (!get_stage())
        return;

    {
        commands::UsdEditsUndoBlock block;
        SdfChangeBlock change_block;
        block_usd_notifications(true);
        for (const auto& connection : connections)
            delete_connection(connection);

        for (const auto& node : nodes)
        {
            for (const auto& connection : get_connections_for_node(node))
                delete_connection(connection);

            auto prim_path = SdfPath(node);
            get_stage()->RemovePrim(prim_path);
            m_nodes.erase(prim_path);
            Q_EMIT node_removed(node);
        }
        block_usd_notifications(false);
    }
}

QVector<ConnectionId> UsdEditorGraphModel::get_connections() const
{
    QVector<ConnectionId> result(m_connections_cache.size());
    std::transform(m_connections_cache.begin(), m_connections_cache.end(), result.begin(), [](const ConnectionId& connection) { return connection; });
    return std::move(result);
}

TfToken UsdEditorGraphModel::get_expansion_state(const NodeId& node) const
{
    auto it = m_expansion_state_cache.find(node);
    if (it == m_expansion_state_cache.end())
        return UsdUITokens->open;
    return it->second;
}

void UsdEditorGraphModel::set_expansion_state(const NodeId& node, PXR_NS::TfToken expansion_state)
{
    auto it = m_expansion_state_cache.find(node);
    if (it != m_expansion_state_cache.end() && it->second == expansion_state)
        return;

    m_expansion_state_cache[node] = expansion_state;
}
//
// void UsdEditorGraphModel::on_prop_changed(const PXR_NS::SdfPath& prop_path)
//{
//    assert(get_stage());
//    assert(prop_path.IsPropertyPath());
//
//    const auto prop = get_stage()->GetPropertyAtPath(prop_path);
//    assert(prop);
//
//    SdfPathVector connections;
//    if (prop.Is<UsdAttribute>())
//        prop.As<UsdAttribute>().GetConnections(&connections);
//    else
//        prop.As<UsdRelationship>().GetTargets(&connections);
//
//    std::unordered_set<SdfPath, SdfPath::Hash> target_set(connections.begin(), connections.end());
//    for (auto it = m_connections_cache.begin(); it != m_connections_cache.end();)
//    {
//        // If has incoming connection that are no longer exists
//        if (SdfPath(it->end_port) == prop_path && target_set.find(SdfPath(it->start_port)) == target_set.end())
//        {
//            const auto removed_connection = *it;
//            it = m_connections_cache.erase(it);
//            Q_EMIT connection_removed(removed_connection);
//        }
//        ++it;
//    }
//
//    for (const auto target : target_set)
//    {
//        const auto result = m_connections_cache.emplace(ConnectionId{ target.GetString(), prop_path.GetString() });
//        if (result.second)
//            Q_EMIT connection_created(*result.first);
//    }
//    Q_EMIT port_updated(prop_path.GetString());
//}
//
// void UsdEditorGraphModel::on_prim_resynced(const SdfPath& prim_path)
//{
//    assert(get_stage());
//    assert(prim_path.IsPrimPath());
//
//    const NodeId node_id = prim_path.GetString();
//    const auto prim = get_stage()->GetPrimAtPath(prim_path);
//    auto node_iter = m_nodes.find(prim_path);
//    if (prim && node_iter == m_nodes.end())
//    {
//        auto connections = get_connections_for_prim(prim);
//        for (const auto& con : connections)
//        {
//            if (m_nodes.find(con.input.GetPrimPath()) != m_nodes.end() ||
//                m_nodes.find(con.output.GetPrimPath()) != m_nodes.end())
//            {
//                add_node_to_graph(prim_path);
//                return;
//            }
//        }
//        return;
//    }
//
//    if (!prim && node_iter != m_nodes.end())
//    {
//        std::vector<ConnectionId> removed_connections;
//        for (auto it = m_connections_cache.begin(); it != m_connections_cache.end();)
//        {
//            if (SdfPath(it->start_port).GetPrimPath() == prim_path || SdfPath(it->end_port).GetPrimPath() == prim_path)
//            {
//                removed_connections.emplace_back(*it);
//                it = m_connections_cache.erase(it);
//            }
//            else
//            {
//                ++it;
//            }
//        }
//
//        m_nodes.erase(prim_path);
//        for (const auto& connection : removed_connections)
//            Q_EMIT connection_removed(connection);
//
//        Q_EMIT node_removed(node_id);
//    }
//}
//
// void UsdEditorGraphModel::on_prop_resynced(const SdfPath& prop_path)
//{
//    assert(get_stage());
//    assert(prop_path.IsPropertyPath());
//
//    const auto prop = get_stage()->GetPropertyAtPath(prop_path);
//    if (prop && prop.IsAuthored())
//    {
//        SdfPathVector connections;
//        if (prop.Is<UsdAttribute>())
//            prop.As<UsdAttribute>().GetConnections(&connections);
//        else
//            prop.As<UsdRelationship>().GetTargets(&connections);
//
//        auto source_prim_path = prop_path.GetPrimPath();
//        if (m_nodes.find(source_prim_path) == m_nodes.end())
//        {
//            for (const auto target : connections)
//            {
//                if (m_nodes.find(target.GetPrimPath()) != m_nodes.end())
//                {
//                    add_node_to_graph(target.GetPrimPath());
//                    break;
//                }
//            }
//            return;
//        }
//
//        std::vector<ConnectionId> new_connections;
//        new_connections.reserve(connections.size());
//        for (const auto target : connections)
//        {
//            if (m_nodes.find(target.GetPrimPath()) == m_nodes.end())
//            {
//                add_node_to_graph(prop_path);
//                continue;
//            }
//
//            const auto result = m_connections_cache.emplace(ConnectionId{ target.GetString(), prop_path.GetString() });
//            if (result.second)
//                new_connections.push_back(*result.first);
//        }
//        Q_EMIT port_updated(prop.GetPath().GetString());
//        for (const auto& connection : new_connections)
//            Q_EMIT connection_created(connection);
//
//        return;
//    }
//
//    for (auto it = m_connections_cache.begin(); it != m_connections_cache.end();)
//    {
//        if (SdfPath(it->start_port) == prop_path || SdfPath(it->end_port) == prop_path)
//        {
//            const auto removed_connection = *it;
//            it = m_connections_cache.erase(it);
//            Q_EMIT connection_removed(removed_connection);
//        }
//        else
//        {
//            ++it;
//        }
//    }
//
//    Q_EMIT port_updated(prop.GetPath().GetString());
//}

void UsdEditorGraphModel::try_add_prim(const PXR_NS::SdfPath& prim_path) {}

void UsdEditorGraphModel::try_remove_prim(const PXR_NS::SdfPath& prim_path) {}

void UsdEditorGraphModel::try_update_prop(const PXR_NS::SdfPath& prop_path) {}

OPENDCC_NAMESPACE_CLOSE
