# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
import opendcc.core as dcc_core
from opendcc import cmds
from .custom_widget_action import CustomAction

from opendcc.i18n import i18n


class GroupAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("edit_menu.group_action", "Group"), parent=parent)
        self.custom_triggered.connect(self.do)

        shortcut = QtGui.QKeySequence(QtCore.Qt.CTRL | QtCore.Qt.Key_G)
        self.setShortcut(shortcut, self.do)

    def do(self):
        cmds.group_prim()
