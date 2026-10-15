/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/common_cmds/api.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/command_utils.h"

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class ParentPrimCommand;
    using ParentCommandNotifier = utils::CommandExecNotifier<ParentPrimCommand, PXR_NS::SdfPathVector, PXR_NS::SdfPathVector>;

    class OPENDCC_USD_EDITOR_COMMON_CMDS_API ParentPrimCommand : public base::commands::UndoCommand
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        virtual void undo() override;
        virtual void redo() override;

        static ParentCommandNotifier& get_notifier();

        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        PXR_NS::UsdPrim get_parent_prim(PXR_NS::UsdStageWeakPtr stage, const PXR_NS::SdfPath& prim_path) const;
        void do_cmd();

        SelectionList m_old_selection;
        PXR_NS::SdfPathVector m_old_paths;
        PXR_NS::SdfPathVector m_new_paths;
        std::unique_ptr<commands::UndoInverse> m_inverse;
    };
}

OPENDCC_NAMESPACE_CLOSE
