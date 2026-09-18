// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/node_editor/layout.h"
#include "opendcc/ui/node_editor/node.h"
#include "opendcc/ui/node_editor/connection.h"
#include "opendcc/ui/node_editor/graph_model.h"

#include <graphviz/gvc.h>
#include <graphviz/cgraph.h>
#include <sstream>

#include <QDir>
#include <QProcess>
#include <QCoreApplication>

OPENDCC_NAMESPACE_OPEN
bool layout_items(const QVector<NodeItem*>& items, bool vertical)
{
    if (items.empty())
        return true;

#ifdef OPENDCC_OS_WINDOWS
    const auto unflatten_path = QString("unflatten.exe");
    const auto dot_path = QString("dot.exe");
#else
    const auto unflatten_path = QString("unflatten");
    const auto dot_path = QString("dot");
#endif

    Agraph_t* graph = agopen(const_cast<char*>("G"), Agdirected, nullptr);

    std::unordered_map<NodeItem*, Agnode_t*> vertices;

    auto add_vertex = [&vertices, &graph](NodeItem* item) {
        Agnode_t* v = agnode(graph, const_cast<char*>(item->get_id().c_str()), 1);
        vertices[item] = v;
        auto w = std::to_string((item->childrenBoundingRect() | item->boundingRect()).width() / 72);
        auto h = std::to_string((item->childrenBoundingRect() | item->boundingRect()).height() / 72);
        agsafeset(v, (char*)"width", const_cast<char*>(w.c_str()), (char*)"0.75");
        agsafeset(v, (char*)"height", const_cast<char*>(h.c_str()), (char*)"0.75");
        return v;
    };

    auto scene = items[0]->get_scene();

    for (const auto item : items)
    {
        Agnode_t* v;
        auto vert_iter = vertices.find(item);
        if (vert_iter == vertices.end())
            v = add_vertex(item);
        else
            v = vert_iter->second;

        const auto& model = item->get_model();
        auto connections = model.get_connections_for_node(item->get_id());
        for (const auto& con : connections)
        {
            const auto output = model.get_node_id_from_port(con.end_port);
            if (output == item->get_id())
                continue;

            auto input_item = item->get_scene()->get_item_for_node(output);
            if (!input_item)
                continue;
            auto vert_iter = vertices.find(input_item);
            if (vert_iter == vertices.end())
            {
                const auto input_v = add_vertex(input_item);
                agedge(graph, v, input_v, nullptr, 1);
            }
            else
            {
                agedge(graph, v, vert_iter->second, nullptr, 1);
            }
        }
    }

    std::function<QString(const QString& proc, const QStringList& args, const QString& input)> run_proc =
        [](const QString& proc, const QStringList& args, const QString& input) {
            QString result;
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(proc, args);
            if (p.waitForStarted())
            {
                p.write((input + '\n').toLatin1());
                p.closeWriteChannel();
                if (p.waitForFinished())
                {
                    result = p.readAllStandardOutput();
                }
            }

            return result;
        };

    GVC_t* gvc = gvContext();
    char* dummy_dot;
    uint32_t dummy_length;

    gvLayout(gvc, graph, "dot");
    auto res = gvRenderData(gvc, graph, "dot", &dummy_dot, &dummy_length);

    auto unflatten = run_proc(unflatten_path,
                              QStringList() << "-c 4"
                                            << "-f"
                                            << "-l 3",
                              QString(dummy_dot));
    const char* rankdir;
    if (vertical)
        rankdir = "-Grankdir=TB";
    else
        rankdir = "-Grankdir=LR";

    auto layout = run_proc(dot_path,
                           QStringList() << "-Tdot"
                                         << "-y"
                                         << "-Granksep=\"1.2 equally\""
                                         << "-Nshape=rect"
                                         << "-Nfixedsize=true"
                                         << "-q" << rankdir,
                           unflatten);

    gvFreeRenderData(dummy_dot);
    gvFreeLayout(gvc, graph);
    gvFreeContext(gvc);
    agclose(graph);

    graph = agmemread(layout.toLocal8Bit().data());
    QVector<NodeId> nodes_to_move;
    QVector<QPointF> nodes_positions;
    for (auto n = agfstnode(graph); n != nullptr; n = agnxtnode(graph, n))
    {
        const auto id = agnameof(n);
        auto item = scene->get_item_for_node(id);
        if (!item)
        {
            continue;
        }

        if (auto pos_str = agget(n, (char*)"pos"))
        {
            std::stringstream pos_stream { pos_str };
            float x, y;
            char sep;
            pos_stream >> x >> sep >> y;

            QPointF pos { x, y };
            const auto old_pos = item->pos();
            pos -= item->boundingRect().center();
            nodes_positions.push_back(pos);
            nodes_to_move.push_back(item->get_id());
        }
    }

    scene->begin_move(nodes_to_move);
    for (int i = 0; i < nodes_to_move.size(); ++i)
    {
        auto item = scene->get_item_for_node(nodes_to_move[i]);
        item->setPos(nodes_positions[i]);
    }
    scene->end_move();

    agclose(graph);
    return true;
}

OPENDCC_NAMESPACE_CLOSE
