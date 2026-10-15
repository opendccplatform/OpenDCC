/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/base/commands_api/core/command.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN
namespace commands
{
    class ParentPrimCommand;

    class GroupPrimToNodeGraphCommand : public base::commands::UndoCommand
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        virtual void undo() override;
        virtual void redo() override;

    private:
        PXR_NS::SdfPath define_group_root(PXR_NS::UsdStageWeakPtr stage, const PXR_NS::SdfPathVector& prim_paths) const;
        SelectionList m_old_selection;
        SelectionList m_new_selection;
        std::unique_ptr<commands::UndoInverse> m_create_cmd;
        std::unique_ptr<ParentPrimCommand> m_parent_cmd;
    };
}

OPENDCC_NAMESPACE_CLOSE
