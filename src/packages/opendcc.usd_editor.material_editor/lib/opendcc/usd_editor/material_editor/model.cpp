// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/material_editor/model.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/prim_material_override.h"
#include "opendcc/app/viewport/persistent_material_override.h"
#include "opendcc/app/ui/shader_node_registry.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/usd_editor/usd_node_editor/utils.h"
#include "usd_fallback_proxy/core/usd_prim_fallback_proxy.h"

#include <pxr/usd/usdShade/material.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usdUI/nodeGraphNodeAPI.h>
#include <pxr/usd/usd/editTarget.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/layerUtils.h>

#include <regex>

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);
class MaterialNodeMoveAction : public MoveAction
{
public:
    MaterialNodeMoveAction(MaterialGraphModel& model, const QPointF& old_pos, const QPointF& new_pos, bool is_input)
        : m_model(model)
        , m_old_pos(old_pos)
        , m_new_pos(new_pos)
        , m_is_input(is_input)
    {
        redo();
    }

    void undo() override { m_model.move_material_node(m_is_input, m_old_pos); }

    void redo() override { m_model.move_material_node(m_is_input, m_new_pos); }

private:
    MaterialGraphModel& m_model;
    QPointF m_old_pos;
    QPointF m_new_pos;
    bool m_is_input = false;
};

class ExternalNodeMoveAction : public MoveAction
{
public:
    ExternalNodeMoveAction(MaterialGraphModel& model, const NodeId& node_id, const QPointF& old_pos, const QPointF& new_pos)
        : m_model(model)
        , m_node_id(node_id)
        , m_old_pos(old_pos)
        , m_new_pos(new_pos)
    {
        do_cmd(m_new_pos);
        m_inverse = std::make_unique<commands::UndoInverse>(std::move(commands::UndoRouter::instance().get_inversions()));
    }

    void undo() override { do_cmd(m_old_pos); }

    void redo() override { do_cmd(m_new_pos); }

private:
    void do_cmd(const QPointF& pos)
    {
        m_model.move_external_node(m_node_id, pos);
        if (m_inverse)
            m_inverse->invert();
    }

    MaterialGraphModel& m_model;
    std::unique_ptr<commands::UndoInverse> m_inverse = nullptr;
    NodeId m_node_id;
    QPointF m_old_pos;
    QPointF m_new_pos;
};

namespace
{
    static SdfLayerHandle find_layer_handle(const UsdAttribute& attr, const UsdTimeCode& time)
    {
        for (const auto& spec : attr.GetPropertyStack(time))
        {
            if (spec->HasDefaultValue() || spec->GetLayer()->GetNumTimeSamplesForPath(spec->GetPath()) > 0)
            {
                return spec->GetLayer();
            }
        }
        return TfNullPtr;
    }
    static bool resolve_symlinks(const std::string& srcPath, std::string* outPath)
    {
        std::string error;
        *outPath = TfRealPath(srcPath, false, &error);

        if (outPath->empty() || !error.empty())
        {
            return false;
        }

        return true;
    }
    static SdfAssetPath resolve_asset_symlinks(const SdfAssetPath& assetPath)
    {
        std::string p = assetPath.GetResolvedPath();
        if (p.empty())
        {
            p = assetPath.GetAssetPath();
        }

        if (resolve_symlinks(p, &p))
        {
            return SdfAssetPath(assetPath.GetAssetPath(), p);
        }
        else
        {
            return assetPath;
        }
    }
    static std::pair<std::string, std::string> split_udim_pattern(const std::string& path)
    {
        static const std::string pattern = "<UDIM>";
        const std::string::size_type pos = path.find(pattern);
        if (pos != std::string::npos)
        {
            return { path.substr(0, pos), path.substr(pos + pattern.size()) };
        }

        return { std::string(), std::string() };
    }

    static std::string resolve_path_for_first_tile(const std::pair<std::string, std::string>& split_path, const SdfLayerHandle& layer)
    {
        ArResolver& resolver = ArGetResolver();

        for (int i = 1001; i < 1100; i++)
        {
            // Fill in integer
            std::string path = split_path.first + std::to_string(i) + split_path.second;
            if (layer)
            {
                // Deal with layer-relative paths.
                path = SdfComputeAssetPathRelativeToLayer(layer, path);
            }
            // Resolve. Unlike the non-UDIM case, we do not resolve symlinks
            // here to handle the case where the symlinks follow the UDIM
            // naming pattern but the files that are linked do not. We'll
            // let whoever consumes the pattern determine if they want to
            // resolve symlinks themselves.
            return resolver.Resolve(path);
        }
        return std::string();
    }

