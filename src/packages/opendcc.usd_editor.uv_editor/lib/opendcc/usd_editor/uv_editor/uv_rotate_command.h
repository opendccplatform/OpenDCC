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

#include <pxr/base/gf/vec2f.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

OPENDCC_NAMESPACE_OPEN

class UVEditorGLWidget;

class UvRotateCommand final
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    UvRotateCommand() = default;
    virtual ~UvRotateCommand() override = default;

    // Command
    base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

    // UndoCommand
    void undo() override;
    void redo() override;

    // ToolCommand
    base::commands::CommandArgs make_args() const override;

    //
    void init_from_mesh_selection(UVEditorGLWidget* widget, const SelectionList& mesh_list);
    void init_from_uv_selection(UVEditorGLWidget* widget, const SelectionList& uv_list);

    void start();
    void end();

    bool is_started() const;

    void apply_rotate(double angle);

    const PXR_NS::GfVec2f& get_centroid() const;

private:
    void do_cmd();

    SelectionData::IndexIntervals to_points_indices(const PXR_NS::UsdPrim& prim, const SelectionData& mesh_data) const;
    SelectionData::IndexIntervals to_uv_points_indices(UVEditorGLWidget* widget, const PXR_NS::SdfPath& path,
                                                       const SelectionData::IndexIntervals mesh_indices) const;

    struct PointsData
    {
        PXR_NS::UsdGeomPrimvarsAPI primvars;
        struct WeightedPoint
        {
            PXR_NS::GfVec2f point;
            float weight = 1.0f;
        };
        std::unordered_map<SelectionList::IndexType, WeightedPoint> start_points;
    };
    std::vector<PointsData> m_selection;

    std::unique_ptr<commands::UndoInverse> m_inverse;
    std::unique_ptr<commands::UsdEditsBlock> m_change_block;

    UVEditorGLWidget* m_widget;

    PXR_NS::GfVec2f m_centroid;
};

OPENDCC_NAMESPACE_CLOSE
