/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/editTarget.h>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    /** @brief Sets the local edit target and restores the previous target on undo. */
    class SetEditTargetCommand : public base::commands::UndoCommand
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        virtual void undo() override;
        virtual void redo() override;

        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        PXR_NS::UsdStageWeakPtr m_stage;
        PXR_NS::UsdEditTarget m_old_target;
        PXR_NS::SdfLayerRefPtr m_new_layer;
    };
}

OPENDCC_NAMESPACE_CLOSE
