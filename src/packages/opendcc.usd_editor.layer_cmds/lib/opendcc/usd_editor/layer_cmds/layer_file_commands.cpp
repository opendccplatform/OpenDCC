// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/layer_cmds/layer_file_commands.h"
#include "opendcc/usd_editor/layer_cmds/sublayer_utils.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/block.h"
#include "opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h"
#include <set>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::Command;
using base::commands::CommandArgs;
using base::commands::CommandResult;
using base::commands::CommandSyntax;

namespace
{
    SdfLayerRefPtr snapshot(const SdfLayerRefPtr& layer)
    {
        auto result = SdfLayer::CreateAnonymous("layer_snapshot.usda");
        result->TransferContent(layer);
        return result;
    }
}

std::string commands::SaveLayerCommand::cmd_name = "save_layer";
CommandSyntax commands::SaveLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("layer_identifier", "Layer to save")
        .kwarg<std::string>("path", "Export path; omit to save in place")
        .result<std::string>("Saved layer identifier")
        .description("Writes a layer to disk. File writes are not undoable.");
}
std::shared_ptr<Command> commands::SaveLayerCommand::create_cmd()
{
    return std::make_shared<SaveLayerCommand>();
}
CommandResult commands::SaveLayerCommand::execute(const CommandArgs& args)
{
    const auto identifier = args.get_arg<std::string>(0);
    auto layer = identifier ? SdfLayer::Find(identifier->get_value()) : SdfLayerHandle();
    if (!layer)
        return CommandResult(CommandResult::Status::INVALID_ARG);
    if (const auto path = args.get_kwarg<std::string>("path"))
    {
        if (path->get_value().empty())
            return CommandResult(CommandResult::Status::INVALID_ARG);
        SdfLayerRefPtr target = SdfLayer::Find(path->get_value());
        if (target && !target->PermissionToSave())
            return CommandResult(CommandResult::Status::INVALID_ARG);
        if (target == layer)
        {
            if (!layer->PermissionToSave() || !layer->Save())
                return CommandResult(CommandResult::Status::FAIL);
        }
        else if (!layer->Export(path->get_value()))
            return CommandResult(CommandResult::Status::FAIL);
        target = SdfLayer::FindOrOpen(path->get_value());
        if (!target)
            return CommandResult(CommandResult::Status::FAIL);
        return CommandResult(CommandResult::Status::SUCCESS, target->GetIdentifier());
    }
    if (layer->IsAnonymous() || !layer->PermissionToSave() || !layer->Save())
        return CommandResult(CommandResult::Status::INVALID_ARG);
    return CommandResult(CommandResult::Status::SUCCESS, layer->GetIdentifier());
}

