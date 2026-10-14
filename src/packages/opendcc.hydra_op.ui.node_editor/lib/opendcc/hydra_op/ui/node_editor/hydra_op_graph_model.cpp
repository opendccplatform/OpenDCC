// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/ui/node_editor/hydra_op_graph_model.h"
#include "opendcc/hydra_op/schema/nodegraph.h"

#include "pxr/usd/usdUI/tokens.h"
#include "pxr/usd/usdUI/backdrop.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/usd_editor/usd_node_editor/utils.h"
#include "usd_fallback_proxy/core/usd_prim_fallback_proxy.h"

#include "opendcc/hydra_op/schema/merge.h"
#include "opendcc/hydra_op/schema/translateAPI.h"

#include <opendcc/app/viewport/viewport_widget.h>
#include "opendcc/hydra_op/session.h"
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <opendcc/hydra_op/schema/group.h>

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN

OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

struct CallbackHandlers
{
    HydraOpSession::Handle terminal_node_changed;
    HydraOpGraphModel& model;

    CallbackHandlers(HydraOpGraphModel& model)
        : model(model)
    {
        terminal_node_changed = HydraOpSession::instance().register_event_handler(
            HydraOpSession::EventType::ViewNodeChanged, [this] { update_terminal_node(HydraOpSession::instance().get_view_node().GetString()); });
    }
    void update_terminal_node(const NodeId& node)
    {
        if (node != model.m_terminal_node.GetString())
        {
            model.m_terminal_node = node.empty() ? SdfPath::EmptyPath() : SdfPath(node);
            Q_EMIT model.terminal_node_changed(node);
        }
    }

    ~CallbackHandlers() { HydraOpSession::instance().unregister_event_handler(HydraOpSession::EventType::ViewNodeChanged, terminal_node_changed); }
};

class NodegraphNodeMoveAction : public MoveAction
{
public:
    NodegraphNodeMoveAction(HydraOpGraphModel& model, const QPointF& old_pos, const QPointF& new_pos, const NodeId& node_id)
        : m_model(model)
        , m_old_pos(old_pos)
        , m_new_pos(new_pos)
        , m_node_id(node_id)
    {
        redo();
    }

    void undo() override { m_model.move_nodegraph_node(m_node_id, m_old_pos); }

    void redo() override { m_model.move_nodegraph_node(m_node_id, m_new_pos); }

private:
    HydraOpGraphModel& m_model;
    QPointF m_old_pos;
    QPointF m_new_pos;
    NodeId m_node_id;
};

namespace
{
    bool is_descendant(SdfPath root, SdfPath target)
    {
        return target.GetPrimPath() == root || target.GetPrimPath().GetParentPath() == root;
    }

    std::string make_tagged_path(const PortId& port_id, const std::string& tag)
    {
        auto pos = port_id.rfind('.');
        std::string result = port_id.substr(0, pos);
        result += tag;
        if (pos != std::string::npos)
            result += port_id.substr(pos);
        return std::move(result);
    }
    bool is_add_port(const PortId& port_id)
    {
        return TfStringEndsWith(port_id, "#add_in_port") || TfStringEndsWith(port_id, "#add_out_port");
    }

    bool is_input_node(const NodeId& node_id)
    {
        return TfStringContains(node_id, "#graph_in");
    }
    bool is_output_node(const NodeId& node_id)
    {
        return TfStringEndsWith(node_id, "#graph_out");
    }

};
UsdPrim HydraOpGraphModel::create_usd_prim(const TfToken& name, const TfToken& type, const SdfPath& parent_path, bool change_selection)
{
    auto result = UsdGraphModel::create_usd_prim(name, type, parent_path, change_selection);
    const auto supported_translate_api = is_supported_type_for_translate_api(type);
    if (result && supported_translate_api)
    {
        result.ApplyAPI<UsdHydraOpTranslateAPI>();
        // TODO: Look its good idea to move all that logic to python
        auto translate_api = UsdHydraOpTranslateAPI(result);
        if (supported_translate_api)
        {
            translate_api.CreateHydraOpPathAttr(VtValue(translate_api.GetPath().GetString()));
        }
    }
    return result;
}

