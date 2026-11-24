/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/app/core/undo/inverse.h"

#include <memory>
#include <vector>
#include <pxr/usd/sdf/layer.h>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    /** @brief Replaces layer contents from USDA text through the undo router. */
    class ApplyLayerTextCommand : public base::commands::UndoCommand
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

        PXR_NS::SdfLayerRefPtr m_layer;
        PXR_NS::SdfLayerRefPtr m_contents;
        std::vector<PXR_NS::SdfLayerRefPtr> m_retained_layers;
        std::unique_ptr<commands::UndoInverse> m_inverse;
    };
}

OPENDCC_NAMESPACE_CLOSE