    SdfAssetPath resolve_asset_attr(const SdfAssetPath& path, const UsdAttribute& attr, const UsdTimeCode& time)
    {
        // See whether the asset path contains UDIM pattern.
        const std::pair<std::string, std::string> split_path = split_udim_pattern(path.GetAssetPath());

        if (split_path.first.empty() && split_path.second.empty())
        {
            // Not a UDIM, resolve symlinks and exit.
            return resolve_asset_symlinks(path);
        }

        // Find first tile.
        const std::string first_tile_path = resolve_path_for_first_tile(split_path, find_layer_handle(attr, time));

        if (first_tile_path.empty())
        {
            return path;
        }

        // Construct the file path /filePath/myImage.<UDIM>.exr by using
        // the first part from the first resolved tile, "<UDIM>" and the
        // suffix.

        const std::string& suffix = split_path.second;

        // Sanity check that the part after <UDIM> did not change.
        if (!TfStringEndsWith(first_tile_path, suffix))
        {
            OPENDCC_WARN("Resolution of first udim tile gave ambiguous result. "
                         "First tile for '{}' is '{}'.",
                         path.GetAssetPath().c_str(), first_tile_path.c_str());
            return path;
        }

        // Length of the part /filePath/myImage.<UDIM>.exr.
        const std::string::size_type pref_len = first_tile_path.size() - suffix.size() - 4;

        return SdfAssetPath(path.GetAssetPath(), first_tile_path.substr(0, pref_len) + "<UDIM>" + suffix);
    }

    VtValue resolve_material_param_value(const UsdAttribute& attribute, const UsdTimeCode& time)
    {
        VtValue value;

        if (!attribute.Get(&value, time))
        {
            return value;
        }

        if (!value.IsHolding<SdfAssetPath>())
        {
            return value;
        }

        return VtValue(resolve_asset_attr(value.UncheckedGet<SdfAssetPath>(), attribute, time));
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

    bool is_descendant(SdfPath root, SdfPath target)
    {
        return target.GetPrimPath() == root || target.GetPrimPath().GetParentPath() == root;
    }
};

std::string MaterialGraphModel::get_property_name(const PortId& port_id) const
{
    auto delimiter = port_id.rfind('.');
    if (delimiter == std::string::npos)
        return std::string();
    return port_id.substr(delimiter + 1);
}

NodeId MaterialGraphModel::from_usd_path(const SdfPath& path, const SdfPath& root) const
{
    if (path.GetPrimPath() != root)
        return path.GetString();

    std::string tag;
    auto in = "#mat_in";
    auto out = "#mat_out";

    if (TfStringStartsWith(path.GetName(), UsdShadeTokens->inputs))
        tag = in;
    else if (TfStringStartsWith(path.GetName(), UsdShadeTokens->outputs))
        tag = out;

    std::string result;
    result += path.GetPrimPath().GetString() + tag;
    if (!path.IsPrimPath())
        result += "." + path.GetName();
    return std::move(result);
}

PXR_NS::SdfPath MaterialGraphModel::to_usd_path(const PortId& node_id) const
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
TfToken MaterialGraphModel::get_expansion_state(const NodeId& node) const
{
    TfToken result = UsdUITokens->open;
    auto prim = get_prim_for_node(node);
    if (!prim)
        return result;

    auto node_prim = UsdUINodeGraphNodeAPI(prim);
    node_prim.GetExpansionStateAttr().Get(&result);
    return result;
}

NodeId MaterialGraphModel::get_node_id_from_port(const PortId& port) const
{
    auto pos = port.rfind('#');
    if (pos != std::string::npos)
        return port.substr(0, port.rfind('.'));
    return SdfPath(port).GetPrimPath().GetString();
}

void MaterialGraphModel::set_expansion_state(const NodeId& node, PXR_NS::TfToken expansion_state)
{
    auto prim = get_prim_for_node(node);
    if (!prim)
        return;

    auto node_prim = UsdUINodeGraphNodeAPI(prim);
    node_prim.CreateExpansionStateAttr(VtValue(expansion_state));
}

bool MaterialGraphModel::connect_ports(const Port& start_port, const Port& end_port)
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
        auto ng = UsdShadeNodeGraph(start_prim);
        if (!ng)
            return false;

        if (!is_add_port(start_port.id))
            return false;

        bool ng_is_root = get_root() == start_prim.GetPrimPath();
        auto stripped_name =
            TfToken(end_prop->get_name_token().data() + end_prop->get_name_token().GetString().find(SdfPathTokens->namespaceDelimiter) + 1);

        bool create_input = start_port.type == Port::Type::Input;
        if (ng_is_root)
            create_input = !create_input;

        TfTokenVector existing_names;
        for (const auto proxy : UsdPrimFallbackProxy(start_prim).get_all_property_proxies())
            existing_names.push_back(proxy->get_name_token());

        if (create_input)
            stripped_name = commands::utils::get_new_name(TfToken("inputs:" + stripped_name.GetString()), existing_names);
        else
            stripped_name = commands::utils::get_new_name(TfToken("outputs:" + stripped_name.GetString()), existing_names);

