// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_tools/viewport_change_pivot_command.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/router.h"
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "opendcc/app/viewport/viewport_manipulator_utils.h"
#include "pxr/base/gf/transform.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string ViewportChangePivotCommand::cmd_name = "change_pivot";

CommandSyntax ViewportChangePivotCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<GfVec3d>("offset_delta", "Translation delta")
        .arg<GfRotation>("rotation_delta", "Rotation delta")
        .kwarg<SelectionList>("objects", "Affected objects");
}
std::shared_ptr<Command> ViewportChangePivotCommand::create_cmd()
{
    return std::make_shared<ViewportChangePivotCommand>();
}

void ViewportChangePivotCommand::set_initial_state(const SelectionList& selection)
{
    m_selection = selection;
    m_can_edit = false;
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage)
        return;

    const auto time = Application::instance().get_current_time();
    const auto selected_paths = selection.get_fully_selected_paths();
    UsdGeomXformCache cache(time);
    for (int i = selected_paths.size() - 1; i >= 0; i--)
    {
        const auto& path = selected_paths[i];
        auto prim = stage->GetPrimAtPath(path);
        if (!prim)
            continue;
        auto xform = UsdGeomXformable(prim);
        if (!xform)
            continue;
        const auto is_time_varying = cache.TransformMightBeTimeVarying(prim);
        if (!is_time_varying && (i != 0 || !m_pivot_transforms.empty()))
            continue;
        m_can_edit = !is_time_varying;

        GfVec3d translation;
        GfVec3f rotation;
        GfVec3f scale;
        GfVec3f pivot;
        UsdGeomXformCommonAPI::RotationOrder rot_order;
        auto xform_api = UsdGeomXformCommonAPI(xform);
        if (!xform_api.GetXformVectorsByAccumulation(&translation, &rotation, &scale, &pivot, &rot_order, time))
        {
            pivot.Set(0, 0, 0);
        }
        if (m_can_edit)
        {
            bool dummy;
            PivotTransform pt;
            pt.local_position = pivot;
            pt.xform = xform;
            pt.world_transform = cache.GetLocalToWorldTransform(prim);
            pt.local_transform = cache.GetLocalTransformation(prim, &dummy);
            pt.inv_world_transform = pt.world_transform.GetInverse();
            m_pivot_transforms.emplace_back(pt);
        }
    }

    if (m_pivot_transforms.empty())
        return;

    const auto world_transform = cache.GetLocalToWorldTransform(m_pivot_transforms.front().xform.GetPrim());
    m_start_pivot_info = std::make_unique<PivotInfo>();
    m_start_pivot_info->position = world_transform.Transform(m_pivot_transforms.front().local_position);
    m_start_pivot_info->orientation = world_transform.GetOrthonormalized().ExtractRotation();
}

bool ViewportChangePivotCommand::get_pivot_info(PivotInfo& pivot_info) const
{
    if (!m_start_pivot_info)
        return false;
    pivot_info = *m_start_pivot_info;
    return true;
}

void ViewportChangePivotCommand::apply_delta(const GfVec3d& delta_pos, const GfRotation& delta_rotation /*= GfRotation()*/)
{
    auto session = Application::instance().get_session();
    auto stage = session->get_current_stage();
    if (!stage || (GfIsClose(delta_pos, GfVec3d(0), 0.000001) && GfIsClose(delta_rotation.GetAngle(), 0.0, 0.000001)))
        return;

    m_delta_move = delta_pos;
    m_delta_rot = delta_rotation;
    const auto time = Application::instance().get_current_time();
    std::vector<std::function<void()>> deferred_edits;
    {
        SdfChangeBlock change_block;
        for (const auto& pivot_transform : m_pivot_transforms)
        {
            auto pivot_time = UsdTimeCode::Default();
            bool dummy;
            for (const auto& op : pivot_transform.xform.GetOrderedXformOps(&dummy))
            {
                if (op.GetOpType() == UsdGeomXformOp::Type::TypeTranslate && op.HasSuffix(TfToken("pivot")))
                {
                    pivot_time = manipulator_utils::get_non_varying_time(op.GetAttr());
                    break;
                }
            }
            UsdGeomXformCommonAPI xform_api(pivot_transform.xform);
            if (xform_api)
            {
                const GfVec3d new_pivot = pivot_transform.inv_world_transform.Transform(
                    pivot_transform.world_transform.Transform(pivot_transform.local_position) + delta_pos);
                xform_api.SetPivot(GfVec3f(new_pivot), pivot_time);
                GfTransform transform;
                transform.SetPivotPosition(new_pivot);
                transform.SetMatrix(pivot_transform.local_transform);

                manipulator_utils::decompose_to_common_api(pivot_transform.xform, transform);
            }
            else
            {
                GfMatrix4d local;
                pivot_transform.xform.GetLocalTransformation(&local, &dummy);
                GfTransform transform(local);
                transform.SetTranslation(transform.GetTranslation() - delta_pos);

                if (GfIsClose(transform.GetPivotOrientation().GetAngle(), 0, 0.00001))
                {
                    pivot_transform.xform.ClearXformOpOrder();
                    transform.SetPivotPosition(delta_pos);
                    deferred_edits.emplace_back(
                        [transform, xform = pivot_transform.xform] { manipulator_utils::decompose_to_common_api(xform, transform); });
                }
                else
                {
                    OPENDCC_ERROR("Failed to change pivot on prim '{}': failed to decompose to common API.",
                                  pivot_transform.xform.GetPrim().GetPrimPath().GetText());
                }
            }
        }
    }
    if (!deferred_edits.empty())
    {
        SdfChangeBlock change_block;
        for (const auto& edit : deferred_edits)
            edit();
    }
}

void ViewportChangePivotCommand::undo()
{
    m_inverse->invert();
}

void ViewportChangePivotCommand::redo()
{
    m_inverse->invert();
}

void ViewportChangePivotCommand::start_block()
{
    m_change_block = std::make_unique<commands::UsdEditsBlock>();
}

void ViewportChangePivotCommand::end_block()
{
    m_inverse = m_change_block->take_edits();
    m_change_block = nullptr;
}

bool ViewportChangePivotCommand::is_recording() const
{
    return m_change_block != nullptr;
}

bool ViewportChangePivotCommand::can_edit() const
{
    return m_can_edit;
}

CommandArgs ViewportChangePivotCommand::make_args() const
{
    CommandArgs result;
    result.arg(m_delta_move);
    result.arg(m_delta_rot);
    if (m_selection != Application::instance().get_selection())
        result.kwarg("objects", m_selection);
    return result;
}

CommandResult ViewportChangePivotCommand::execute(const CommandArgs& args)
{
    m_delta_move = *args.get_arg<GfVec3d>(0);
    m_delta_rot = *args.get_arg<GfRotation>(1);
    if (auto obj_arg = args.get_kwarg<SelectionList>("objects"))
        m_selection = *obj_arg;
    else
        m_selection = Application::instance().get_selection();

    set_initial_state(m_selection);
    start_block();
    apply_delta(m_delta_move, m_delta_rot);
    end_block();
    return CommandResult(CommandResult::Status::SUCCESS);
}

OPENDCC_NAMESPACE_CLOSE
