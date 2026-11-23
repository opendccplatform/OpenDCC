# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import time
import unittest
from Qt import QtCore, QtWidgets
from pxr import Sdf, Usd
import opendcc.core as core
from qt_input import mouse_click, mouse_move


def flush():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()
    application = QtWidgets.QApplication.instance()
    if not isinstance(application, QtWidgets.QApplication):
        return
    from opendcc.ui.scene_outliner import SceneOutliner

    deadline = time.monotonic() + 3
    while any(w.search_pending() for w in application.allWidgets() if isinstance(w, SceneOutliner)):
        if time.monotonic() >= deadline:
            raise AssertionError("Outliner search did not complete")
        QtCore.QCoreApplication.processEvents()
        QtCore.QThread.msleep(1)


def click(widget, pos, modifiers=QtCore.Qt.NoModifier):
    mouse_move(widget, pos, modifiers)
    mouse_click(widget, pos, modifiers)
    flush()


def wait_for(predicate, timeout=3):
    deadline = time.monotonic() + timeout
    while not predicate() and time.monotonic() < deadline:
        flush()
        QtCore.QThread.msleep(5)
    return predicate()


class StageTest(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.undo = self.app.get_undo_stack()
        self.previous = self.session.get_current_stage()
        source = Sdf.Layer.FindOrOpen(str(Path(__file__).parent / "fixtures/scene.usda"))
        layer = Sdf.Layer.CreateAnonymous()
        layer.TransferContent(source)
        self.stage = Usd.Stage.Open(layer)
        self.stage.GetPrimAtPath("/Root/Payload").GetPayloads().AddPayload(
            str(Path(__file__).parent / "fixtures/payload.usda"), Sdf.Path("/Asset")
        )
        self.session.set_current_stage(self.stage)
        self.undo.clear()
        self.addCleanup(self.restore_stage)

    def restore_stage(self):
        self.undo.clear()
        if self.previous:
            self.session.set_current_stage(self.previous)
        self.close_stage(self.stage)
        flush()

    def close_stage(self, stage):
        if stage:
            self.session.close_stage(stage)

    def checked(self, command, *args, **kwargs):
        result = command(*args, **kwargs)
        self.assertTrue(result.is_successful(), command.__name__)
        self.assertTrue(self.undo.can_undo())
        flush()

    def round_trip(self, command, *args, **kwargs):
        target = kwargs.get("stage", self.stage)
        before = target.GetRootLayer().ExportToString()
        rules_before = target.GetLoadRules()
        self.checked(command, *args, **kwargs)
        after = target.GetRootLayer().ExportToString()
        rules_after = target.GetLoadRules()
        self.undo.undo()
        flush()
        self.assertEqual(target.GetRootLayer().ExportToString(), before)
        self.assertEqual(target.GetLoadRules(), rules_before)
        self.undo.redo()
        flush()
        self.assertEqual(target.GetRootLayer().ExportToString(), after)
        self.assertEqual(target.GetLoadRules(), rules_after)


class OutlinerTest(StageTest):
    def setUp(self):
        super().setUp()
        self.assertIsNotNone(QtWidgets.QApplication.instance())
        from opendcc.usd_editor.usd_outliner import UsdOutliner

        self.panel = UsdOutliner()
        self.widget = self.panel.outliner
        self.addCleanup(self.destroy_widget)
        self.panel.resize(650, 550)
        self.panel.show()
        flush()
        self.view = self.widget.findChild(QtWidgets.QTreeView)
        self.proxy = self.view.model()
        self.model = self.proxy.sourceModel()
        self.name_column = self.column("Name")

    def focus_view(self):
        QtWidgets.QApplication.setActiveWindow(self.panel)
        self.view.setFocus()
        self.assertTrue(wait_for(lambda: self.view.hasFocus()))

    def destroy_widget(self):
        self.panel.close()
        self.panel.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        flush()

    def column(self, title):
        columns = self.model.columns()
        return next(
            i
            for i in range(self.model.columnCount())
            if self.model.headerData(i, QtCore.Qt.Horizontal) == title
            or (i < len(columns) and columns[i].title == title)
        )

    def find(self, name, parent=QtCore.QModelIndex()):
        def walk(parent):
            for row in range(self.proxy.rowCount(parent)):
                index = self.proxy.index(row, self.name_column, parent)
                if index.data() == name:
                    return index
                found = walk(index.sibling(index.row(), 0))
                if found.isValid():
                    return found
            return QtCore.QModelIndex()

        for _ in range(4):
            found = walk(parent)
            if found.isValid():
                return found
            # Category decisions for cold rows arrive after Qt releases its proxy state.
            flush()
        return QtCore.QModelIndex()

    def filter(self, text):
        field = self.widget.findChild(QtWidgets.QLineEdit)
        field.setText(text)
        self.assertTrue(
            wait_for(
                lambda: not self.widget.search_pending()
                and all(not t.isActive() for t in self.widget.findChildren(QtCore.QTimer))
            )
        )
        flush()
