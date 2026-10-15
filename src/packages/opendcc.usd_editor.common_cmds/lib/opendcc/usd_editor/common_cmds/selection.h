/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/common_cmds/api.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/app/core/selection_list.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief The SelectPrimCommand class allows to modify selection list.
 * The selection list is formed by passing positional arguments with one of the flags.
 *
 * The SelectPrimCommand utilizes a positional argument of the following types:
 *    SelectionList;
 *    UsdPrim;
 *    SdfPath;
 *    UsdPrimVector;
 *    SdfPathVector.
 *
 * The flags are represented by the bool values:
 *    remove - Remove selection from the selection list;
 *    add - Add selection to current selection list;
 *    replace - Replace current selection list with selection;
 *    clear - Clear current selection list.
 *
 *    @note By default, this command uses ``replace`` flag.
 *
 */
class OPENDCC_USD_EDITOR_COMMON_CMDS_API SelectPrimCommand : public base::commands::UndoCommand
{
public:
    SelectPrimCommand() = default;

    virtual void redo() override;

    virtual void undo() override;

    virtual bool merge_with(base::commands::UndoCommand* command) override;

    bool operator==(const SelectPrimCommand& other) const;

    bool operator!=(const SelectPrimCommand& other) const { return !(*this == other); }

    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

    static std::string cmd_name;
    static base::commands::CommandSyntax cmd_syntax();
    static std::shared_ptr<base::commands::Command> create_cmd();

private:
    SelectionList m_new_selection;
    SelectionList m_old_selection;
};

OPENDCC_NAMESPACE_CLOSE
