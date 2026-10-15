// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/close_curve_command.h"

#include "opendcc/base/commands_api/core/command.h"

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::UndoCommand;
using base::commands::CommandResult;

CloseCurveCommand::CloseCurveCommand(BezierCurvePtr curve, BezierToolContext* context, const BezierCurve::Point& point)
    : m_curve(curve)
    , m_context(context)
    , m_point(point)
{
    m_command_name = "CloseCurveCommand";
}

CommandResult CloseCurveCommand::execute(const CommandArgs& args) /* override */
{
    return CommandResult(CommandResult::Status::SUCCESS);
}

void CloseCurveCommand::undo() /* override */
{
    do_cmd();
}

void CloseCurveCommand::redo() /* override */
{
    do_cmd();
}

bool CloseCurveCommand::merge_with(UndoCommand* command) /* override */
{
    return false;
}

CommandArgs CloseCurveCommand::make_args() const /* override */
{
    return CommandArgs();
}

void CloseCurveCommand::do_cmd()
{
    const auto first = 0;

    if (m_curve->is_close())
    {
        const auto point = m_curve->first();
        const auto last = m_curve->size() - 1;
        m_context->set_select_curve_point_index(last);
        m_curve->open();
        m_curve->set_point(first, m_point);
        m_point = point;
    }
    else
    {
        const auto point = m_curve->first();
        m_context->set_select_curve_point_index(first);
        m_curve->close();
        m_curve->set_point(first, m_point);
        m_point = point;
    }
}

OPENDCC_NAMESPACE_CLOSE
