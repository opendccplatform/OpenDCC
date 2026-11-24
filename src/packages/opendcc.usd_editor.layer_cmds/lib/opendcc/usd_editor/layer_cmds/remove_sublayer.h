/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/app/core/undo/inverse.h"

#include <pxr/usd/sdf/layer.h>

#include <memory>
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    /** @brief Removes a sublayer path without deleting the layer. */
    class RemoveSubLayerCommand : public base::commands::UndoCommand
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

        PXR_NS::SdfLayerRefPtr m_parent;
        std::vector<std::string> m_paths;
        std::vector<PXR_NS::SdfLayerOffset> m_offsets;
        std::unique_ptr<commands::UndoInverse> m_inverse;
        std::vector<PXR_NS::SdfLayerRefPtr> m_layers;
    };
}

OPENDCC_NAMESPACE_CLOSE
