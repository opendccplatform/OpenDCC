# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore
import opendcc.core as dcc_core


def subscribe_to_no_stage_signal(widgets):
    no_stage_widgets = widgets.copy()

    def update_no_stage(state):
        for widget in no_stage_widgets:
            widget.setEnabled(not state)

    session_ui = SessionUI.instance()
    session_ui.no_stage_signal.connect(update_no_stage)
    update_no_stage(session_ui.no_stage)


class SessionUI(QtCore.QObject):
    """
    A singleton class for handling sessions
    """

    _instance = None

    no_stage_signal = QtCore.Signal(bool)
    no_stage = None

    def __init__(self, parent=None):
        super(SessionUI, self).__init__(parent=parent)
        dcc_core.Application.instance().register_event_callback(
            dcc_core.Application.EventType.SESSION_STAGE_LIST_CHANGED, self.current_stage_changed
        )
        self.no_stage = self.get_no_stage()

    def get_no_stage(self):
        session = dcc_core.Application.instance().get_session()
        stage_list = session.get_stage_list()
        return len(stage_list) == 0

    def current_stage_changed(self):
        no_stage = self.get_no_stage()
        if no_stage != self.no_stage or self.no_stage is None:
            self.no_stage = no_stage
            self.no_stage_signal.emit(no_stage)

    @staticmethod
    def instance():
        if not SessionUI._instance:
            SessionUI._instance = SessionUI()
        return SessionUI._instance
