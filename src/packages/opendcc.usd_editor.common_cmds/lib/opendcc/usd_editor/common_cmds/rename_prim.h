/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/usd_editor/common_cmds/api.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/command_utils.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class RenamePrimCommand;
    using RenameCommandNotifier = utils::CommandExecNotifier<RenamePrimCommand, PXR_NS::SdfPath, PXR_NS::SdfPath>;

    class OPENDCC_USD_EDITOR_COMMON_CMDS_API RenamePrimCommand : public base::commands::UndoCommand
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        virtual void undo() override;
        virtual void redo() override;

        static RenameCommandNotifier& get_notifier();

        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        PXR_NS::UsdPrim get_prim_to_rename() const;
        bool rename_prim() const;
        void update_selection(const PXR_NS::SdfPath& old_path, const PXR_NS::SdfPath& new_path) const;
        void do_cmd();

        PXR_NS::SdfPath m_old_path;
        PXR_NS::TfToken m_new_name;
        PXR_NS::UsdStageWeakPtr m_stage;
    };

}

OPENDCC_NAMESPACE_CLOSE
