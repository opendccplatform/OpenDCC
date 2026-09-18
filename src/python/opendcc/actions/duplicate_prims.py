# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
import opendcc.core as dcc_core
from opendcc import cmds
from .custom_widget_action import CustomAction
from .options_widget import OptionsWidget, make_option

from opendcc.i18n import i18n


edit_target_mode = i18n("edit_menu.duplicate_action", "Edit Target")
on_each_layer_mode = i18n("edit_menu.duplicate_action", "On Each Layer")
collapsed_mode = i18n("edit_menu.duplicate_action", "Collapsed")


class DuplicateAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("edit_menu.duplicate_action", "Duplicate"), parent=parent)
        self.custom_triggered.connect(self.do)

        shortcut = QtGui.QKeySequence(QtCore.Qt.CTRL | QtCore.Qt.Key_D)
        self.setShortcut(shortcut, self.do)
        self.on_checkbox_clicked(self.show_options)

        options = []
        duplicate_mode = make_option(
            i18n("edit_menu.duplicate_action", "Mode"),
            "duplicate_command.mode",
            {"type": "option", "options": [edit_target_mode, on_each_layer_mode, collapsed_mode]},
            "Edit Target",
        )
        options.append(duplicate_mode)

        self.options_widget = OptionsWidget(
            i18n("edit_menu.duplicate_action", "Duplicate"),
            i18n("edit_menu.duplicate_action", "Duplicate"),
            self.do,
            options,
            self.parent(),
        )

    def do(self):
        settings = dcc_core.Application.instance().get_settings()
        duplicate_mode = settings.get_string("duplicate_command.mode", "Edit Target")

        if duplicate_mode == edit_target_mode:
            cmds.duplicate_prim()
        elif duplicate_mode == on_each_layer_mode:
            cmds.duplicate_prim(each_layer=True)
        else:
            cmds.duplicate_prim(collapsed=True)

    def show_options(self):
        self.options_widget.show()