        auto new_prop = start_prim.CreateAttribute(stripped_name, end_prop->get_type_name());
        new_start.id = start_prim.GetPrimPath().AppendProperty(new_prop.GetName()).GetString();
        new_start.type = start_port.type;

        new_end.id = to_usd_path(end_port.id).GetString();
        new_end.type = end_port.type;
        return true;
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
        SdfPathVector targets;
        if (auto attr = prop->get_attribute())
        {
            attr.GetConnections(&targets);
            remove_connections(attr, targets);
        }
        else if (auto rel = prop->get_relationship())
        {
            rel.GetTargets(&targets);
            remove_connections(rel, targets);
        }
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

QPointF MaterialGraphModel::get_node_position(const NodeId& node_id) const
{
    static auto get_or_null_from_map = [](const std::unordered_map<NodeId, QPointF>& map, const NodeId& id) -> QPointF {
        auto pos = map.find(id);
        if (pos != map.end())
            return pos->second;
        return QPointF(0, 0);
    };

    if (is_external_node(node_id))
        return get_or_null_from_map(m_external_node_pos, node_id);
    else if (TfStringEndsWith(node_id, "#mat_in"))
        return get_or_null_from_map(m_mat_in_pos, node_id);
    else if (TfStringEndsWith(node_id, "#mat_out"))
        return get_or_null_from_map(m_mat_out_pos, node_id);

    return UsdGraphModel::get_node_position(node_id);
}

std::unique_ptr<MoveAction> MaterialGraphModel::on_node_moved(const NodeId& node_id, const QPointF& old_pos, const QPointF& new_pos)
{
    if (is_external_node(node_id))
        return std::make_unique<ExternalNodeMoveAction>(*this, node_id, old_pos, new_pos);
    if (TfStringEndsWith(node_id, "#mat_in"))
        return std::make_unique<MaterialNodeMoveAction>(*this, old_pos, new_pos, true);
    else if (TfStringEndsWith(node_id, "#mat_out"))
        return std::make_unique<MaterialNodeMoveAction>(*this, old_pos, new_pos, false);
    else
        return UsdGraphModel::on_node_moved(node_id, old_pos, new_pos);
}

bool MaterialGraphModel::has_port(const PortId& port) const
{
    return UsdGraphModel::has_port(to_usd_path(port).GetString());
}

MaterialGraphModel::MaterialGraphModel(QObject* parent)
    : UsdGraphModel(parent)
{
    connect(this, &GraphModel::node_created, this, [this](const NodeId& node) { get_graph_cache().nodes.insert(node); });
    connect(this, &GraphModel::node_removed, this, [this](const NodeId& node) { get_graph_cache().nodes.erase(node); });
    stage_changed_impl();
}

MaterialGraphModel::~MaterialGraphModel() {}

bool MaterialGraphModel::can_connect(SdfValueTypeName src, SdfValueTypeName dst)
{
    return true;
    // if (src.IsArray() != dst.IsArray())
    //     return false;
    //// special case when connecting string to token
    // if (src.GetScalarType() == SdfValueTypeNames->String && dst.GetScalarType() == SdfValueTypeNames->Token ||
    //     dst.GetScalarType() == SdfValueTypeNames->String && src.GetScalarType() == SdfValueTypeNames->Token)
    //{
    //     return true;
    // }

    // return src.GetScalarType().GetType() == dst.GetScalarType().GetType();
}

bool MaterialGraphModel::can_connect(const Port& start_port, const Port& end_port) const
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

    // We prohibit ports belonging to one prim from connecting, except for those cases when the prim is a root.
    // The root prim in the "Material Editor" is 2 nodes: 'Material Input' and 'Material Output'. Such nodes must be able to connect.
    // If the other nodes try to connect to themselves, then we get incorrect behavior of the ConnectionItem.
    if (start_path.GetPrimPath() == end_path.GetPrimPath())
    {
        // The connection between the special nodes 'Material Input' and 'Material Output' is defined by their prim being root
        // and the fact that they have different prefixes in the port name.
        // This is a more correct check because it is not guaranteed that special nodes must contain the #mat_in and #mat_out tags in the NodeId.
        auto is_different_mat_nodes = [](const SdfPath& start, const SdfPath& end) {
            return TfStringStartsWith(start.GetName(), UsdShadeTokens->inputs) && TfStringStartsWith(end.GetName(), UsdShadeTokens->outputs);
        };

        bool is_root = start_path.GetPrimPath() == m_network_path;
        bool is_connection_between_mat_nodes = is_different_mat_nodes(start_path, end_path) || is_different_mat_nodes(end_path, start_path);

        if (!is_root || (is_root && !is_connection_between_mat_nodes))
            return false;
    }

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
    if (start_prop && end_prop)
        return can_connect(start_prop->get_type_name(), end_prop->get_type_name());
    else
        return false;

