/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/selection_list.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class ExportSelectionCommand : public base::commands::Command
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();
    };
}

OPENDCC_NAMESPACE_CLOSE
