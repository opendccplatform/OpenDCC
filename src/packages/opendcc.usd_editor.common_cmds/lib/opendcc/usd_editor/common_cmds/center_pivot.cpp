// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/center_pivot.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

CommandSyntax commands::CenterPivotCommand::cmd_syntax()
{
    return CommandSyntax();
}

std::shared_ptr<Command> commands::CenterPivotCommand::create_cmd()
{
    return std::make_shared<commands::CenterPivotCommand>();
}

std::string commands::CenterPivotCommand::cmd_name = "center_pivot";

CommandResult commands::CenterPivotCommand::execute(const CommandArgs& args)
{
    auto session = Application::instance().get_session();
    UsdStageWeakPtr stage = Application::instance().get_session()->get_current_stage();

    if (!stage)
    {
        OPENDCC_WARN("Failed to center pivot: stage doesn't exist.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    SelectionList selection = Application::instance().get_selection();
    if (selection.empty())
    {
        OPENDCC_WARN("Failed to center pivot: nothing is selected.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    auto bbox_cache = session->get_stage_bbox_cache(session->get_current_stage_id());
    const auto time = UsdTimeCode(Application::instance().get_current_time());

    UsdEditsBlock block;

    for (auto& sel_data : selection)
    {
        auto prim_path = sel_data.first;
        if (auto prim = stage->GetPrimAtPath(prim_path))
        {
            auto xformable_prim = UsdGeomXformable(prim);
            if (xformable_prim)
            {
                auto xform_api = UsdGeomXformCommonAPI(xformable_prim);
                if (xform_api)
                {
                    const auto bbox = bbox_cache.ComputeUntransformedBound(prim);
                    if (bbox.GetRange().IsEmpty())
                        continue;

                    GfVec3d translation;
                    GfVec3f rotation, scale, pivot;
                    UsdGeomXformCommonAPI::RotationOrder rotation_order;
                    if (!xform_api.GetXformVectors(&translation, &rotation, &scale, &pivot, &rotation_order, time))
                        continue;

                    const auto center = GfVec3f(bbox.ComputeCentroid());
                    GfMatrix4d matrix;
                    bool resetsXformStack = false;
                    xformable_prim.GetLocalTransformation(&matrix, &resetsXformStack, time);
                    const auto pivot_delta = GfVec3d(center) - GfVec3d(pivot);
                    // Changing the scale/rotate pivot must not change the composed transform.
                    translation += matrix.TransformDir(pivot_delta) - pivot_delta;

                    const auto animated = xformable_prim.TransformMightBeTimeVarying();
                    const auto pivot_time = animated ? time : manipulator_utils::get_non_varying_time(
                                                                 prim.GetAttribute(TfToken("xformOp:translate:pivot")));
                    const auto translation_time = animated ? time : manipulator_utils::get_non_varying_time(
                                                                       prim.GetAttribute(TfToken("xformOp:translate")));
                    const auto ops = xform_api.CreateXformOps(UsdGeomXformCommonAPI::OpTranslate, UsdGeomXformCommonAPI::OpPivot);
                    if (!ops.translateOp || !ops.pivotOp)
                        continue;

                    SdfChangeBlock change_block;
                    ops.pivotOp.Set(center, pivot_time);
                    ops.translateOp.Set(translation, translation_time);
                }
            }
        }
    }

    m_inverse = block.take_edits();

    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::CenterPivotCommand::undo()
{
    do_cmd();
}

void commands::CenterPivotCommand::redo()
{
    do_cmd();
}

void commands::CenterPivotCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}
OPENDCC_NAMESPACE_CLOSE
