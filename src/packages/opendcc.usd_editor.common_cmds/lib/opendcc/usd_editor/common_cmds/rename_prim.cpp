// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "pxr/base/tf/stringUtils.h"
#include "opendcc/usd_editor/common_cmds/rename_prim.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::Command;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

std::string commands::RenamePrimCommand::cmd_name("rename_prim");

std::shared_ptr<Command> commands::RenamePrimCommand::create_cmd()
{
    return std::make_shared<commands::RenamePrimCommand>();
}

CommandSyntax commands::RenamePrimCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<TfToken>("name")
        .kwarg<UsdPrim>("prim", "Prim to rename")
        .kwarg<UsdStageWeakPtr>("stage", "Target stage")
        .kwarg<SdfPath>("path", "Path to rename")
        .result<SdfPath>("New path");
}

CommandResult commands::RenamePrimCommand::execute(const CommandArgs& args)
{
    UsdPrim prim_to_rename;
    if (auto prim = args.get_kwarg<UsdPrim>("prim"))
    {
        prim_to_rename = prim->get_value();
        m_stage = prim->get_value().GetStage();
        m_old_path = prim->get_value().GetPrimPath();
    }
    else if (auto paths_arg = args.get_kwarg<SdfPath>("path"))
    {
        m_old_path = *paths_arg;
    }
    else
    {
        const auto current_selection = Application::instance().get_prim_selection();
        if (current_selection.empty())
        {
            OPENDCC_WARN("Failed to rename prim: no valid prim to rename was specified.");
            return CommandResult(CommandResult::Status::INVALID_ARG);
        }
        m_old_path = current_selection.front();
    }

    if (auto stage_kwarg = args.get_kwarg<UsdStageWeakPtr>("stage"))
        m_stage = *stage_kwarg;
    else
        m_stage = Application::instance().get_session()->get_current_stage();

    prim_to_rename = get_prim_to_rename();
    if (!prim_to_rename)
        return CommandResult(CommandResult::Status::INVALID_ARG);

    auto new_name = args.get_arg<TfToken>(0)->get_value();
    auto valid_name = TfToken(TfMakeValidIdentifier(new_name));
    if (valid_name.IsEmpty())
    {
        OPENDCC_WARN("Failed to rename prim at path '{}': new name is empty.", m_old_path.GetText());
        return CommandResult(CommandResult::Status::INVALID_ARG);
    }

    if (valid_name == prim_to_rename.GetName())
    {
        m_new_name = valid_name;
        return CommandResult(CommandResult::Status::SUCCESS, prim_to_rename.GetPath());
    }
    else
    {
        m_new_name = utils::get_new_name_for_prim(valid_name, prim_to_rename.GetParent());
    }

    redo();
    return CommandResult(CommandResult::Status::SUCCESS, m_old_path);
}

void commands::RenamePrimCommand::undo()
{
    do_cmd();
}

void commands::RenamePrimCommand::redo()
{
    do_cmd();
}

commands::RenameCommandNotifier& commands::RenamePrimCommand::get_notifier()
{
    static commands::RenameCommandNotifier notifier;
    return notifier;
}

UsdPrim commands::RenamePrimCommand::get_prim_to_rename() const
{
    if (!m_stage.IsExpired() && !m_stage.IsInvalid())
    {
        if (auto prim = m_stage->GetPrimAtPath(m_old_path))
        {
            return prim;
        }
        else
        {
            OPENDCC_WARN("Failed to rename prim at path '{}': prim doesn't exist.", m_old_path.GetText());
            return UsdPrim();
        }
    }
    OPENDCC_WARN("Failed to rename prim at path '{}': stage doesn't exist.", m_old_path.GetText());
    return UsdPrim();
}

bool commands::RenamePrimCommand::rename_prim() const
{
    SdfChangeBlock change_block;

    auto edit = SdfNamespaceEdit::Rename(m_old_path, m_new_name);
    SdfBatchNamespaceEdit batch({ edit });

    const auto& layer = m_stage->GetEditTarget().GetLayer();
    SdfNamespaceEditDetailVector details;
    if (layer->CanApply(batch, &details))
    {
        utils::rename_targets(m_stage, edit.currentPath, edit.newPath);
        if (!layer->Apply(batch))
        {
            OPENDCC_WARN("Failed to rename prims.");
            return false;
        }
    }
    else
    {
        for (const auto& detail : details)
            OPENDCC_WARN("Failed to rename prim at path {}: {}", m_old_path.GetText(), detail.reason.c_str());
        return false;
    }

    get_notifier().notify(edit.currentPath, edit.newPath);
    return true;
}

void commands::RenamePrimCommand::update_selection(const SdfPath& old_path, const SdfPath& new_path) const
{
    auto cur_selection = Application::instance().get_selection();
    bool dirty_selection = false;
    SelectionList new_selection;
    for (auto& sel_data : cur_selection)
    {
        auto path = sel_data.first;
        const auto& data = sel_data.second;

        if (path.HasPrefix(m_old_path))
        {
            dirty_selection = true;
            path = path.ReplacePrefix(old_path, new_path);
        }
        new_selection.set_selection_data(path, data);
    }

    if (dirty_selection)
        Application::instance().set_selection(new_selection);
}

void commands::RenamePrimCommand::do_cmd()
{
    if (!m_stage || !rename_prim())
        return;

    const auto new_path = m_old_path.GetParentPath().AppendChild(m_new_name);
    update_selection(m_old_path, new_path);
    m_new_name = m_old_path.GetNameToken();
    m_old_path = new_path;
}
OPENDCC_NAMESPACE_CLOSE
