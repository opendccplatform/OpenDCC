// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"

#include <string>
#include <vector>

#include "opendcc/app/ui/wrap_shiboken.h"
#include "opendcc/app/core/wrap_application.h"
#include "opendcc/app/core/wrap_session.h"
#include "opendcc/app/core/wrap_usd_clipboard.h"
#include "opendcc/app/ui/wrap_panel.h"
#include "opendcc/app/viewport/wrap_prim_refine_manager.h"
#include "opendcc/app/viewport/wrap_def_cam_settings.h"
#include "opendcc/app/core/wrap_settings.h"
#include "opendcc/app/core/wrap_logging.h"
#include "opendcc/app/core/wrap_undo.h"
#include "opendcc/app/core/wrap_selection_list.h"
#include "opendcc/app/viewport/wrap_tool_settings_registry.h"
#include "opendcc/app/viewport/wrap_tool_context.h"
#include "opendcc/app/ui/wrap_node_icon_registry.h"
#include "opendcc/app/viewport/wrap_viewport_view.h"
#include "opendcc/app/viewport/wrap_viewport_context_menu.h"
#include "opendcc/app/viewport/wrap_dnd_callback.h"
#include "opendcc/app/ui/wrap_shader_node_registry.h"
#include "opendcc/base/pybind_bridge/pybind11.h"
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.app");

PYBIND11_MODULE(_core, m)
{
    py_interp::bind::wrap_application(m);
    py_interp::bind::wrap_session(m);
    py_interp::bind::wrap_usd_clipboard(m);
    py_interp::bind::wrap_panel(m);
    py_interp::bind::wrap_prim_refine_manager(m);
    py_interp::bind::wrap_def_cam_settings(m);
    py_interp::bind::wrap_settings(m);
    py_interp::bind::wrap_logging(m);
    py_interp::bind::wrap_undo(m);
    py_interp::bind::wrap_selection_list(m);
    py_interp::bind::wrap_tool_settings_registry(m);
    py_interp::bind::wrap_tool_context(m);
    py_interp::bind::wrap_node_icon_registry(m);
    py_interp::bind::wrap_viewport_view(m);
    py_interp::bind::wrap_viewport_context_menu(m);
    py_interp::bind::wrap_dnd_callback(m);
    py_interp::bind::wrap_shader_node_registry(m);
    py_interp::bind::wrap_shiboken();
}

OPENDCC_NAMESPACE_CLOSE
