/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/core/undo/block.h"

#include <pxr/base/gf/rotation.h>
#include <pxr/usd/usdGeom/xformable.h>

OPENDCC_NAMESPACE_OPEN

class ViewportChangePivotCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    struct PivotInfo
    {
        PXR_NS::GfVec3d position = PXR_NS::GfVec3d(0);
        PXR_NS::GfRotation orientation = PXR_NS::GfRotation();
    };

    void set_initial_state(const SelectionList& selection);
    bool get_pivot_info(PivotInfo& pivot_info) const;
    void apply_delta(const PXR_NS::GfVec3d& delta_pos, const PXR_NS::GfRotation& delta_rotation = PXR_NS::GfRotation(PXR_NS::GfVec3d::XAxis(), 0));
    virtual void undo() override;
    virtual void redo() override;

    void start_block();
    void end_block();
    bool is_recording() const;
    bool can_edit() const;

    virtual base::commands::CommandArgs make_args() const override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

    static std::string cmd_name;
    static base::commands::CommandSyntax cmd_syntax();
    static std::shared_ptr<base::commands::Command> create_cmd();

private:
    std::unique_ptr<PivotInfo> m_start_pivot_info;
    struct PivotTransform
    {
        PXR_NS::UsdGeomXformable xform;
        PXR_NS::GfVec3d local_position;
        PXR_NS::GfMatrix4d world_transform;
        PXR_NS::GfMatrix4d local_transform;
        PXR_NS::GfMatrix4d inv_world_transform;
    };
    SelectionList m_selection;
    PXR_NS::GfVec3d m_delta_move;
    PXR_NS::GfRotation m_delta_rot;
    std::vector<PivotTransform> m_pivot_transforms;
    std::unique_ptr<commands::UndoInverse> m_inverse;
    std::unique_ptr<commands::UsdEditsBlock> m_change_block;
    bool m_can_edit = false;
};

OPENDCC_NAMESPACE_CLOSE