    // todo reconsider
    return UsdGraphModel::can_connect(start_port, end_port);
}

void MaterialGraphModel::on_selection_set(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections)
{
    QVector<NodeId> resolved_nodes;
    resolved_nodes.reserve(nodes.size());
    for (const auto& node : nodes)
        resolved_nodes.push_back(to_usd_path(node).GetString());
    UsdGraphModel::on_selection_set(resolved_nodes, connections);
}

void MaterialGraphModel::delete_connection(const ConnectionId& connection)
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

void MaterialGraphModel::remove(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections)
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
            if (TfStringEndsWith(node, "#mat_in") || TfStringEndsWith(node, "#mat_out"))
                continue;

            for (const auto& connection : get_connections_for_node(node))
                delete_connection(connection);

            auto prim_path = to_usd_path(node);
            if (get_stage()->RemovePrim(prim_path))
                Q_EMIT node_removed(node);
        }
        block_usd_notifications(false);
    }
}

void MaterialGraphModel::set_show_external_nodes(bool show)
{
    if (m_show_external_nodes == show)
        return;
    m_show_external_nodes = show;

    init_material_network();
    Q_EMIT model_reset();
}

bool MaterialGraphModel::show_external_nodes() const
{
    return m_show_external_nodes;
}

void MaterialGraphModel::set_root(const PXR_NS::SdfPath& network_path)
{
    if (m_network_path == network_path)
        return;

    if (!get_stage() || !can_be_root(from_usd_path(network_path, m_network_path)))
    {
        m_network_path = SdfPath::EmptyPath();
    }
    else
    {
        m_network_path = network_path;
    }

    init_material_network();
    Q_EMIT model_reset();
}

PXR_NS::SdfPath MaterialGraphModel::get_root() const
{
    return m_network_path;
}

void MaterialGraphModel::stage_changed_impl()
{
    auto stage = get_stage();
    if (!stage)
    {
        set_root(SdfPath::EmptyPath());
        return;
    }
    auto material = UsdShadeMaterial(stage->GetPrimAtPath(get_root()));
    if (!material)
    {
        set_root(SdfPath::EmptyPath());
        return;
    }
    Q_EMIT model_reset();
}

void MaterialGraphModel::init_material_network()
{
    get_graph_cache().connections.clear();
    get_graph_cache().nodes.clear();
    if (!get_stage() || get_root().IsEmpty())
        return;

    auto root_prim = get_stage()->GetPrimAtPath(m_network_path);
    if (!root_prim)
        return;

    auto add_connection = [this](const ConnectionId& connection) {
        ConnectionId connection_id;
        if (SdfPath(connection.start_port).GetPrimPath() == m_network_path)
            connection_id.start_port =
                SdfPath(connection.start_port).GetPrimPath().GetString() + "#mat_in." + SdfPath(connection.start_port).GetName();
        else if (SdfPath(connection.end_port).GetPrimPath() == m_network_path)
            connection_id.end_port = SdfPath(connection.end_port).GetPrimPath().GetString() + "#mat_out." + SdfPath(connection.end_port).GetName();

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
            if (is_descendant(m_network_path, SdfPath(connection.start_port)) && is_descendant(m_network_path, SdfPath(connection.end_port)))
                add_connection(connection);
        }
    };

    if (!show_external_nodes())
    {
        add_connections_for_prim(root_prim);
        for (const auto& child : root_prim.GetAllChildren())
        {
            get_graph_cache().nodes.insert(child.GetPath().GetString());
            add_connections_for_prim(child);
        }
    }
    else
    {
        auto& nodes = get_graph_cache().nodes;
        std::function<void(const UsdPrim& prim)> traverse = [this, &traverse, &nodes, &add_connection](const UsdPrim& prim) {
            auto node_path = from_usd_path(prim.GetPath(), get_root());
            if (nodes.find(node_path) != nodes.end())
                return;
            nodes.emplace(std::move(node_path));
            const auto connections = get_connections_for_prim(prim);
            const auto is_external = !is_descendant(get_root(), prim.GetPath());
            for (const auto& con : connections)
            {
                auto prim_path = SdfPath(con.start_port).GetPrimPath();
                const auto next_prim = get_stage()->GetPrimAtPath(prim_path);
                if (!next_prim)
                    continue;

                if ((UsdShadeNodeGraph(prim) || UsdShadeMaterial(prim)) && prim_path.GetParentPath() == prim.GetPath())
                    continue;

                if (SdfPath(con.start_port).IsPropertyPath())
                    add_connection(con);

                if (prim_path == get_root())
                    continue;
                traverse(next_prim);
            }
        };
        for (const auto& child : root_prim.GetAllChildren())
        {
            traverse(child);
        }

        for (const auto& con : get_connections_for_prim(root_prim))
        {
            auto prim_path = SdfPath(con.start_port).GetPrimPath();
            auto prim = get_stage()->GetPrimAtPath(prim_path);
            if (!prim || TfStringStartsWith(get_property_name(con.end_port), "inputs:"))
                continue;

            if (SdfPath(con.start_port).IsPropertyPath())
                add_connection(con);
            traverse(prim);
        }
    }
}

