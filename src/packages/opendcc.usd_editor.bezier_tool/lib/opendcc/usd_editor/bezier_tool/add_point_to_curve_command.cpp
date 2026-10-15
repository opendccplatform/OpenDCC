// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/add_point_to_curve_command.h"

#include "opendcc/base/commands_api/core/command.h"

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::UndoCommand;
using base::commands::CommandResult;

AddPointToCurveCommand::AddPointToCurveCommand(BezierCurvePtr curve, BezierToolContext* context, const BezierCurve::Point& point)
    : m_curve(curve)
    , m_point(point)
    , m_context(context)
{
    m_command_name = "AddPointToCurve";
}

CommandResult AddPointToCurveCommand::execute(const CommandArgs& args) /* override */
{
    return CommandResult(CommandResult::Status::SUCCESS);
}

void AddPointToCurveCommand::undo() /* override */
{
    const auto& info = m_context->get_info();

    const auto last = m_curve->size() - 1;
    const auto prev = info.select_curve_point_index - 1;
    m_context->set_select_curve_point_index(info.select_curve_point_index ? prev : BezierCurve::s_invalid_index);

    m_curve->pop_back();
}

void AddPointToCurveCommand::redo() /* override */
{
    const auto& info = m_context->get_info();

    const auto invalid = info.select_curve_point_index == BezierCurve::s_invalid_index;
    const auto next = info.select_curve_point_index + 1;
    m_context->set_select_curve_point_index(invalid ? 0 : next);

    m_curve->push_back(m_point);
}

bool AddPointToCurveCommand::merge_with(UndoCommand* command) /* override */
{
    return false;
}

CommandArgs AddPointToCurveCommand::make_args() const /* override */
{
    return CommandArgs();
}

OPENDCC_NAMESPACE_CLOSE
