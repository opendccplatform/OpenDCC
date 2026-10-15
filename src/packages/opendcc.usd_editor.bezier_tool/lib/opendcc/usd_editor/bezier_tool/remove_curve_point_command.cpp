// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/remove_curve_point_command.h"

#include "opendcc/base/commands_api/core/command.h"

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::UndoCommand;
using base::commands::CommandResult;

RemoveCurvePointCommand::RemoveCurvePointCommand(BezierCurvePtr curve, BezierToolContext* context, size_t point_index,
                                                 const BezierCurve::Point& point, bool change_close)
    : m_curve(curve)
    , m_context(context)
    , m_point_index(point_index)
    , m_point(point)
    , m_change_close(change_close)
{
    m_command_name = "RemoveCurvePointCommand";
}

CommandResult RemoveCurvePointCommand::execute(const CommandArgs& args) /* override */
{
    return CommandResult(CommandResult::Status::SUCCESS);
}

void RemoveCurvePointCommand::undo() /* override */
{
    if (m_change_close && !m_curve->is_close())
    {
        m_curve->insert_point(m_point_index, m_point);
        m_curve->remove_point(m_curve->size() - 1);
        m_curve->close();
    }
    else
    {
        m_curve->insert_point(m_point_index, m_point);
        m_context->set_select_curve_point_index(m_point_index);
    }
}

void RemoveCurvePointCommand::redo() /* override */
{
    m_curve->remove_point(m_point_index);
    const auto size = m_curve->size();
    m_context->set_select_curve_point_index(size ? qBound(size_t(0), m_point_index, size - 1) : BezierCurve::s_invalid_index);
}

bool RemoveCurvePointCommand::merge_with(UndoCommand* command) /* override */
{
    return false;
}

CommandArgs RemoveCurvePointCommand::make_args() const /* override */
{
    return CommandArgs();
}

OPENDCC_NAMESPACE_CLOSE