void MaterialGraphModel::update_material_override()
{
#if PXR_VERSION < 2002
    PrimMaterialDescriptor mat_descr(build_mat_network_override().Get<std::string>(), {});
#else
    PrimMaterialDescriptor mat_descr(build_mat_network_override(), {});
#endif
    PersistentMaterialOverride::instance().get_override()->material_resource_override(get_root(), mat_descr);
    ViewportWidget::update_all_gl_widget();
}

PXR_NS::VtValue MaterialGraphModel::build_mat_network_override() const
{
    if (m_preview_shader.IsEmpty() || !get_stage())
        return VtValue();

    const auto terminal_mat_path = m_preview_shader;
    HdMaterialNetworkMap network_map;
    auto& network = network_map.map[HdMaterialTerminalTokens->surface];

    TfHashSet<SdfPath, SdfPath::Hash> visited_nodes;
    std::function<void(UsdStageRefPtr, SdfPath, HdMaterialNetwork&)> build_material_network =
        [&visited_nodes, &build_material_network](UsdStageRefPtr stage, SdfPath path, HdMaterialNetwork& network) {
            if (visited_nodes.find(path) != visited_nodes.end())
                return;

            HdMaterialNode node;
            node.path = path;

            auto shader = UsdShadeShader(stage->GetPrimAtPath(path));

            for (const auto& input : shader.GetInputs())
            {
                const auto input_name = input.GetBaseName();
                UsdShadeAttributeType attr_type;
                auto attr = input.GetValueProducingAttribute(&attr_type);
                if (attr_type == UsdShadeAttributeType::Output)
                {
                    build_material_network(stage, attr.GetPrimPath(), network);
                    HdMaterialRelationship rel;
                    rel.outputId = node.path;
                    rel.outputName = input_name;
                    rel.inputId = attr.GetPrimPath();
                    rel.inputName = UsdShadeOutput(attr).GetBaseName();
                    network.relationships.push_back(std::move(rel));
                }
                else if (attr_type == UsdShadeAttributeType::Input)
                {
                    node.parameters[input_name] = resolve_material_param_value(attr, Application::instance().get_current_time());
                }
            }

            TfToken id;
            if (!shader.GetShaderId(&id) || id.IsEmpty())
                return;

            node.identifier = id;

            auto sdr_node = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(node.identifier);
            if (sdr_node)
            {
                const auto& primvars = sdr_node->GetPrimvars();
                network.primvars.insert(network.primvars.end(), primvars.begin(), primvars.end());
                for (const auto& p : sdr_node->GetAdditionalPrimvarProperties())
                {
                    VtValue vtname;
                    TfToken primvar_name;
                    auto param_iter = node.parameters.find(p);
                    if (param_iter != node.parameters.end())
                        vtname = param_iter->second;

                    if (vtname.IsEmpty() && sdr_node)
                    {
                        if (auto prop = sdr_node->GetShaderInput(p))
                            vtname = prop->GetDefaultValue();
                    }
                    if (vtname.IsHolding<TfToken>())
                        primvar_name = vtname.UncheckedGet<TfToken>();
                    else if (vtname.IsHolding<std::string>())
                        primvar_name = TfToken(vtname.UncheckedGet<std::string>());

                    network.primvars.push_back(primvar_name);
                }
            }

            network.nodes.push_back(node);
            visited_nodes.insert(node.path);
        };

    build_material_network(get_stage(), terminal_mat_path, network);

    auto terminal_shader = UsdPrimFallbackProxy(get_stage()->GetPrimAtPath(terminal_mat_path));
    auto terminal_shader_props = terminal_shader.get_all_property_proxies();
    if (std::any_of(terminal_shader_props.begin(), terminal_shader_props.end(), [](const UsdPropertyProxyPtr& proxy) {
            if (!TfStringStartsWith(proxy->get_name_token(), "outputs:"))
                return false;
            return proxy->get_type_name() == SdfValueTypeNames->Token || proxy->get_type_name().GetRole() == SdfValueRoleNames->Color;
        }))
    {
        // Render specific logic:
        // Adding intermediate unlit node as a terminal node
        auto shader = UsdShadeShader(terminal_shader.get_usd_prim());
        TfToken shader_id;
        if (!shader.GetShaderId(&shader_id))
            return VtValue();
        const auto node_plugin = ShaderNodeRegistry::get_node_plugin_name(shader_id);
        if (node_plugin == "ndrCycles" && !terminal_shader.get_property_proxy(TfToken("outputs:out")))
        {
            HdMaterialNode emission_node;
            emission_node.identifier = TfToken("cycles:emission");
            emission_node.path = terminal_mat_path.AppendChild(TfToken("___intermediate_terminal_node___emission"));
            emission_node.parameters[TfToken("strength")] = VtValue(1.0f);
            HdMaterialNode light_path_node;
            light_path_node.identifier = TfToken("cycles:light_path");
            light_path_node.path = terminal_mat_path.AppendChild(TfToken("___intermediate_terminal_node___light_path"));
            HdMaterialNode mix_shader;
            mix_shader.identifier = TfToken("cycles:mix_closure");
            mix_shader.path = terminal_mat_path.AppendChild(TfToken("___intermediate_terminal_node___mix"));
            HdMaterialRelationship emission_to_mix;
            emission_to_mix.inputId = emission_node.path;
            emission_to_mix.inputName = TfToken("emission");
            emission_to_mix.outputId = mix_shader.path;
            emission_to_mix.outputName = TfToken("closure2");

            HdMaterialRelationship light_path_to_mix;
            light_path_to_mix.inputId = light_path_node.path;
            light_path_to_mix.inputName = TfToken("is_camera_ray");
            light_path_to_mix.outputId = mix_shader.path;
            light_path_to_mix.outputName = TfToken("fac");

            HdMaterialRelationship terminal_rel;
            terminal_rel.outputId = emission_node.path;
            terminal_rel.outputName = TfToken("color");
            terminal_rel.inputId = terminal_mat_path;
            for (const auto& prop : terminal_shader_props)
            {
                if (TfStringStartsWith(prop->get_name_token(), "outputs:") && prop->get_type_name().GetRole() == SdfValueRoleNames->Color)
                {
                    terminal_rel.inputName = TfToken(TfStringGetSuffix(prop->get_name_token(), ':'));
                    break;
                }
            }
            if (!terminal_rel.inputName.IsEmpty())
            {
                network.nodes.push_back(emission_node);
                network.nodes.push_back(light_path_node);
                network.nodes.push_back(mix_shader);
                network.relationships.push_back(terminal_rel);
                network.relationships.push_back(light_path_to_mix);
                network.relationships.push_back(emission_to_mix);
                network_map.terminals.push_back(mix_shader.path);
            }
        }
        else if (node_plugin == "rmanDiscovery" && !terminal_shader.get_property_proxy(TfToken("outputs:out")))
        {
            HdMaterialNode emission_node;
            emission_node.identifier = TfToken("PxrConstant");
            emission_node.path = terminal_mat_path.AppendChild(TfToken("___intermediate_terminal_node___"));
            HdMaterialRelationship terminal_rel;
            terminal_rel.outputId = emission_node.path;
            terminal_rel.outputName = TfToken("emitColor");
            terminal_rel.inputId = terminal_mat_path;
            for (const auto& prop : terminal_shader_props)
            {
                if (TfStringStartsWith(prop->get_name_token(), "outputs:") && prop->get_type_name().GetRole() == SdfValueRoleNames->Color)
                {
                    terminal_rel.inputName = TfToken(TfStringGetSuffix(prop->get_name_token(), ':'));
                    break;
                }
            }
            if (!terminal_rel.inputName.IsEmpty())
            {
                network.nodes.push_back(emission_node);
                network.relationships.push_back(terminal_rel);
                network_map.terminals.push_back(emission_node.path);
            }
        }
        else
        {
            network_map.terminals.push_back(terminal_mat_path);
        }
    }

    return VtValue(network_map);
}

