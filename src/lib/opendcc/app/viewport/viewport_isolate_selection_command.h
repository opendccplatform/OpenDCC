/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include "opendcc/base/commands_api/core/command.h"

#include <pxr/usd/sdf/path.h>
#include <functional>

OPENDCC_NAMESPACE_OPEN

class ViewportGLWidget;

class OPENDCC_API ViewportIsolateSelectionCommand : public base::commands::UndoCommand
{
public:
    void set_ui_state(ViewportGLWidget* gl_widget, const std::function<void(bool)>& ui_callback = std::function<void(bool)>());
    virtual void undo() override;
    virtual void redo() override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    ViewportGLWidget* m_gl_widget = nullptr;
    PXR_NS::SdfPathVector m_selected_paths;
    PXR_NS::SdfPathVector m_old_paths;
    std::function<void(bool)> m_ui_callback;
};

OPENDCC_NAMESPACE_CLOSE
