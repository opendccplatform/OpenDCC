# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import opendcc.core as core

assert core.ui_load_notifications == [True], core.ui_load_notifications
print("UI load callback completed", flush=True)
