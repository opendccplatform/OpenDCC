/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_context.h"
#include "opendcc/usd_editor/common_tools/viewport_rotate_tool_command.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/undo/block.h"

#include <pxr/usd/usdGeom/pointBased.h>

#include <memory>

OPENDCC_NAMESPACE_OPEN

class SelectionList;

class ViewportRotateToolCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    virtual ~ViewportRotateToolCommand() = default;

    virtual void undo() override;
    virtual void redo() override;

    void set_initial_state(const SelectionList& selection, ViewportRotateToolContext::Orientation orientation);
    void start_block();
    void end_block();
    bool is_recording() const;
    bool can_edit() const;
    void apply_delta(const PXR_NS::GfRotation& delta);
    bool get_start_gizmo_data(ViewportRotateManipulator::GizmoData& gizmo_data) const;
    bool affects_components() const;

    virtual base::commands::CommandArgs make_args() const override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

    static std::string cmd_name;
    static base::commands::CommandSyntax cmd_syntax();
    static std::shared_ptr<base::commands::Command> create_cmd();

private:
    struct InstancerData
    {
        PXR_NS::UsdGeomPointInstancer point_instancer;
        std::vector<SelectionList::IndexType> indices;
        PXR_NS::VtMatrix4dArray local_xforms;
    };

    struct TransformData
    {
        PXR_NS::UsdGeomXformable xform;
        PXR_NS::GfMatrix4d parent_transform;
        PXR_NS::GfMatrix4d transform;
        PXR_NS::GfMatrix4d inv_transform;
        PXR_NS::GfMatrix4d local;
        PXR_NS::GfVec3f local_angles;
        PXR_NS::GfVec3f scale;
        PXR_NS::GfVec3f pivot;
        PXR_NS::UsdGeomXformCommonAPI::RotationOrder rot_order;

        bool operator>(const TransformData& other) const;
        bool operator<(const TransformData& other) const;
    };

    struct PointsDelta
    {
        PXR_NS::UsdGeomPointBased point_based;
        struct WeightedPoint
        {
            PXR_NS::GfVec3f point;
            float weight;
        };
        std::unordered_map<SelectionList::IndexType, WeightedPoint> start_points;
    };

    std::vector<InstancerData> m_instancer_data;
    std::vector<TransformData> m_prim_transforms;
    std::vector<PointsDelta> m_points_delta;
    PXR_NS::GfVec3f m_pivot;
    SelectionList m_selection;
    PXR_NS::GfRotation m_rotate_delta;
    std::unique_ptr<commands::UndoInverse> m_inverse;
    std::unique_ptr<commands::UsdEditsBlock> m_change_block;
    ViewportRotateManipulator::GizmoData m_start_gizmo_data;
    ViewportRotateToolContext::Orientation m_orientation;
    bool m_can_edit;
};

OPENDCC_NAMESPACE_CLOSE
