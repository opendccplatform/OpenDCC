// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/common_cmds/selection.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/base/pybind_bridge/boost_python.h"
#include <pxr/base/tf/pyError.h>
#include "opendcc/app/core/selection_list.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

std::string SelectPrimCommand::cmd_name("select");

std::shared_ptr<Command> SelectPrimCommand::create_cmd()
{
    return std::make_shared<SelectPrimCommand>();
}

CommandSyntax SelectPrimCommand::cmd_syntax()
{
    return CommandSyntax()
        .arg<SelectionList, UsdPrim, SdfPath, std::vector<UsdPrim>, SdfPathVector>("selection", "Objects and subcomponents to select")
        .kwarg<bool>("clear", "Clear current selection list")
        .kwarg<bool>("replace", "Replace current selection list with 'selection'")
        .kwarg<bool>("add", "Add 'selection' to current selection list")
        .kwarg<bool>("remove", "Remove 'selection' from current selection list")
        .description(
            "Update application's selection list with 'selection' and one of available flags. By default, 'selection' replaces current selection list.");
}

void SelectPrimCommand::redo()
{
    Application::instance().set_selection(m_new_selection);
}

void SelectPrimCommand::undo()
{
    Application::instance().set_selection(m_old_selection);
}

bool SelectPrimCommand::merge_with(UndoCommand* command)
{
    auto other_selection = dynamic_cast<SelectPrimCommand*>(command);
    if (!other_selection)
        return false;

    return m_new_selection == other_selection->m_new_selection;
}

CommandResult SelectPrimCommand::execute(const CommandArgs& args)
{
    SelectionList new_selection;
    if (auto sel_list = args.get_arg<SelectionList>(0))
        new_selection = *sel_list;
    else if (auto prim = args.get_arg<UsdPrim>(0))
        new_selection.add_prims({ prim->get_value().GetPrimPath() });
    else if (auto path = args.get_arg<SdfPath>(0))
        new_selection.add_prims({ path->get_value() });
    else if (auto prim_vec = args.get_arg<std::vector<UsdPrim>>(0))
    {
        SdfPathVector path_vec(prim_vec->get_value().size());
        std::transform(prim_vec->get_value().begin(), prim_vec->get_value().end(), path_vec.begin(),
                       [](const UsdPrim& p) { return p.GetPrimPath(); });
        new_selection.add_prims(path_vec);
    }
    else if (auto prim_vec = args.get_arg<SdfPathVector>(0))
        new_selection.add_prims(*prim_vec);

    m_old_selection = Application::instance().get_selection();

    if (args.has_kwarg("clear") && *args.get_kwarg<bool>("clear"))
    {
        m_new_selection = SelectionList();
    }
    else if (args.has_kwarg("add") && *args.get_kwarg<bool>("add"))
    {
        m_new_selection = m_old_selection;
        m_new_selection.merge(new_selection);
    }
    else if (args.has_kwarg("remove") && *args.get_kwarg<bool>("remove"))
    {
        m_new_selection = m_old_selection;
        m_new_selection.difference(new_selection);
    }
    else
    {
        m_new_selection = new_selection;
    }

    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

bool SelectPrimCommand::operator==(const SelectPrimCommand& other) const
{
    return m_new_selection == other.m_new_selection && m_old_selection == other.m_old_selection;
}
OPENDCC_NAMESPACE_CLOSE
