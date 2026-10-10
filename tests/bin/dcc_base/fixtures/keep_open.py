# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore
import opendcc.core as core

assert core.Application.instance().is_ui_available()
assert QtCore.QThread.currentThread().loopLevel() > 0


def finish():
    print("event loop continued", flush=True)
    QtCore.QCoreApplication.exit(11)


QtCore.QTimer.singleShot(100, finish)