bool MaterialGraphModel::can_be_root(const NodeId& node_id) const
{
    if (!get_stage() || node_id.empty())
        return false;
    const auto usd_path = to_usd_path(node_id);
    auto prim = get_stage()->GetPrimAtPath(usd_path);
    return UsdShadeNodeGraph(prim) || UsdShadeMaterial(prim);
}

bool MaterialGraphModel::can_fall_through(const NodeId& node_id) const
{
    if (!get_stage() || node_id.empty())
        return false;
    const auto usd_path = to_usd_path(node_id);
    if (usd_path == get_root())
        return false;

    auto prim = get_stage()->GetPrimAtPath(usd_path);
    return static_cast<bool>(UsdShadeNodeGraph(prim));
}

bool MaterialGraphModel::is_supported_prim_type(const PXR_NS::UsdPrim& prim) const
{
    return prim.IsA<UsdShadeShader>() || prim.GetTypeName() == "NodeGraph" || prim.GetTypeName() == "Backdrop";
}

void MaterialGraphModel::move_material_node(bool is_input, const QPointF& pos)
{
    NodeId node_id;
    if (is_input)
    {
        node_id = get_root().GetString() + "#mat_in";
        m_mat_in_pos[node_id] = pos;
    }
    else
    {
        node_id = get_root().GetString() + "#mat_out";
        m_mat_out_pos[node_id] = pos;
    }
    Q_EMIT port_updated(node_id + "." + UsdUITokens->uiNodegraphNodePos.GetString());
}

