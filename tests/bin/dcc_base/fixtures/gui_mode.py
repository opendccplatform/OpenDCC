# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import sys

from Qt import QtCore
import opendcc.core as core

assert core.Application.instance().is_ui_available()
assert QtCore.QThread.currentThread().loopLevel() > 0
print("GUI script executed", flush=True)
sys.exit(7)
