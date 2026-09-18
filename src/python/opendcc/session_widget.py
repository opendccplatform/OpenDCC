# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui
import opendcc.core as dcc_core
from pxr import Usd

# not supported by Qt.py
from Qt.QtCore import QSignalBlocker

from opendcc.i18n import i18n


class SessionToolbarWidget(QtWidgets.QWidget):
    stage_list_changed = QtCore.Signal()
    stage_list_selection_changed = QtCore.Signal()

    def __init__(self, parent=None):
        super(SessionToolbarWidget, self).__init__(parent=parent)
        self.setLayout(QtWidgets.QHBoxLayout())
        self.stage_selection = QtWidgets.QComboBox()
        self.stage_selection.setSizeAdjustPolicy(QtWidgets.QComboBox.AdjustToContents)
        # self.layout().addWidget(QtWidgets.QLabel("Stage:"))
        self.layout().addWidget(self.stage_selection)
        self.layout().setContentsMargins(0, 0, 0, 0)
        self.layout().setSpacing(0)
        dcc_core.Application.instance().register_event_callback(
            dcc_core.Application.EventType.SESSION_STAGE_LIST_CHANGED,
            lambda: self.stage_list_changed.emit(),
        )
        dcc_core.Application.instance().register_event_callback(
            dcc_core.Application.EventType.CURRENT_STAGE_CHANGED,
            lambda: self.stage_list_selection_changed.emit(),
        )

        self.stage_list_changed.connect(self.__update_stage_list)
        self.stage_list_selection_changed.connect(self.__update_stage_list_selection)
        self.stage_selection.currentIndexChanged.connect(self.__set_stage_list_selection)
        self.setToolTip(i18n("session_toolbar_widget", "Current Stage"))
        # self.setMinimumWidth(150)

        self.__update_stage_list()

    def __update_stage_list(self):
        lk = QSignalBlocker(self)
        lk_sel = QSignalBlocker(self.stage_selection)
        session = dcc_core.Application.instance().get_session()
        self.stage_selection.clear()

        stage_list = session.get_stage_list()
        if len(stage_list) == 0:
            self.stage_selection.setDisabled(True)
            self.stage_selection.addItem(
                QtGui.QIcon(":/icons/svg/usd_small_disabled"),
                i18n("session_toolbar_widget", "No Stage"),
            )
        else:
            self.stage_selection.setDisabled(False)
            for stage in stage_list:
                stage_id = session.get_stage_id(stage)
                root_layer = stage.GetRootLayer()
                display_layer_name = root_layer.GetDisplayName()
                self.stage_selection.addItem(
                    QtGui.QIcon(":/icons/svg/usd_small"), display_layer_name, stage_id.ToString()
                )
            self.__update_stage_list_selection()

    def __update_stage_list_selection(self):
        lk = QSignalBlocker(self)
        lk_sel = QSignalBlocker(self.stage_selection)
        session = dcc_core.Application.instance().get_session()
        selection_idx = self.stage_selection.findData(session.get_current_stage_id().ToString())
        if selection_idx != -1:
            self.stage_selection.setCurrentIndex(selection_idx)

    def __set_stage_list_selection(self, selection_idx):
        lk = QSignalBlocker(self)
        lk_sel = QSignalBlocker(self.stage_selection)
        session = dcc_core.Application.instance().get_session()

        if selection_idx != -1:
            item_data = self.stage_selection.itemData(selection_idx)
            stage_cache = session.get_stage_cache()

            id = Usd.StageCache.Id.FromString(item_data)
            if id.IsValid() and stage_cache.Contains(id):
                session.set_current_stage(id)
