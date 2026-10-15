/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/undo/inverse.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/selection_list.h"
#include "opendcc/app/viewport/viewport_scale_manipulator.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usdGeom/pointBased.h>
#include <pxr/usd/usdGeom/pointInstancer.h>

#include <memory>

OPENDCC_NAMESPACE_OPEN

class SelectionList;

class ViewportScaleToolCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    virtual ~ViewportScaleToolCommand() = default;

    virtual void undo() override;
    virtual void redo() override;

    void set_initial_state(const SelectionList& selection);
    void start_block();
    void end_block();
    bool is_recording() const;
    void apply_delta(const PXR_NS::GfVec3d& delta);
    bool can_edit() const;
    bool get_start_gizmo_data(ViewportScaleManipulator::GizmoData& result);

    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
    virtual base::commands::CommandArgs make_args() const override;

    static std::string cmd_name;
    static base::commands::CommandSyntax cmd_syntax();
    static std::shared_ptr<base::commands::Command> create_cmd();

private:
    struct TransformData
    {
        PXR_NS::UsdGeomXformable xform;
        PXR_NS::GfMatrix4d transform;
        PXR_NS::GfMatrix4d parent_transform;
        PXR_NS::GfMatrix4d local;
        PXR_NS::GfVec3f local_scale;
        PXR_NS::GfVec3f pivot;

        bool operator>(const TransformData& other) const;
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

    struct InstancerData
    {
        PXR_NS::UsdGeomPointInstancer point_instancer;
        std::vector<SelectionList::IndexType> indices;
        PXR_NS::VtVec3fArray local_scales;
    };

    std::vector<TransformData> m_prim_transforms;
    std::vector<PointsDelta> m_points_delta;
    std::vector<InstancerData> m_instancer_data;
    PXR_NS::GfVec3f m_pivot;
    PXR_NS::GfVec3d m_scale_delta;
    SelectionList m_selection;
    std::unique_ptr<commands::UndoInverse> m_inverse;
    std::unique_ptr<commands::UsdEditsBlock> m_change_block;
    ViewportScaleManipulator::GizmoData m_start_gizmo_data;
    bool m_can_edit = false;
};

OPENDCC_NAMESPACE_CLOSE
