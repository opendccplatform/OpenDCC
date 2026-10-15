/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <stdint.h>
#include <string>

#include "opendcc/anim_engine/curve/curve.h"
#include "opendcc/anim_engine/ui/graph_editor/graph_editor.h"
#include "opendcc/app/core/undo/stack.h"

OPENDCC_NAMESPACE_OPEN

class SplineWidgetCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    virtual void finalize() = 0;
    virtual void set_initial_state(const std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SplineWidget::CurveData>& widget_curves) = 0;
};

class ChangeSelectionCommand : public SplineWidgetCommand
{
public:
    virtual void set_initial_state(const std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SplineWidget::CurveData>& widget_curves) override;
    virtual ~ChangeSelectionCommand();
    virtual void finalize() override;
    virtual void redo() override;
    virtual void undo() override;
    virtual bool merge_with(base::commands::UndoCommand* command) override;

    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
    virtual base::commands::CommandArgs make_args() const override;

private:
    std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SelectionInfo> start_selection_data_map;
    std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SelectionInfo> end_selection_data_map;
    const std::map<OPENDCC_NAMESPACE::AnimEngine::CurveId, SplineWidget::CurveData>* m_widget_curves = nullptr; // for finalize method only
};

OPENDCC_NAMESPACE_CLOSE