std::string HydraOpGraphModel::get_property_name(const PortId& port_id) const
{
    auto delimiter = port_id.rfind('.');
    if (delimiter == std::string::npos)
        return std::string();
    return port_id.substr(delimiter + 1);
}
NodeId HydraOpGraphModel::from_usd_path(const SdfPath& path, const SdfPath& root) const
{
    if (path.GetPrimPath() != root)
        return path.GetString();

    const auto target_inputs = get_input_names(path.GetPrimPath().GetString());
    const auto is_input = std::find(target_inputs.begin(), target_inputs.end(), path.GetName()) != target_inputs.end();

    const auto tag = is_input ? "#graph_in" : "#graph_out";

    std::string result;
    result += path.GetPrimPath().GetString() + tag;

    if (!path.IsPrimPath())
    {
        if (is_input)
        {
            result += "_" + path.GetName();
        }

        result += "." + path.GetName();
    }
    return std::move(result);
}

PXR_NS::SdfPath HydraOpGraphModel::to_usd_path(const PortId& node_id) const
{
    auto pos = node_id.rfind('#');
    if (pos == std::string::npos)
        return SdfPath(node_id);

    // if phantom property
    if (pos > 0 && node_id[pos - 1] == '.')
    {
        return SdfPath(node_id.substr(0, pos - 1));
    }
    else
    {

        SdfPath result(node_id.substr(0, pos));
        auto prop_delim = node_id.rfind('.');
        if (prop_delim != std::string::npos && prop_delim > pos)
            result = result.AppendProperty(TfToken(node_id.substr(prop_delim + 1)));
        return result;
    }
}
TfToken HydraOpGraphModel::get_expansion_state(const NodeId& node) const
{
    return UsdUITokens->open;
}

NodeId HydraOpGraphModel::get_node_id_from_port(const PortId& port) const
{
    auto pos = port.rfind('#');
    if (pos != std::string::npos)
        return port.substr(0, port.rfind('.'));
    return SdfPath(port).GetPrimPath().GetString();
}

bool HydraOpGraphModel::set_terminal_node(const NodeId& node_id)
{
    auto usd_node = to_usd_path(node_id);
    if (usd_node == m_terminal_node)
        return true;

    // TERMINAL_NODE_CHANGED callback will set all necessary data and send a signal
    HydraOpSession::instance().set_view_node(to_usd_path(node_id));
    return true;
}

bool HydraOpGraphModel::set_bypass(const NodeId& node_id, bool value)
{
    if (auto prim = get_prim_for_node(node_id))
    {
        auto attr = prim.GetAttribute(UsdHydraOpTokens->inputsBypass);
        if (!attr)
        {
            attr = prim.GetAttribute(UsdHydraOpTokens->hydraOpBypass);
        }
        if (!attr)
        {
            return false;
        }
        return attr.Set(value);
    }
    return false;
}

NodeId HydraOpGraphModel::get_terminal_node() const
{
    return m_terminal_node.GetString();
}

void HydraOpGraphModel::set_expansion_state(const NodeId& node, PXR_NS::TfToken expansion_state)
{
    return;
}

