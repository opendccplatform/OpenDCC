# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import opendcc.core as core


core.ui_load_notifications = []
app = core.Application.instance()
app.register_event_callback(
    core.Application.EventType.AFTER_UI_LOAD,
    lambda: core.ui_load_notifications.append(app.is_ui_available()),
)
app.register_event_callback(
    "before_app_quit", lambda: print("shutdown callback completed", flush=True)
)
