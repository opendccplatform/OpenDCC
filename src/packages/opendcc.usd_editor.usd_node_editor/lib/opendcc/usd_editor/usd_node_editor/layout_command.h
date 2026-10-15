/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/usd_node_editor/api.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/ui/node_editor/graph_model.h"
#include <unordered_map>
#include <QPointF>
#include <QVector>

OPENDCC_NAMESPACE_OPEN

class NodeEditorScene;

class OPENDCC_USD_NODE_EDITOR_API NodeEditorLayoutCommand : public base::commands::UndoCommand
{
public:
    NodeEditorLayoutCommand() = default;
    virtual ~NodeEditorLayoutCommand() override = default;

    virtual void undo() override;
    virtual void redo() override;

    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    NodeEditorScene* m_scene = nullptr;
    QVector<NodeId> m_nodes;
    QVector<QPointF> m_old_pos;
    QVector<QPointF> m_new_pos;
};

OPENDCC_NAMESPACE_CLOSE