bool HydraOpGraphModel::connect_ports(const Port& start_port, const Port& end_port)
{
    if (!can_connect(start_port, end_port))
        return false;
    auto start_prim = get_prim_for_node(get_node_id_from_port(start_port.id));
    auto end_prim = get_prim_for_node(get_node_id_from_port(end_port.id));

    auto start_prim_proxy = UsdPrimFallbackProxy(start_prim);
    auto end_prim_proxy = UsdPrimFallbackProxy(end_prim);

    auto start_prop = start_prim_proxy.get_property_proxy(TfToken(get_property_name(start_port.id)));
    auto end_prop = end_prim_proxy.get_property_proxy(TfToken(get_property_name(end_port.id)));

    auto handle_add_port = [this](const UsdPrim& start_prim, const UsdPropertyProxyPtr& end_prop, const Port& start_port, const Port& end_port,
                                  Port& new_start, Port& new_end) {
        if (!has_add_port(start_prim))
        {
            return false;
        }

        if (!is_add_port(start_port.id))
        {
            return false;
        }

        if (UsdHydraOpGroup(start_prim))
        {
            const auto new_name = commands::utils::get_new_name(TfToken("inputs:in"), start_prim.GetPropertyNames());
            if (auto attr = start_prim.CreateAttribute(new_name, SdfValueTypeNames->Token))
            {
                new_start.id = attr.GetPath().GetString();
                new_start.type = start_port.type;

                new_end.id = to_usd_path(end_port.id).GetString();
                new_end.type = end_port.type;
                return true;
            }
        }
        if (UsdHydraOpMerge(start_prim))
        {
            new_start.id = start_prim.GetPath().AppendProperty(UsdHydraOpTokens->inputsIn).GetString();
            new_start.type = start_port.type;
            new_end.id = to_usd_path(end_port.id).GetString();
            new_end.type = end_port.type;
            return true;
        }

        return false;
    };

    Port new_start_port, new_end_port;

    commands::UsdEditsUndoBlock block;
    if (!handle_add_port(start_prim, end_prop, start_port, end_port, new_start_port, new_end_port) &&
        !handle_add_port(end_prim, start_prop, end_port, start_port, new_end_port, new_start_port))
    {
        new_start_port.id = to_usd_path(start_port.id).GetString();
        new_start_port.type = start_port.type;

        new_end_port.id = to_usd_path(end_port.id).GetString();
        new_end_port.type = end_port.type;
    }

    // remove existing connections
    const auto author_start_port = start_port.type == Port::Type::Input;
    auto prop = author_start_port ? start_prop : end_prop;
    if (prop && prop->is_authored())
    {
        if (prop->get_relationship())
            prop->get_relationship().ClearTargets(false);
    }

    auto connect_result = UsdGraphModel::connect_ports(new_start_port, new_end_port);
    if (connect_result)
    {
        // due to hydra specific we must ensure that both properties are authored.
        UsdPropertyProxyPtr prop;
        if (author_start_port)
            prop = end_prop;
        else
            prop = start_prop;

        if (prop && !prop->is_authored())
        {
            VtValue def_val;
            prop->get(&def_val);
            prop->set(def_val);
        }
    }
    return connect_result;
}

QPointF HydraOpGraphModel::get_node_position(const NodeId& node_id) const
{
    if (is_input_node(node_id) || is_output_node(node_id))
    {
        auto pos = m_graph_pos_cache.find(node_id);
        if (pos != m_graph_pos_cache.end())
            return pos->second;
        return QPointF(0, 0);
    }
    return UsdGraphModel::get_node_position(node_id);
}

bool HydraOpGraphModel::is_node_bypassed(const NodeId& node_id) const
{
    auto prim = get_prim_for_node(node_id);
    assert(prim);
    bool value = false;
    auto attr = prim.GetAttribute(UsdHydraOpTokens->inputsBypass);
    if (!attr)
    {
        attr = prim.GetAttribute(UsdHydraOpTokens->hydraOpBypass);
    }
    if (attr && attr.Get(&value))
        return value;
    return false;
}

bool HydraOpGraphModel::has_add_port(const UsdPrim& node_prim) const
{
    return !!UsdHydraOpGroup(node_prim);
}

void HydraOpGraphModel::add_input(const NodeId& node_id, const std::string& port_name)
{
    auto usd_node = to_usd_path(node_id);
    auto stage = get_stage();
    if (!stage)
    {
        return;
    }

    auto prim = stage->GetPrimAtPath(usd_node);
    if (!prim || !has_add_port(prim))
    {
        return;
    }

    commands::UsdEditsUndoBlock block;
    SdfChangeBlock change_block;

    const auto new_name = commands::utils::get_new_name(TfToken(port_name), prim.GetPropertyNames());
    prim.CreateAttribute(new_name, SdfValueTypeNames->Token);
}