void MaterialGraphModel::move_external_node(const NodeId& node_id, const QPointF& pos)
{
    if (is_external_node(node_id))
    {
        m_external_node_pos[node_id] = pos;
        Q_EMIT port_updated(node_id + "." + UsdUITokens->uiNodegraphNodePos.GetString());
    }
}

void MaterialGraphModel::on_rename()
{
    if (m_network_path.IsEmpty())
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
        if (!m_network_path.HasPrefix(old_paths[i]))
            continue;

        m_network_path = m_network_path.ReplacePrefix(old_paths[i], new_paths[i], false);
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

void MaterialGraphModel::on_selection_changed()
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

QVector<NodeId> MaterialGraphModel::get_nodes() const
{
    const auto& nodes = get_graph_cache().nodes;
    QVector<NodeId> result(nodes.size() + 2);
    std::transform(nodes.begin(), nodes.end(), result.begin(), [](const NodeId& node) { return node; });
    result.push_back(m_network_path.GetString() + "#mat_in");
    result.push_back(m_network_path.GetString() + "#mat_out");
    return std::move(result);
}

UsdPrim MaterialGraphModel::get_prim_for_node(const NodeId& node_id) const
{
    return UsdGraphModel::get_prim_for_node(TfStringGetBeforeSuffix(node_id, '#'));
}

PXR_NS::SdfPath MaterialGraphModel::get_preview_shader() const
{
    return m_preview_shader;
}

void MaterialGraphModel::set_preview_shader(PXR_NS::SdfPath preview_shader_path)
{
    if (m_preview_shader == preview_shader_path)
        return;
    m_preview_shader = preview_shader_path;
    if (m_preview_shader.IsEmpty())
    {
        auto mat_override = PersistentMaterialOverride::instance().get_override();
        if (mat_override)
            mat_override->clear_material_resource_override(get_root());
        ViewportWidget::update_all_gl_widget();
    }
    else
    {
        update_material_override();
    }

    Q_EMIT preview_shader_changed(preview_shader_path);
}

QVector<ConnectionId> MaterialGraphModel::get_connections() const
{
    const auto& connections = get_graph_cache().connections;
    QVector<ConnectionId> result(connections.size());
    std::transform(connections.begin(), connections.end(), result.begin(), [](const ConnectionId& connection) { return connection; });
    return std::move(result);
}

QVector<ConnectionId> MaterialGraphModel::get_connections_for_node(const NodeId& node_id) const
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

void MaterialGraphModel::try_add_prim(const PXR_NS::SdfPath& prim_path)
{
    if (get_graph_cache().nodes.find(prim_path.GetString()) != get_graph_cache().nodes.end())
        return;

    if (!show_external_nodes())
    {
        if (prim_path.GetParentPath() != get_root())
            return;

        const NodeId node_id = prim_path.GetString();
        const auto prim = get_stage()->GetPrimAtPath(prim_path);
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
                ConnectionId { from_usd_path(SdfPath(it->start_port), m_network_path), from_usd_path(SdfPath(it->end_port), m_network_path) });

        Q_EMIT node_created(node_id);
        for (const auto& con : incoming_connections)
            Q_EMIT connection_created(
                ConnectionId { from_usd_path(SdfPath(con.start_port), m_network_path), from_usd_path(SdfPath(con.end_port), m_network_path) });
        for (const auto& con : outcoming_connections)
            Q_EMIT connection_created(ConnectionId { con.start_port, con.end_port });
    }
    else
    {
        if (prim_path == get_root())
            return;

        auto& nodes = get_graph_cache().nodes;
        const NodeId node_id = prim_path.GetString();
        const auto prim = get_stage()->GetPrimAtPath(prim_path);
        auto incoming_connections = get_connections_for_prim(prim);

        decltype(incoming_connections.end()) end_it;
        if (is_descendant(get_root(), prim.GetPath()))
        {
            for (const auto& con : incoming_connections)
            {
                const SdfPath sdf_start(con.start_port);
                const SdfPath sdf_end(con.end_port);
                if (sdf_start.IsPropertyPath())
                    get_graph_cache().connections.emplace(ConnectionId { from_usd_path(sdf_start, get_root()), from_usd_path(sdf_end, get_root()) });

                // add external nodes that are not in the current graph
                const auto node_path = from_usd_path(sdf_start.GetPrimPath(), get_root());
                if (sdf_start.GetPrimPath() != get_root() && nodes.find(node_path) == nodes.end())
                    Q_EMIT node_created(node_path);
            }
            end_it = incoming_connections.end();
        }
        else
        {
            // remove external node -> unknown node
            end_it = std::remove_if(incoming_connections.begin(), incoming_connections.end(), [this, &nodes](const ConnectionId& connection) {
                const SdfPath sdf_start(connection.start_port);

                return nodes.find(from_usd_path(sdf_start.GetPrimPath(), get_root())) == nodes.end();
            });

            if (incoming_connections.begin() == end_it)
                return;

            for (auto it = incoming_connections.begin(); it != end_it; ++it)
            {
                const SdfPath sdf_start(it->start_port);
                const SdfPath sdf_end(it->end_port);
                if (sdf_start.IsPropertyPath())
                    get_graph_cache().connections.emplace(ConnectionId { from_usd_path(sdf_start, get_root()), from_usd_path(sdf_end, get_root()) });
            }
        }

        Q_EMIT node_created(node_id);
        for (auto it = incoming_connections.begin(); it != end_it; ++it)
        {
            const SdfPath sdf_start(it->start_port);
            const SdfPath sdf_end(it->end_port);

            Q_EMIT connection_created(ConnectionId { from_usd_path(sdf_start, get_root()), from_usd_path(sdf_end, get_root()) });
        }
    }
}

