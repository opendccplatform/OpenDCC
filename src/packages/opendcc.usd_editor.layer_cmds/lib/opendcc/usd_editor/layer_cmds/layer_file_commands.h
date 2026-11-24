/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/undo/inverse.h"
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <memory>
#include <vector>

OPENDCC_NAMESPACE_OPEN
namespace commands
{
    class SaveLayerCommand : public base::commands::Command
    {
    public:
        base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();
    };

    class RetargetLayerCommand : public base::commands::UndoCommand
    {
    public:
        base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        void undo() override;
        void redo() override;
        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        PXR_NS::UsdStageWeakPtr m_stage;
        PXR_NS::UsdEditTarget m_old_edit_target;
        bool m_retarget_edit_target = false;
        PXR_NS::SdfLayerRefPtr m_source;
        PXR_NS::SdfLayerRefPtr m_target;
        PXR_NS::SdfLayerRefPtr m_before;
        PXR_NS::SdfLayerRefPtr m_after;
        std::vector<PXR_NS::SdfLayerRefPtr> m_retained_layers;
        bool m_target_was_dirty = false;
        std::vector<PXR_NS::SdfLayerRefPtr> m_parents;
        std::vector<std::vector<std::string>> m_paths;
        std::vector<std::vector<PXR_NS::SdfLayerOffset>> m_offsets;
        void invert_paths();
        std::unique_ptr<UndoInverse> m_inverse;
    };

    class ReloadLayerCommand : public base::commands::UndoCommand
    {
    public:
        base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
        void undo() override;
        void redo() override;
        static std::string cmd_name;
        static base::commands::CommandSyntax cmd_syntax();
        static std::shared_ptr<base::commands::Command> create_cmd();

    private:
        void restore(const PXR_NS::SdfLayerRefPtr& contents, bool dirty);
        PXR_NS::SdfLayerRefPtr m_layer;
        PXR_NS::SdfLayerRefPtr m_before;
        PXR_NS::SdfLayerRefPtr m_after;
        std::vector<PXR_NS::SdfLayerRefPtr> m_retained_layers;
        bool m_was_dirty = false;
    };
}
OPENDCC_NAMESPACE_CLOSE
