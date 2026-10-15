// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/center_pivot.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
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
                    auto bbox = bbox_cache.ComputeLocalBound(prim);
                    auto center = GfVec3f(bbox.ComputeCentroid());
                    GfMatrix4d matrix;
                    bool resetsXformStack = false;
                    xformable_prim.GetLocalTransformation(&matrix, &resetsXformStack, Application::instance().get_current_time());
                    center = GfVec3f(matrix.GetInverse().Transform(center));
                    xform_api.SetPivot(center);
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
