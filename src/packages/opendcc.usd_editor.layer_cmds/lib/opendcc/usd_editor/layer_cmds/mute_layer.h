/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"

#include <pxr/usd/usd/stage.h>
#include <string>
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    /** @brief Mutes or unmutes a layer, retaining its contents for undo. */
    class MuteLayerCommand : public base::commands::UndoCommand
    {
    public:
        virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        virtual void undo() override;
        virtual void redo() override;

        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        void set_muted(bool muted);

        PXR_NS::UsdStageWeakPtr m_stage;
        std::vector<PXR_NS::SdfLayerRefPtr> m_layers;
        std::string m_identifier;
        bool m_old_muted = false;
        bool m_new_muted = false;
    };
}

OPENDCC_NAMESPACE_CLOSE
