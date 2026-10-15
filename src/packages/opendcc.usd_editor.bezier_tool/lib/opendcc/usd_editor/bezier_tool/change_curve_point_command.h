/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/usd_editor/bezier_tool/bezier_curve.h"
#include "opendcc/usd_editor/bezier_tool/bezier_tool_context.h"

#include <pxr/base/gf/vec3f.h>

#include <memory>

OPENDCC_NAMESPACE_OPEN

class ChangeCurvePointCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    ChangeCurvePointCommand(const std::string& action_name, BezierCurvePtr curve, BezierToolContext* context, size_t point_index,
                            const BezierCurve::Point& point);

    base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

    void undo() override;
    void redo() override;

    bool merge_with(base::commands::UndoCommand* command) override;

    base::commands::CommandArgs make_args() const override;

private:
    void do_cmd();

    BezierCurvePtr m_curve;
    BezierCurve::Point m_point;
    size_t m_point_index;
    BezierToolContext* m_context = nullptr;
};

OPENDCC_NAMESPACE_CLOSE