std::unique_ptr<MoveAction> HydraOpGraphModel::on_node_moved(const NodeId& node_id, const QPointF& old_pos, const QPointF& new_pos)
{
    if (is_input_node(node_id) || is_output_node(node_id))
        return std::make_unique<NodegraphNodeMoveAction>(*this, old_pos, new_pos, node_id);
    else
        return UsdGraphModel::on_node_moved(node_id, old_pos, new_pos);
}

void HydraOpGraphModel::toggle_node_bypass(const NodeId& node_id)
{
    auto value = is_node_bypassed(node_id);
    set_bypass(node_id, !value);
}

bool HydraOpGraphModel::has_port(const PortId& port) const
{
    return UsdGraphModel::has_port(port);
}

HydraOpGraphModel::HydraOpGraphModel(QObject* parent)
    : UsdGraphModel(parent)
{
    connect(this, &GraphModel::node_created, this, [this](const NodeId& node) { get_graph_cache().nodes.insert(node); });
    connect(this, &GraphModel::node_removed, this, [this](const NodeId& node) { get_graph_cache().nodes.erase(node); });
    m_handlers = std::make_unique<CallbackHandlers>(*this);
    stage_changed_impl();
}

HydraOpGraphModel::~HydraOpGraphModel() {}

std::vector<std::string> HydraOpGraphModel::get_input_names(const NodeId& node_id) const
{
    auto prim = get_stage()->GetPrimAtPath(to_usd_path(node_id));
    if (UsdHydraOpGroup(prim))
    {
        std::vector<std::string> result;
        for (const auto& prop : prim.GetPropertiesInNamespace("inputs"))
        {
            const auto name = prop.GetName().GetString();
            if (prop.Is<UsdAttribute>() && TfStringStartsWith(name, "inputs:in"))
            {
                result.push_back(prop.GetName());
            }
        }
        return result;
    }

    if (prim.HasAttribute(UsdHydraOpTokens->inputsIn))
    {
        return { "inputs:in" };
    }
    else if (prim.HasAttribute(UsdHydraOpTokens->hydraOpIn))
    {
        return { "hydraOp:in" };
    }

    return {};
}

std::vector<std::string> HydraOpGraphModel::get_output_names(const NodeId& node_id) const
{
    auto prim = get_stage()->GetPrimAtPath(to_usd_path(node_id));
    if (UsdHydraOpTranslateAPI(prim))
    {
        return { UsdHydraOpTokens->hydraOpOut.GetString() };
    }

    return { UsdHydraOpTokens->outputsOut.GetString() };
}

bool HydraOpGraphModel::can_connect(const Port& start_port, const Port& end_port) const
{
    auto stage = get_stage();
    if (!stage)
        return false;
    if (start_port.type == end_port.type)
        return false;

    if (is_add_port(start_port.id) && is_add_port(end_port.id))
        return false;

    if (is_add_port(start_port.id) || is_add_port(end_port.id))
        return true;

    const auto start_path = to_usd_path(start_port.id);
    const auto end_path = to_usd_path(end_port.id);
    auto start_prim = stage->GetPrimAtPath(start_path.GetPrimPath());
    if (!start_prim)
        return false;
    auto end_prim = stage->GetPrimAtPath(end_path.GetPrimPath());
    if (!end_prim)
        return false;
    if (start_prim == end_prim)
        return false;
    UsdPrimFallbackProxy start_prim_proxy(start_prim);
    UsdPrimFallbackProxy end_prim_proxy(end_prim);
    auto start_prop = start_prim_proxy.get_property_proxy(start_path.GetNameToken());
    auto end_prop = end_prim_proxy.get_property_proxy(end_path.GetNameToken());

    if (start_prop && end_prop && start_prop->get_attribute() && end_prop->get_attribute())
    {
        return start_port.type != end_port.type;
    }
    return false;
}

bool HydraOpGraphModel::is_supported_type_for_translate_api(const PXR_NS::TfToken& type)
{
    static const std::unordered_set<TfToken, TfToken::HashFunctor> allowed_translate_api_types = {
        TfToken("Camera"),       TfToken("Material"),  TfToken("RenderSettings"), TfToken("DiskLight"),
        TfToken("DistantLight"), TfToken("DomeLight"), TfToken("GeometryLight"),  TfToken("LightFilter"),
        TfToken("PortalLight"),  TfToken("RectLight"), TfToken("SphereLight")
    };
    return allowed_translate_api_types.find(type) != allowed_translate_api_types.end();
}

