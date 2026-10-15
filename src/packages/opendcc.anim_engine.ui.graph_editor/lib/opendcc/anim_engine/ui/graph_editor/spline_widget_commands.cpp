// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include "opendcc/anim_engine/ui/graph_editor/spline_widget.h"
#include "opendcc/anim_engine/ui/graph_editor/spline_widget_commands.h"
#include "opendcc/anim_engine/ui/graph_editor/utils.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<ChangeSelectionCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command("spline_widget_selection",
                                      CommandSyntax()
                                          .arg<std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SelectionInfo>>("start_sel_data")
                                          .arg<std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SelectionInfo>>("end_sel_data"),
                                      [] { return std::make_shared<ChangeSelectionCommand>(); });
}

bool SelectionInfo::operator==(const SelectionInfo& other) const
{
    return (selected_keys == other.selected_keys) && (selected_tangents == other.selected_tangents);
}

bool SelectionInfo::operator!=(const SelectionInfo& other) const
{
    return !(*this == other);
}

void ChangeSelectionCommand::set_initial_state(const std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SplineWidget::CurveData>& widget_curves)
{
    m_widget_curves = &widget_curves;
    get_selection_info(widget_curves, start_selection_data_map);
}

void ChangeSelectionCommand::finalize()
{
    get_selection_info(*m_widget_curves, end_selection_data_map);
}

ChangeSelectionCommand::~ChangeSelectionCommand() {}

void ChangeSelectionCommand::redo()
{
    global_selection_dispatcher().dispatch(SelectionEvent::SELECTION_CHANGED, end_selection_data_map);
}

void ChangeSelectionCommand::undo()
{
    global_selection_dispatcher().dispatch(SelectionEvent::SELECTION_CHANGED, start_selection_data_map);
}

bool ChangeSelectionCommand::merge_with(UndoCommand* command)
{
    const ChangeSelectionCommand* other = dynamic_cast<const ChangeSelectionCommand*>(command);
    if (command->get_command_name() != get_command_name() || (other == nullptr))
    {
        return false;
    }

    return this->end_selection_data_map == other->end_selection_data_map;
}

CommandResult ChangeSelectionCommand::execute(const CommandArgs& args)
{
    start_selection_data_map = *args.get_arg<std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SelectionInfo>>(0);
    end_selection_data_map = *args.get_arg<std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SelectionInfo>>(1);
    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

CommandArgs ChangeSelectionCommand::make_args() const
{
    return CommandArgs().arg(start_selection_data_map).arg(end_selection_data_map);
}
OPENDCC_NAMESPACE_CLOSE
