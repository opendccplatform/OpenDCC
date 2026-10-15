/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/selection_list.h"

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class PickWalkCommand : public base::commands::UndoCommand
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        virtual void undo() override;
        virtual void redo() override;

        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        void do_cmd();

        SelectionList m_old_selection;
    };
}

OPENDCC_NAMESPACE_CLOSE