void HydraOpGraphModel::on_selection_set(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections)
{
    QVector<NodeId> resolved_nodes;
    resolved_nodes.reserve(nodes.size());
    for (const auto& node : nodes)
        resolved_nodes.push_back(to_usd_path(node).GetString());
    UsdGraphModel::on_selection_set(resolved_nodes, connections);
}

void HydraOpGraphModel::delete_connection(const ConnectionId& connection)
{
    if (!get_stage())
        return;

    auto prop = get_stage()->GetPropertyAtPath(to_usd_path(connection.end_port));
    if (!prop)
        return;

    remove_connection(prop, to_usd_path(connection.start_port));

    if (get_graph_cache().connections.erase(connection))
    {
        Q_EMIT connection_removed(connection);
    }
}

void HydraOpGraphModel::remove(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections)
{
    if (!get_stage())
        return;

    const auto cur_view_node = HydraOpSession::instance().get_view_node();
    {
        commands::UsdEditsUndoBlock block;
        SdfChangeBlock change_block;
        block_usd_notifications(true);
        for (const auto& connection : connections)
            delete_connection(connection);

        for (const auto& node : nodes)
        {
            if (is_input_node(node) || is_output_node(node))
                continue;

            for (const auto& connection : get_connections_for_node(node))
                delete_connection(connection);

            auto prim_path = to_usd_path(node);
            if (cur_view_node == prim_path)
            {
                HydraOpSession::instance().set_view_node(SdfPath::EmptyPath());
            }
            if (get_stage()->RemovePrim(prim_path))
                Q_EMIT node_removed(node);
        }
        block_usd_notifications(false);
    }
}

void HydraOpGraphModel::set_root(const PXR_NS::SdfPath& root)
{
    if (m_root == root)
    {
        if (get_stage() && get_stage()->HasAuthoredMetadata(TfToken("hydraOpNodegraphPrimPath")))
        {
            std::string nodegraph_path;
            get_stage()->GetMetadata(TfToken("hydraOpNodegraphPrimPath"), &nodegraph_path);
            if (!nodegraph_path.empty() && nodegraph_path == root.GetString())
            {
                return;
            }
            else
            {
                get_stage()->SetMetadata(TfToken("hydraOpNodegraphPrimPath"), VtValue(m_root.GetString()));
            }
        }

        return;
    }

    if (!can_be_root(from_usd_path(root, m_root)) || !get_stage())
    {
        m_root = SdfPath::EmptyPath();
    }
    else
    {
        m_root = root;
    }

    if (!m_root.IsEmpty() && !get_stage()->HasAuthoredMetadata(TfToken("hydraOpNodegraphPrimPath")))
    {
        get_stage()->SetMetadata(TfToken("hydraOpNodegraphPrimPath"), VtValue(m_root.GetString()));
    }

    init_scene_graph();

    m_terminal_node = SdfPath();
    if (get_stage())
    {
        if (auto graph_prim = get_stage()->GetPrimAtPath(m_root))
        {
            m_terminal_node = HydraOpSession::instance().get_view_node();
        }
    }

    Q_EMIT model_reset();
}

PXR_NS::SdfPath HydraOpGraphModel::get_root() const
{
    return m_root;
}

void HydraOpGraphModel::stage_changed_impl()
{
    auto stage = get_stage();
    if (!stage)
    {
        set_root(SdfPath::EmptyPath());
        return;
    }

    // reset cache
    m_root = SdfPath::EmptyPath();
    m_graph_pos_cache.clear();
    init_scene_graph();

    std::string nodegraph_path;
    if (stage->GetMetadata(TfToken("hydraOpNodegraphPrimPath"), &nodegraph_path) && !nodegraph_path.empty())
    {
        if (can_be_root(nodegraph_path))
        {
            set_root(SdfPath(nodegraph_path));
            return;
        }
    }

    m_terminal_node = HydraOpSession::instance().get_view_node();

    Q_EMIT model_reset();
}

