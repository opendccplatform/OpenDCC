/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/args.h"

#include <pxr/usd/sdf/layer.h>
#include <memory>
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    /** @brief Moves a sublayer while preserving its layer offset. */
    class MoveSubLayerCommand : public base::commands::UndoCommand
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

        struct ParentState
        {
            PXR_NS::SdfLayerRefPtr layer;
            std::vector<std::string> paths;
            std::vector<PXR_NS::SdfLayerOffset> offsets;
            bool dirty;
        };

        std::vector<ParentState> m_parents;
        std::vector<PXR_NS::SdfLayerRefPtr> m_moved_layers;
    };
}

OPENDCC_NAMESPACE_CLOSE