std::string commands::RetargetLayerCommand::cmd_name = "retarget_layer";
CommandSyntax commands::RetargetLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("source_identifier", "Layer to replace")
        .arg<std::string>("target_identifier", "Saved replacement layer")
        .description("Copies source contents into the target and replaces its sublayer occurrences in the current stage.");
}
std::shared_ptr<Command> commands::RetargetLayerCommand::create_cmd()
{
    return std::make_shared<RetargetLayerCommand>();
}
CommandResult commands::RetargetLayerCommand::execute(const CommandArgs& args)
{
    const auto source = args.get_arg<std::string>(0);
    const auto target = args.get_arg<std::string>(1);
    auto stage = Application::instance().get_session()->get_current_stage();
    m_source = source ? SdfLayer::Find(source->get_value()) : SdfLayerHandle();
    m_target = target ? SdfLayer::FindOrOpen(target->get_value()) : SdfLayerRefPtr();
    if (!stage || !m_source || !m_target || m_source == m_target || !m_target->PermissionToEdit())
        return CommandResult(CommandResult::Status::INVALID_ARG);
    std::vector<std::pair<SdfLayerRefPtr, std::vector<std::string>>> parents;
    for (const auto& layer : stage->GetUsedLayers())
    {
        std::vector<std::string> paths;
        bool changed = false;
        for (const auto& path : layer->GetSubLayerPaths())
        {
            const bool replace = SdfLayer::FindRelativeToLayer(layer, path) == m_source;
            paths.push_back(replace ? m_target->GetIdentifier() : path);
            changed = changed || replace;
        }
        if (!changed)
            continue;
        if (layer == m_target || !layer->PermissionToEdit())
            return CommandResult(CommandResult::Status::INVALID_ARG);
        std::set<std::string> resolved;
        for (const auto& path : paths)
        {
            auto child = SdfLayer::FindRelativeToLayer(layer, path);
            if (!resolved.insert(child ? child->GetIdentifier() : path).second)
                return CommandResult(CommandResult::Status::INVALID_ARG);
        }
        parents.emplace_back(layer, std::move(paths));
    }
    if (parents.empty())
        return CommandResult(CommandResult::Status::INVALID_ARG);
    m_stage = stage;
    m_old_edit_target = stage->GetEditTarget();
    m_retarget_edit_target = m_old_edit_target.GetLayer() == m_source;
    m_retained_layers = commands::utils::retain_layer_tree(m_source);
    auto target_tree = commands::utils::retain_layer_tree(m_target);
    m_retained_layers.insert(m_retained_layers.end(), target_tree.begin(), target_tree.end());
    m_target_was_dirty = m_target->IsDirty();
    m_before = snapshot(m_target);
    m_after = snapshot(m_source);
    {
        UsdEditsBlock edits;
        m_target->TransferContent(m_after);
    }
    {
        UsdEditsBlock edits;
        for (const auto& parent : parents)
        {
            const auto old_paths = parent.first->GetSubLayerPaths();
            m_parents.push_back(parent.first);
            m_paths.emplace_back(old_paths.begin(), old_paths.end());
            m_offsets.push_back(parent.first->GetSubLayerOffsets());
            parent.first->SetSubLayerPaths(parent.second);
            for (size_t i = 0; i < m_offsets.back().size(); ++i)
                parent.first->SetSubLayerOffset(m_offsets.back()[i], i);
        }
        m_inverse = edits.take_edits();
    }
    if (m_retarget_edit_target)
        m_stage->SetEditTarget(m_stage->GetEditTargetForLocalLayer(m_target));
    return CommandResult(CommandResult::Status::SUCCESS);
}
void commands::RetargetLayerCommand::invert_paths()
{
    if (m_inverse && m_inverse->size())
    {
        m_inverse->invert();
        return;
    }

    UsdEditsBlock edits;
    for (size_t i = 0; i < m_parents.size(); ++i)
        commands::utils::swap_sublayers(m_parents[i], m_paths[i], m_offsets[i]);
}
void commands::RetargetLayerCommand::undo()
{
    invert_paths();
    {
        UsdEditsBlock edits;
        m_target->TransferContent(m_before);
        if (auto proxy = TfDynamic_cast<LayerStateDelegateProxyRefPtr>(m_target->GetStateDelegate()))
            proxy->set_dirty(m_target_was_dirty);
    }
    if (m_retarget_edit_target && m_stage)
        m_stage->SetEditTarget(m_old_edit_target);
}
void commands::RetargetLayerCommand::redo()
{
    {
        UsdEditsBlock edits;
        m_target->TransferContent(m_after);
    }
    invert_paths();
    if (m_retarget_edit_target && m_stage)
        m_stage->SetEditTarget(m_stage->GetEditTargetForLocalLayer(m_target));
}

std::string commands::ReloadLayerCommand::cmd_name = "reload_layer";
CommandSyntax commands::ReloadLayerCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<std::string>("layer_identifier", "Layer to reload from disk")
        .description("Reloads layer contents with undoable snapshots; redo restores the same contents.");
}
std::shared_ptr<Command> commands::ReloadLayerCommand::create_cmd()
{
    return std::make_shared<ReloadLayerCommand>();
}
CommandResult commands::ReloadLayerCommand::execute(const CommandArgs& args)
{
    const auto identifier = args.get_arg<std::string>(0);
    m_layer = identifier ? SdfLayer::Find(identifier->get_value()) : SdfLayerHandle();
    if (!m_layer || m_layer->IsAnonymous() || !m_layer->PermissionToEdit() || !SdfLayer::OpenAsAnonymous(m_layer->GetRealPath()))
        return CommandResult(CommandResult::Status::INVALID_ARG);
    m_before = snapshot(m_layer);
    m_was_dirty = m_layer->IsDirty();
    m_retained_layers = commands::utils::retain_layer_tree(m_layer);
    UsdEditsBlock edits;
    if (!m_layer->Reload(true))
        return CommandResult(CommandResult::Status::FAIL);
    m_after = snapshot(m_layer);
    auto reloaded_layers = commands::utils::retain_layer_tree(m_layer);
    m_retained_layers.insert(m_retained_layers.end(), reloaded_layers.begin(), reloaded_layers.end());
    return CommandResult(CommandResult::Status::SUCCESS);
}
void commands::ReloadLayerCommand::restore(const SdfLayerRefPtr& contents, bool dirty)
{
    UsdEditsBlock edits;
    m_layer->TransferContent(contents);
    if (auto proxy = TfDynamic_cast<LayerStateDelegateProxyRefPtr>(m_layer->GetStateDelegate()))
        proxy->set_dirty(dirty);
}
void commands::ReloadLayerCommand::undo()
{
    restore(m_before, m_was_dirty);
}
void commands::ReloadLayerCommand::redo()
{
    restore(m_after, false);
}

OPENDCC_NAMESPACE_CLOSE