void HydraOpGraphModel::init_scene_graph()
{
    get_graph_cache().connections.clear();
    get_graph_cache().nodes.clear();
    if (!get_stage() || get_root().IsEmpty())
        return;

    auto root_prim = get_stage()->GetPrimAtPath(get_root());
    if (!root_prim)
        return;

    auto add_connection = [this](const ConnectionId& connection) {
        ConnectionId connection_id;
        const auto start_path = SdfPath(connection.start_port);
        const auto end_path = SdfPath(connection.end_port);
        if (start_path.GetPrimPath() == get_root())
        {
            const auto port_name = start_path.GetName();
            connection_id.start_port = start_path.GetPrimPath().GetString() + "#graph_in_" + port_name + "." + port_name;
        }
        if (end_path.GetPrimPath() == get_root())
        {
            connection_id.end_port = end_path.GetPrimPath().GetString() + "#graph_out." + end_path.GetName();
        }

        if (connection_id.start_port.empty())
            connection_id.start_port = SdfPath(connection.start_port).GetString();
        if (connection_id.end_port.empty())
            connection_id.end_port = SdfPath(connection.end_port).GetString();
        get_graph_cache().connections.emplace(std::move(connection_id));
    };
    auto add_connections_for_prim = [this, add_connection](const UsdPrim& prim) {
        auto connections = get_connections_for_prim(prim);
        for (const auto& connection : connections)
        {
            // add connections only on the current level of hierarchy
            if (is_descendant(get_root(), SdfPath(connection.start_port)) && is_descendant(get_root(), SdfPath(connection.end_port)))
                add_connection(connection);
        }
    };

    add_connections_for_prim(root_prim);
    for (const auto& child : root_prim.GetAllChildren())
    {
        get_graph_cache().nodes.insert(child.GetPath().GetString());
        add_connections_for_prim(child);
    }
}

bool HydraOpGraphModel::can_be_root(const NodeId& node_id) const
{
    if (!get_stage() || node_id.empty())
        return false;
    const auto usd_path = to_usd_path(node_id);
    auto prim = get_stage()->GetPrimAtPath(usd_path);
    return UsdHydraOpNodegraph(prim) || UsdHydraOpGroup(prim) || (prim.GetTypeName() == TfToken("Material"));
}

bool HydraOpGraphModel::can_fall_through(const NodeId& node_id) const
{
    if (!get_stage() || node_id.empty())
        return false;
    const auto usd_path = to_usd_path(node_id);
    if (usd_path == get_root())
        return false;

    auto prim = get_stage()->GetPrimAtPath(usd_path);
    return !!UsdHydraOpGroup(prim);
}

bool HydraOpGraphModel::is_supported_prim_type(const PXR_NS::UsdPrim& prim) const
{
    return prim.IsA<UsdHydraOpBaseNode>() || is_supported_type_for_translate_api(prim.GetTypeName()) || prim.GetTypeName() == "Backdrop";
}

void HydraOpGraphModel::move_nodegraph_node(const NodeId& node_id, const QPointF& pos)
{
    m_graph_pos_cache[node_id] = pos;

    Q_EMIT port_updated(node_id + "." + UsdUITokens->uiNodegraphNodePos.GetString());
}

