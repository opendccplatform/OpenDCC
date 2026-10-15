// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

// workaround
#include "opendcc/base/qt_python.h"
#include "opendcc/base/logging/logger.h"

#include "opendcc/app/viewport/viewport_isolate_selection_command.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/viewport/viewport_widget.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.selection");

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::UndoCommand;
using base::commands::CommandRegistry;
using base::commands::CommandSyntax;
using base::commands::CommandArgs;
using base::commands::CommandResult;

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<ViewportIsolateSelectionCommand, TfType::Bases<UndoCommand>>();
    CommandRegistry::register_command("isolate", CommandSyntax().kwarg<SdfPathVector>("paths"),
                                      [] { return std::make_shared<ViewportIsolateSelectionCommand>(); });
}

void ViewportIsolateSelectionCommand::set_ui_state(ViewportGLWidget* gl_widget,
                                                   const std::function<void(bool)>& ui_callback /*= std::function<void(bool)>()*/)
{
    m_gl_widget = gl_widget;
    m_ui_callback = ui_callback;

    SdfPath::RemoveDescendentPaths(&m_selected_paths);
    if (gl_widget)
        m_old_paths = gl_widget->get_populated_paths();
}

void ViewportIsolateSelectionCommand::undo()
{
    if (m_gl_widget)
        m_gl_widget->set_populated_paths(m_old_paths);

    if (m_ui_callback)
        m_ui_callback(true);
}

void ViewportIsolateSelectionCommand::redo()
{
    if (m_gl_widget)
        m_gl_widget->set_populated_paths(m_selected_paths);

    if (m_ui_callback)
        m_ui_callback(false);
}

CommandResult ViewportIsolateSelectionCommand::execute(const CommandArgs& args)
{
    if (auto paths_arg = args.get_kwarg<SdfPathVector>("paths"))
    {
        m_selected_paths = *paths_arg;
    }
    else
    {
        m_selected_paths = Application::instance().get_prim_selection();
    }

    if (auto view = ApplicationUI::instance().get_active_view())
    {
        m_gl_widget = view->get_gl_widget();
        if (!m_gl_widget)
        {
            OPENDCC_ERROR("Failed to isolate prims. There is no active viewport widget.");
            return CommandResult();
        }
        m_old_paths = m_gl_widget->get_populated_paths();
    }
    else
    {
        OPENDCC_ERROR("Failed to isolate prims. There is no active viewport widget.");
        return CommandResult();
    }

    SdfPath::RemoveDescendentPaths(&m_selected_paths);
    redo();
    return CommandResult(CommandResult::Status::SUCCESS);
}

OPENDCC_NAMESPACE_CLOSE