// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/entry_point.h"
#include "opendcc/base/logging/logger.h"

#include "opendcc/base/commands_api/core/command_registry.h"

#include "opendcc/usd_editor/common_cmds/assign_material.h"
#include "opendcc/usd_editor/common_cmds/center_pivot.h"
#include "opendcc/usd_editor/common_cmds/clear_edits.h"
#include "opendcc/usd_editor/common_cmds/copy_attribute.h"
#include "opendcc/usd_editor/common_cmds/copy_prims.h"
#include "opendcc/usd_editor/common_cmds/create_camera_from_view.h"
#include "opendcc/usd_editor/common_cmds/create_material.h"
#include "opendcc/usd_editor/common_cmds/create_mesh.h"
#include "opendcc/usd_editor/common_cmds/create_prim.h"
#include "opendcc/usd_editor/common_cmds/cut_prims.h"
#include "opendcc/usd_editor/common_cmds/duplicate_prim.h"
#include "opendcc/usd_editor/common_cmds/export_selection.h"
#include "opendcc/usd_editor/common_cmds/group_prim.h"
#include "opendcc/usd_editor/common_cmds/parent_prim.h"
#include "opendcc/usd_editor/common_cmds/paste_attribute.h"
#include "opendcc/usd_editor/common_cmds/paste_prims.h"
#include "opendcc/usd_editor/common_cmds/pick_walk.h"
#include "opendcc/usd_editor/common_cmds/remove_prims.h"
#include "opendcc/usd_editor/common_cmds/rename_prim.h"
#include "opendcc/usd_editor/common_cmds/selection.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(UsdEditorCommonCmdsEntryPoint);
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

void UsdEditorCommonCmdsEntryPoint::initialize(const Package& package)
{

    CommandRegistry::register_command(SelectPrimCommand::cmd_name, SelectPrimCommand::cmd_syntax(), &SelectPrimCommand::create_cmd);
    CommandRegistry::register_command(commands::AssignMaterialCommand::cmd_name, commands::AssignMaterialCommand::cmd_syntax(),
                                      &commands::AssignMaterialCommand::create_cmd);
    CommandRegistry::register_command(commands::RenamePrimCommand::cmd_name, commands::RenamePrimCommand::cmd_syntax(),
                                      &commands::RenamePrimCommand::create_cmd);
    CommandRegistry::register_command(commands::RemovePrimsCommand::cmd_name, commands::RemovePrimsCommand::cmd_syntax(),
                                      &commands::RemovePrimsCommand::create_cmd);
    CommandRegistry::register_command(commands::DuplicatePrimCommand::cmd_name, commands::DuplicatePrimCommand::cmd_syntax(),
                                      &commands::DuplicatePrimCommand::create_cmd);
    CommandRegistry::register_command(commands::ExportSelectionCommand::cmd_name, commands::ExportSelectionCommand::cmd_syntax(),
                                      &commands::ExportSelectionCommand::create_cmd);
    CommandRegistry::register_command(commands::GroupPrimCommand::cmd_name, commands::GroupPrimCommand::cmd_syntax(),
                                      &commands::GroupPrimCommand::create_cmd);
    CommandRegistry::register_command(commands::ParentPrimCommand::cmd_name, commands::ParentPrimCommand::cmd_syntax(),
                                      &commands::ParentPrimCommand::create_cmd);
    CommandRegistry::register_command(commands::PickWalkCommand::cmd_name, commands::PickWalkCommand::cmd_syntax(),
                                      &commands::PickWalkCommand::create_cmd);

    CommandRegistry::register_command(commands::CenterPivotCommand::cmd_name, commands::CenterPivotCommand::cmd_syntax(),
                                      &commands::CenterPivotCommand::create_cmd);

    CommandRegistry::register_command(commands::ClearEditsCommand::cmd_name, commands::ClearEditsCommand::cmd_syntax(),
                                      &commands::ClearEditsCommand::create_cmd);
    CommandRegistry::register_command(commands::AECopyCommand::cmd_name, commands::AECopyCommand::cmd_syntax(), &commands::AECopyCommand::create_cmd);
    CommandRegistry::register_command(commands::AEPasteCommand::cmd_name, commands::AEPasteCommand::cmd_syntax(),
                                      &commands::AEPasteCommand::create_cmd);

    CommandRegistry::register_command(commands::CreateCameraFromViewCommand::cmd_name, commands::CreateCameraFromViewCommand::cmd_syntax(),
                                      &commands::CreateCameraFromViewCommand::create_cmd);

    CommandRegistry::register_command(commands::CopyPrimsCommand::cmd_name, commands::CopyPrimsCommand::cmd_syntax(),
                                      &commands::CopyPrimsCommand::create_cmd);
    CommandRegistry::register_command(commands::CutPrimsCommand::cmd_name, commands::CutPrimsCommand::cmd_syntax(),
                                      &commands::CutPrimsCommand::create_cmd);
    CommandRegistry::register_command(commands::PastePrimsCommand::cmd_name, commands::PastePrimsCommand::cmd_syntax(),
                                      &commands::PastePrimsCommand::create_cmd);

    CommandRegistry::register_command(commands::CreatePrimCommand::cmd_name, commands::CreatePrimCommand::cmd_syntax(),
                                      &commands::CreatePrimCommand::create_cmd);
    CommandRegistry::register_command(commands::CreateMaterialCommand::cmd_name, commands::CreateMaterialCommand::cmd_syntax(),
                                      &commands::CreateMaterialCommand::create_cmd);
    CommandRegistry::register_command(commands::CreateMeshCommand::cmd_name, commands::CreateMeshCommand::cmd_syntax(),
                                      &commands::CreateMeshCommand::create_cmd);
}

void UsdEditorCommonCmdsEntryPoint::uninitialize(const Package& package)
{
    CommandRegistry::unregister_command(SelectPrimCommand::cmd_name);
    CommandRegistry::unregister_command(commands::RenamePrimCommand::cmd_name);
    CommandRegistry::unregister_command(commands::RemovePrimsCommand::cmd_name);
    CommandRegistry::unregister_command(commands::DuplicatePrimCommand::cmd_name);
    CommandRegistry::unregister_command(commands::ExportSelectionCommand::cmd_name);
    CommandRegistry::unregister_command(commands::GroupPrimCommand::cmd_name);
    CommandRegistry::unregister_command(commands::ParentPrimCommand::cmd_name);
    CommandRegistry::unregister_command(commands::PickWalkCommand::cmd_name);

    CommandRegistry::unregister_command(commands::AssignMaterialCommand::cmd_name);

    CommandRegistry::unregister_command(commands::CenterPivotCommand::cmd_name);
    CommandRegistry::unregister_command(commands::ClearEditsCommand::cmd_name);

    CommandRegistry::unregister_command(commands::CreateCameraFromViewCommand::cmd_name);

    CommandRegistry::unregister_command(commands::AECopyCommand::cmd_name);
    CommandRegistry::unregister_command(commands::AEPasteCommand::cmd_name);

    CommandRegistry::unregister_command(commands::CopyPrimsCommand::cmd_name);
    CommandRegistry::unregister_command(commands::CutPrimsCommand::cmd_name);
    CommandRegistry::unregister_command(commands::PastePrimsCommand::cmd_name);

    CommandRegistry::unregister_command(commands::CreatePrimCommand::cmd_name);
    CommandRegistry::unregister_command(commands::CreateMaterialCommand::cmd_name);
    CommandRegistry::unregister_command(commands::CreateMeshCommand::cmd_name);
}

OPENDCC_NAMESPACE_CLOSE
