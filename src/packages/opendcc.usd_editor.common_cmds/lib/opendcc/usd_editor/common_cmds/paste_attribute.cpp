// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "paste_attribute.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/primRange.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandSyntax;
using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::AEPasteCommand::cmd_name = "paste_attr";

CommandSyntax commands::AEPasteCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SdfPath>("selection", "Path to the attribute to be copied.")
        .kwarg<UsdStageRefPtr>("stage", "Stage")
        .kwarg<bool>("paste_value")
        .kwarg<bool>("paste_metadata")
        .kwarg<bool>("paste_time_samples")
        .description("The paste_attribute_value command allows you to paste an attribute value from the clipboard.");
}

std::shared_ptr<Command> commands::AEPasteCommand::create_cmd()
{
    return std::make_shared<commands::AEPasteCommand>();
}

CommandResult commands::AEPasteCommand::execute(const CommandArgs& args)
{
    UsdEditsBlock change_block;
    SdfPath path;
    m_mode = 0;
    VtValue value;
    UsdStageRefPtr stage;

    if (auto path_arg = args.get_arg<SdfPath>(0))
    {
        path = *path_arg;
    }
    else
    {
        OPENDCC_WARN("Failed to paste attribute value: empty attribute path.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (auto mode_kwarg = args.get_kwarg<bool>("paste_value"))
    {
        if (*mode_kwarg)
        {
            m_mode = 0;
        }
    }

    if (auto mode_kwarg = args.get_kwarg<bool>("paste_metadata"))
    {
        if (*mode_kwarg)
        {
            m_mode = 1;
        }
    }

    if (auto mode_kwarg = args.get_kwarg<bool>("paste_time_samples"))
    {
        if (*mode_kwarg)
        {
            m_mode = 2;
        }
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
        OPENDCC_WARN("Failed to paste attribute value: incorrect attribute path.");
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    auto clipboard_attr = Application::get_usd_clipboard().get_clipboard_attribute();
    if (!clipboard_attr.IsValid())
    {
        OPENDCC_WARN("Failed to paste.");
        return CommandResult(CommandResult::Status::FAIL);
    }

    switch (m_mode)
    {
    case 0:
        clipboard_attr.Get(&value);
        attr.Set(value);
        break;
    case 1:
        for (auto metadata : clipboard_attr.GetAllMetadata())
        {
            attr.SetMetadata(metadata.first, metadata.second);
        }
        break;
    case 2:
        std::vector<double> time_samples;
        clipboard_attr.GetTimeSamples(&time_samples);
        for (const auto time : time_samples)
        {
            VtValue value;
            clipboard_attr.Get(&value, time);
            attr.Set(value, time);
        }
        break;
    }

    m_inverse = change_block.take_edits();
    return CommandResult { CommandResult::Status::SUCCESS };
}

void commands::AEPasteCommand::undo()
{
    do_cmd();
}

void commands::AEPasteCommand::redo()
{
    do_cmd();
}

void commands::AEPasteCommand::do_cmd()
{
    if (m_inverse)
        m_inverse->invert();
}

OPENDCC_NAMESPACE_CLOSE