void HydraOpGraphModel::on_rename()
{
    if (m_root.IsEmpty())
    {
        get_node_provider().rename_performed();
        return;
    }

    auto& graph_cache = get_graph_cache();
    GraphCache new_graph_cache;
    new_graph_cache.connections.reserve(graph_cache.connections.size());
    new_graph_cache.nodes.reserve(graph_cache.nodes.size());

    const auto& old_paths = get_node_provider().get_old_rename_paths();
    const auto& new_paths = get_node_provider().get_new_rename_paths();

    for (int i = 0; i < old_paths.size(); ++i)
    {
        if (!m_root.HasPrefix(old_paths[i]))
            continue;

        m_root = m_root.ReplacePrefix(old_paths[i], new_paths[i], false);
        const auto old_str = old_paths[i].GetString();
        const auto new_str = new_paths[i].GetString();

        for (const auto& node : get_graph_cache().nodes)
            new_graph_cache.nodes.insert(TfStringReplace(node, old_str, new_str));

        for (const auto& con : graph_cache.connections)
        {
            ConnectionId new_con;
            new_con.start_port = TfStringReplace(con.start_port, old_str, new_str);
            new_con.end_port = TfStringReplace(con.end_port, old_str, new_str);
            new_graph_cache.connections.insert(std::move(new_con));
        }
        std::swap(graph_cache, new_graph_cache);
        get_node_provider().rename_performed();
        Q_EMIT model_reset();
        return;
    }
    get_node_provider().rename_performed();
}

void HydraOpGraphModel::on_selection_changed()
{
    if (get_root().IsEmpty())
        return;
    const auto sel_paths = Application::instance().get_prim_selection();
    QVector<NodeId> nodes;
    nodes.reserve(sel_paths.size());
    for (const auto path : sel_paths)
    {
        if (get_graph_cache().nodes.find(path.GetString()) != get_graph_cache().nodes.end())
            nodes.push_back(path.GetString());
    }
    Q_EMIT selection_changed(std::move(nodes), QVector<ConnectionId>());
}

QVector<NodeId> HydraOpGraphModel::get_nodes() const
{
    if (get_root() == SdfPath::EmptyPath())
        return {};

    const auto& nodes = get_graph_cache().nodes;
    QVector<NodeId> result(nodes.size());
    std::transform(nodes.begin(), nodes.end(), result.begin(), [](const NodeId& node) { return node; });

    const auto root_id = get_root().GetString();
    for (const auto& input : get_input_names(root_id))
    {
        result.push_back(root_id + "#graph_in_" + input);
    }
    if (UsdHydraOpGroup(get_prim_for_node(get_root().GetString())))
    {
        result.push_back(root_id + "#graph_out");
    }
    return result;
}

UsdPrim HydraOpGraphModel::get_prim_for_node(const NodeId& node_id) const
{
    return UsdGraphModel::get_prim_for_node(TfStringGetBeforeSuffix(node_id, '#'));
}

QVector<ConnectionId> HydraOpGraphModel::get_connections() const
{
    const auto& connections = get_graph_cache().connections;
    QVector<ConnectionId> result(connections.size());
    std::transform(connections.begin(), connections.end(), result.begin(), [](const ConnectionId& connection) { return connection; });
    return std::move(result);
}

QVector<ConnectionId> HydraOpGraphModel::get_connections_for_node(const NodeId& node_id) const
{
    if (!get_stage() || get_root().IsEmpty())
        return {};

    QVector<ConnectionId> result;
    const auto node_path = node_id;

    for (const auto& connection : get_graph_cache().connections)
    {
        if (get_node_path(connection.start_port) == node_path || get_node_path(connection.end_port) == node_path)
            result.push_back(connection);
    }

    return std::move(result);
}

void HydraOpGraphModel::try_add_prim(const PXR_NS::SdfPath& prim_path)
{
    if (get_graph_cache().nodes.find(prim_path.GetString()) != get_graph_cache().nodes.end())
        return;

    if (prim_path.GetParentPath() != get_root())
        return;

    const NodeId node_id = prim_path.GetString();
    const auto prim = get_stage()->GetPrimAtPath(prim_path);

    if (!UsdHydraOpBaseNode(prim) && !UsdHydraOpTranslateAPI(prim) && !UsdUIBackdrop(prim))
    {
        return;
    }

    auto incoming_connections = get_connections_for_prim(prim);

    decltype(incoming_connections)::iterator end_it;
    if (!is_descendant(get_root(), prim.GetPath()))
    {
        end_it = std::remove_if(incoming_connections.begin(), incoming_connections.end(), [this](const ConnectionId& connection) {
            return !(is_descendant(get_root(), SdfPath(connection.start_port)) || is_descendant(get_root(), SdfPath(connection.end_port)));
        });
    }
    else
    {
        end_it = incoming_connections.end();
    }

    auto outcoming_connections = get_connections_for_node(node_id);

    for (auto it = incoming_connections.begin(); it != end_it; ++it)
        get_graph_cache().connections.emplace(
            ConnectionId { from_usd_path(SdfPath(it->start_port), m_root), from_usd_path(SdfPath(it->end_port), m_root) });

    Q_EMIT node_created(node_id);
    for (const auto& con : incoming_connections)
        Q_EMIT connection_created(ConnectionId { from_usd_path(SdfPath(con.start_port), m_root), from_usd_path(SdfPath(con.end_port), m_root) });
    for (const auto& con : outcoming_connections)
        Q_EMIT connection_created(ConnectionId { con.start_port, con.end_port });
}

