/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <opendcc/opendcc.h>
#include <opendcc/ui/node_editor/api.h>
#include <QPoint>
#include <QObject>
#include <vector>
#include "opendcc/base/utils/hash.h"

OPENDCC_NAMESPACE_OPEN
typedef std::string NodeId;
typedef std::string PortId;

struct Port
{
    enum Type
    {
        Unknown = 0,
        Input = 1,
        Output = 2,
        Both = Input | Output
    };
    PortId id;
    Type type = Type::Unknown;

    bool operator==(const Port& other) const { return id == other.id && type == other.type; }

    bool operator!=(const Port& other) const { return !(*this == other); }
};

struct ConnectionId
{
    PortId start_port;
    PortId end_port;

    struct Hash
    {
        size_t operator()(const ConnectionId& value) const
        {
            size_t hash = 0;
            base::utils::hash_combine(hash, value.start_port, value.end_port);
            return hash;
        }
    };
    bool operator==(const ConnectionId& other) const { return start_port == other.start_port && end_port == other.end_port; }
    bool operator!=(const ConnectionId& other) const { return !(*this == other); }
};

class OPENDCC_UI_NODE_EDITOR_API GraphModel : public QObject
{
    Q_OBJECT
public:
    GraphModel(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    virtual QVector<NodeId> get_nodes() const = 0;
    virtual QVector<ConnectionId> get_connections() const = 0;
    virtual QVector<ConnectionId> get_connections_for_node(const NodeId& node_id) const = 0;

    virtual QPointF get_node_position(const NodeId& node_id) const = 0;
    virtual NodeId get_node_id_from_port(const PortId& port) const = 0;

    virtual bool can_rename(const NodeId& old_name, const NodeId& new_name) const = 0;
    virtual bool rename(const NodeId& old_name, const NodeId& new_name) const = 0;
    virtual bool can_connect(const Port& start_port, const Port& end_port) const = 0;
    virtual bool connect_ports(const Port& start_port, const Port& end_port) = 0;
    virtual bool has_port(const PortId& port) const = 0;
    virtual void delete_connection(const ConnectionId& connection) = 0;
    virtual void remove(const QVector<NodeId>& nodes, const QVector<ConnectionId>& connections) = 0;
Q_SIGNALS:
    void node_created(const std::string& node_id);
    void node_updated(const std::string& node_id);
    void connection_created(const ConnectionId& connection_id);
    void node_removed(const std::string& node);
    void connection_removed(const ConnectionId& connection);
    void node_moved(const std::string& node, QPointF pos);
    void selection_changed(const QVector<std::string>& nodes, const QVector<OPENDCC_NAMESPACE::ConnectionId>& connections);
    void port_updated(const std::string& port_id);
    void model_reset();
};

OPENDCC_NAMESPACE_CLOSE