void MaterialGraphModel::try_remove_prim(const PXR_NS::SdfPath& prim_path)
{
    if (get_root().HasPrefix(prim_path))
    {
        m_mat_in_pos.erase(get_root().GetString() + "#mat_in");
        m_mat_out_pos.erase(get_root().GetString() + "#mat_out");

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

void MaterialGraphModel::try_update_prop(const PXR_NS::SdfPath& prop_path)
{
    if (!m_preview_shader.IsEmpty() && prop_path.HasPrefix(m_preview_shader))
    {
        update_material_override();
    }

    const auto prop = get_stage()->GetPropertyAtPath(prop_path);

    SdfPathVector connections;
    if (prop)
    {
        if (prop.Is<UsdAttribute>())
            prop.As<UsdAttribute>().GetConnections(&connections);
        else
            prop.As<UsdRelationship>().GetTargets(&connections);
    }
    std::unordered_set<SdfPath, SdfPath::Hash> target_set(connections.begin(), connections.end());
    for (auto it = get_graph_cache().connections.begin(); it != get_graph_cache().connections.end();)
    {
        const auto start_path = to_usd_path(it->start_port);
        const auto end_path = to_usd_path(it->end_port);
        // If incoming connection that are no longer exists or port was deleted
        if (end_path == prop_path && target_set.find(start_path) == target_set.end() || !has_port(it->end_port) || !has_port(it->start_port))
        {
            const auto removed_connection = *it;
            it = get_graph_cache().connections.erase(it);
            Q_EMIT connection_removed(removed_connection);
        }
        ++it;
    }

    std::string prop_model_path;
    if (prop_path.GetPrimPath() == m_network_path)
    {
        if (TfStringStartsWith(prop.GetName(), "inputs:"))
            prop_model_path = make_tagged_path(prop_path.GetString(), "#mat_in");
        else if (TfStringStartsWith(prop.GetName(), "outputs:"))
            prop_model_path = make_tagged_path(prop_path.GetString(), "#mat_out");
        else
            return;
    }
    else
    {
        prop_model_path = prop_path.GetString();
    }

    for (const auto target : target_set)
    {
        std::string target_model_path = target.GetString();
        if (target.GetPrimPath() == m_network_path)
        {
            if (TfStringStartsWith(target.GetName(), UsdShadeTokens->inputs))
                target_model_path = make_tagged_path(target.GetString(), "#mat_in");
            else if (TfStringStartsWith(target.GetName(), UsdShadeTokens->outputs))
                target_model_path = make_tagged_path(target.GetString(), "#mat_out");
        }
        else
        {
            if (!show_external_nodes() && !is_descendant(m_network_path, target))
            {
                continue;
            }

            if (show_external_nodes() && !is_descendant(get_root(), target))
            {
                const auto node_id = get_node_id_from_port(from_usd_path(target, get_root()));
                if (get_graph_cache().nodes.find(node_id) == get_graph_cache().nodes.end())
                    Q_EMIT node_created(node_id);
            }
        }

        if (target.IsPropertyPath())
        {
            const auto result = get_graph_cache().connections.emplace(ConnectionId { target_model_path, prop_model_path });
            if (result.second)
                Q_EMIT connection_created(*result.first);
        }
    }
    Q_EMIT port_updated(prop_model_path);
}

bool MaterialGraphModel::is_external_node(const NodeId& node_id) const
{
    return !is_descendant(get_root(), to_usd_path(node_id));
}

OPENDCC_NAMESPACE_CLOSE
