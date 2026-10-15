/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/usd_editor/common_tools/viewport_move_tool_context.h"

#include <memory>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class UndoInverse;
};

class SelectionList;

class ViewportMoveToolCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    ViewportMoveToolCommand() = default;
    virtual ~ViewportMoveToolCommand() override = default;

    virtual void undo() override;
    virtual void redo() override;

    void set_initial_state(const SelectionList& list, ViewportMoveToolContext::AxisOrientation orientation);
    void start_block();
    void end_block();
    bool is_recording() const;
    void apply_delta(const PXR_NS::GfVec3d& delta);
    bool can_edit() const;
    bool get_start_gizmo_matrix(PXR_NS::GfMatrix4d& result);
    SelectionList get_selection() const;

    virtual base::commands::CommandArgs make_args() const override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

    static std::string cmd_name;
    static base::commands::CommandSyntax cmd_syntax();
    static std::shared_ptr<base::commands::Command> create_cmd();

private:
    struct TransformData
    {
        PXR_NS::UsdGeomXformable xform;
        PXR_NS::GfMatrix4d parent_transform;
        PXR_NS::GfMatrix4d transform;
        PXR_NS::GfMatrix4d local;
        PXR_NS::GfVec3f local_pivot_pos;

        bool operator>(const TransformData& other) const;
        bool operator<(const TransformData& other) const;
    };

    struct InstancerData
    {
        PXR_NS::UsdGeomPointInstancer point_instancer;
        std::vector<SelectionList::IndexType> indices;
        PXR_NS::VtMatrix4dArray local_xforms;
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

    std::vector<TransformData> m_prim_transforms;
    std::vector<InstancerData> m_instancer_data;
    std::vector<PointsDelta> m_points_delta;
    PXR_NS::GfMatrix4d m_start_gizmo_matrix;
    SelectionList m_selection;
    PXR_NS::GfVec3d m_move_delta;
    ViewportMoveToolContext::AxisOrientation m_orientation;
    std::unique_ptr<commands::UndoInverse> m_inverse;
    std::unique_ptr<commands::UsdEditsBlock> m_change_block;
    bool m_can_edit;
};

OPENDCC_NAMESPACE_CLOSE