void HydraOpGraphModel::try_remove_prim(const PXR_NS::SdfPath& prim_path)
{
    if (get_root().HasPrefix(prim_path))
    {
        set_root(SdfPath::EmptyPath());
        return;
    }
    const NodeId node_id = prim_path.GetString();
    if (get_graph_cache().nodes.find(node_id) == get_graph_cache().nodes.end())
        return;

    std::vector<ConnectionId> removed_connections;
    for (auto it = get_graph_cache().connections.begin(); it != get_graph_cache().connections.end();)
    {
        if (get_node_path(it->start_port) == prim_path.GetString() || get_node_path(it->end_port) == prim_path.GetString())
        {
            removed_connections.emplace_back(*it);
            it = get_graph_cache().connections.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (const auto& connection : removed_connections)
        Q_EMIT connection_removed(connection);

    Q_EMIT node_removed(node_id);
}

void HydraOpGraphModel::try_update_prop(const PXR_NS::SdfPath& prop_path)
{
    const auto rel = get_stage()->GetAttributeAtPath(prop_path);

    SdfPathVector connections;
    if (rel)
    {
        rel.GetConnections(&connections);
    }

    std::unordered_set<SdfPath, SdfPath::Hash> target_set(connections.begin(), connections.end());
    for (auto it = get_graph_cache().connections.begin(); it != get_graph_cache().connections.end();)
    {
        const auto start_path = to_usd_path(it->start_port);
        const auto end_path = to_usd_path(it->end_port);
        // If has incoming connection that are no longer exists
        if (end_path == prop_path && target_set.find(start_path) == target_set.end())
        {
            const auto removed_connection = *it;
            it = get_graph_cache().connections.erase(it);
            Q_EMIT connection_removed(removed_connection);
        }
        ++it;
    }

    std::string prop_model_path;
    if (prop_path.GetPrimPath() == m_root)
    {
        const auto is_input = UsdShadeInput::IsInterfaceInputName(prop_path.GetName());
        if (is_input)
            prop_model_path = prop_path.GetPrimPath().GetString() + "#graph_in_" + prop_path.GetName() + "." + prop_path.GetName();
        else
            prop_model_path = prop_path.GetPrimPath().GetString() + "#graph_out." + prop_path.GetName();
    }
    else
    {
        prop_model_path = prop_path.GetString();
    }

    for (const auto target : target_set)
    {
        std::string target_model_path = target.GetString();
        if (target.GetPrimPath() == m_root)
        {
            const auto target_inputs = get_input_names(prop_path.GetPrimPath().GetString());
            const auto is_input = std::find(target_inputs.begin(), target_inputs.end(), prop_path.GetName()) != target_inputs.end();
            if (is_input)
                target_model_path = target.GetPrimPath().GetString() + "#graph_in_" + target.GetName() + "." + target.GetName();
            else
                target_model_path = target.GetPrimPath().GetString() + "#graph_out." + target.GetName();
        }
        else if (!is_descendant(m_root, target))
        {
            continue;
        }

        const auto result = get_graph_cache().connections.emplace(ConnectionId { target_model_path, prop_model_path });
        if (result.second)
            Q_EMIT connection_created(*result.first);
    }
    Q_EMIT port_updated(prop_model_path);
}

OPENDCC_NAMESPACE_CLOSE
