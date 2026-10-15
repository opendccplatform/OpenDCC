// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "copy_attribute.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include <pxr/usd/sdf/copyUtils.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

CommandSyntax commands::AECopyCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPath>("selection", "Path to selected attribute")
        .kwarg<UsdStageRefPtr>("stage", "Stage")
        .description("The copy_attribute_value command allows you to copy an attribute to the clipboard.");
}

std::string commands::AECopyCommand::cmd_name = "copy_attr";

std::shared_ptr<Command> commands::AECopyCommand::create_cmd()
{
    return std::make_shared<commands::AECopyCommand>();
}

CommandResult commands::AECopyCommand::execute(const CommandArgs& args)
{
    UsdEditsBlock change_block;
    SdfPath path;
    UsdStageRefPtr stage;

    if (auto path_arg = args.get_arg<SdfPath>(0))
    {
        path = *path_arg;
    }
    else
    {
        OPENDCC_WARN("Failed to copy attribute value: empty attribute path.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageRefPtr>("stage"))
    {
        stage = *stage_kwarg;
    }
    else
    {
        stage = Application::instance().get_session()->get_current_stage();
    }

    auto attr = stage->GetAttributeAtPath(path);
    if (!attr.IsValid())
    {
        OPENDCC_WARN("Failed to copy attribute value: incorrect attribute path.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr clipboard_stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);
    SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));
    auto clipboard_attr = Application::get_usd_clipboard().get_new_clipboard_attribute(attr.GetTypeName());

    std::vector<double> time_samples;
    attr.GetTimeSamples(&time_samples);
    for (const auto time : time_samples)
    {
        VtValue value;
        attr.Get(&value, time);
        clipboard_attr.Set(value, time);
    }

    for (auto metadata : attr.GetAllMetadata())
    {
        clipboard_attr.SetMetadata(metadata.first, metadata.second);
    }

    VtValue value;
    attr.Get(&value);
    clipboard_attr.Set(value);
    Application::get_usd_clipboard().set_clipboard_attribute(clipboard_attr);

    m_inverse = change_block.take_edits();
    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::AECopyCommand::undo()
{
    do_cmd();
}

void commands::AECopyCommand::redo()
{
    do_cmd();
}

void commands::AECopyCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}

OPENDCC_NAMESPACE_CLOSE
