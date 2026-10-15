/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"

#include <memory>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    class UndoInverse;
}

class UsdEditUndoCommand : public base::commands::UndoCommand
{
public:
    virtual ~UsdEditUndoCommand() = default;

    virtual void redo() override;
    virtual void undo() override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
    void set_state(std::shared_ptr<commands::UndoInverse> inverse);

private:
    std::shared_ptr<commands::UndoInverse> m_inverse;
};

OPENDCC_NAMESPACE_CLOSE
