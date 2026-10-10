# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import opendcc.core as core

assert not core.Application.instance().is_ui_available()
print("script completed", flush=True)
