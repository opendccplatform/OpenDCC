# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import unittest

from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf, Usd
import opendcc.core as core


class TestApplicationUiCallbacks(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.assertTrue(self.app.is_ui_available())

    def listen(self, event, callback):
        handle = self.app.register_event_callback(event, callback)
        self.addCleanup(self.app.unregister_event_callback, event, handle)

    def wait_for_events(self):
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(100, loop.quit)
        loop.exec_()

    def test_viewport_activation_notifies_with_current_view(self):
        viewport = core.ViewportWidget()
        self.addCleanup(viewport.deleteLater)
        self.addCleanup(viewport.close)
        views = []
        self.listen(
            core.Application.EventType.ACTIVE_VIEW_CHANGED,
            lambda: views.append(self.app.get_active_view()),
        )
        viewport.show()
        viewport.activateWindow()
        self.wait_for_events()
        self.assertIn(viewport.get_viewport_view(), views)
        self.assertIs(self.app.get_active_view(), viewport.get_viewport_view())

    def test_viewport_tool_callbacks_observe_new_tool(self):
        from opendcc.actions.tool_context_actions import set_tool_context

        self.app.set_current_viewport_tool(None)
        self.addCleanup(self.app.set_current_viewport_tool, None)
        tools = []
        self.listen(
            "current_viewport_tool_changed",
            lambda: tools.append(
                self.app.get_current_viewport_tool().get_name()
                if self.app.get_current_viewport_tool()
                else None
            ),
        )
        set_tool_context("select_tool")
        self.app.set_current_viewport_tool(None)
        self.assertEqual(tools, ["select_tool", None])

    def test_escape_shortcut_notifies(self):
        window = self.app.get_main_window()
        window.activateWindow()
        self.wait_for_events()
        notifications = []
        self.listen("ui_escape_key_action", lambda: notifications.append(True))
        # Deliver a key through Qt's shortcut handling, without depending on keyboard focus outside the app.
        for event_type in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            event = QtGui.QKeyEvent(event_type, QtCore.Qt.Key_Escape, QtCore.Qt.NoModifier)
            QtWidgets.QApplication.sendEvent(window, event)
        self.assertEqual(notifications, [True])

    def test_edit_target_dirtiness_notifies_after_undoable_edit(self):
        self.app.set_current_viewport_tool(None)
        session = self.app.get_session()
        stage = Usd.Stage.CreateInMemory()
        session.set_current_stage(stage)
        self.addCleanup(session.close_stage, stage)
        layer = stage.GetRootLayer()
        dirty = []
        self.listen("edit_target_dirtiness_changed", lambda: dirty.append(layer.dirty))
        self.addCleanup(self.app.get_undo_stack().clear)
        # The UI installs the delegate that tracks layer dirtiness.
        with core.UsdEditsUndoBlock():
            stage.DefinePrim(Sdf.Path("/World"))
        self.assertEqual(dirty, [True])
