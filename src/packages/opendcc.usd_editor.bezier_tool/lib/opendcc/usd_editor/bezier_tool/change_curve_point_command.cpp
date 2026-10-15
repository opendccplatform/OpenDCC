// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/change_curve_point_command.h"

#include "opendcc/base/commands_api/core/command.h"

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::UndoCommand;
using base::commands::CommandResult;

ChangeCurvePointCommand::ChangeCurvePointCommand(const std::string& action_name, BezierCurvePtr curve, BezierToolContext* context, size_t point_index,
                                                 const BezierCurve::Point& point)
    : m_curve(curve)
    , m_context(context)
    , m_point_index(point_index)
    , m_point(point)
{
    m_command_name = action_name + "CurvePoint";
}

CommandResult ChangeCurvePointCommand::execute(const CommandArgs& args) /* override */
{
    return CommandResult(CommandResult::Status::SUCCESS);
}

void ChangeCurvePointCommand::undo() /* override */
{
    do_cmd();
}

void ChangeCurvePointCommand::redo() /* override */
{
    do_cmd();
}

bool ChangeCurvePointCommand::merge_with(UndoCommand* command) /* override */
{
    return false;
}

CommandArgs ChangeCurvePointCommand::make_args() const /* override */
{
    return CommandArgs();
}

void ChangeCurvePointCommand::do_cmd()
{
    const auto point = m_curve->get_point(m_point_index);
    m_curve->set_point(m_point_index, m_point);
    m_context->set_select_curve_point_index(m_point_index);
    m_point = point;
}

OPENDCC_NAMESPACE_CLOSE
