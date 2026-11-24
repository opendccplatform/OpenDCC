# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf, Tf
from opendcc import cmds
import opendcc.core as core
from opendcc.usd_editor.usd_layers import (
    UsdLayersEntryPoint,
    _make_panel,
    open_with,
    utils,
    _on_context_menu,
)


class LayersTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.qt_app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        if sys.platform == "win32":
            fonts = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"
            for name in ("segoeui.ttf", "cour.ttf"):
                QtGui.QFontDatabase.addApplicationFont(str(fonts / name))
            cls.qt_app.setFont(QtGui.QFont("Segoe UI", 9))
        UsdLayersEntryPoint().initialize(None)

    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.root = Sdf.Layer.CreateAnonymous("layers_test.usda")
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.stack = self.app.get_undo_stack()
        self.stack.set_enabled(True)
        self.stack.clear()
        self.app.set_layer_selection(set())

    def tearDown(self):
        self.stack.clear()
        self.session.close_all()
        self.qt_app.processEvents()

    def run_command(self, name, *args, **kwargs):
        result = getattr(cmds, name)(*args, **kwargs)
        self.assertTrue(result.is_successful(), name)
        self.qt_app.processEvents()
        return result

    def test_save_load_and_reload(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(utils, "_add_recent_file"):
            path = str(Path(directory) / "saved.usda")
            child = Sdf.Layer.CreateAnonymous("save_test.usda")
            self.run_command("add_sublayer", self.root.identifier, child.identifier)
            self.run_command(
                "apply_layer_text", child.identifier, '#usda 1.0\n\ndef Xform "Saved" {}\n'
            )
            with patch.object(QtWidgets.QFileDialog, "getSaveFileName", return_value=(path, "")):
                self.assertTrue(utils.save_layer_as(child.identifier))
            saved = Sdf.Layer.FindOrOpen(path)
            self.assertIn(saved.identifier, self.root.subLayerPaths)
            self.assertTrue(saved.GetPrimAtPath("/Saved"))
            self.stack.undo()
            self.assertIn(child.identifier, self.root.subLayerPaths)
            self.stack.redo()
            self.run_command(
                "apply_layer_text", saved.identifier, '#usda 1.0\n\ndef Xform "Updated" {}\n'
            )
            self.assertTrue(utils.save_layer(saved.identifier))
            self.assertFalse(saved.dirty)
            saved.Reload()
            self.assertTrue(saved.GetPrimAtPath("/Updated"))
            self.run_command("remove_sublayer", self.root.identifier, saved.identifier)
            with patch.object(QtWidgets.QFileDialog, "getOpenFileNames", return_value=([path], "")):
                self.assertTrue(utils.load_sublayer(self.root.identifier))
            self.assertIn(saved.identifier, self.root.subLayerPaths)
            self.stack.undo()
            self.assertNotIn(saved.identifier, self.root.subLayerPaths)

    def test_save_as_same_file_preserves_contents(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(utils, "_add_recent_file"):
            path = str(Path(directory) / "same.usda")
            layer = Sdf.Layer.CreateNew(path)
            layer.ImportFromString('#usda 1.0\n\ndef Xform "Keep" {}\n')
            layer.Save()
            before = layer.ExportToString()
            with patch.object(QtWidgets.QFileDialog, "getSaveFileName", return_value=(path, "")):
                self.assertTrue(utils.save_layer_as(layer.identifier))
            self.assertEqual(layer.ExportToString(), before)
            layer.Reload()
            self.assertEqual(layer.ExportToString(), before)

    def test_shared_sublayer_visible_under_both_parents(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        shared = Sdf.Layer.CreateAnonymous("shared.usda")
        self.root.subLayerPaths = [first.identifier, second.identifier]
        first.subLayerPaths = [shared.identifier]
        second.subLayerPaths = [shared.identifier]
        panel = _make_panel()
        try:
            model = panel.findChild(QtWidgets.QTreeView).model()
            root = model.index(1, 0)
            self.assertEqual(model.rowCount(model.index(0, 0, root)), 1)
            self.assertEqual(model.rowCount(model.index(1, 0, root)), 1)
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_relative_sublayer_uses_parent_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "scene").mkdir()
            correct = Sdf.Layer.CreateNew(str(folder / "scene" / "child.usda"))
            wrong = Sdf.Layer.CreateNew(str(folder / "child.usda"))
            root = Sdf.Layer.CreateNew(str(folder / "scene" / "root.usda"))
            root.subLayerPaths = ["child.usda"]
            self.session.open_stage(root)
            previous = os.getcwd()
            os.chdir(folder)
            panel = None
            try:
                panel = _make_panel()
                model = panel.findChild(QtWidgets.QTreeView).model()
                child = model.index(0, 2, model.index(1, 0))
                self.assertEqual(child.data(), correct.identifier)
            finally:
                os.chdir(previous)
                if panel is not None:
                    panel.close()
                    panel.deleteLater()
                    self.qt_app.processEvents()

    def test_sublayer_cycle_does_not_recurse(self):
        child = Sdf.Layer.CreateAnonymous("cycle.usda")
        self.root.subLayerPaths = [child.identifier]
        child.subLayerPaths = [self.root.identifier]
        panel = _make_panel()
        try:
            model = panel.findChild(QtWidgets.QTreeView).model()
            root = model.index(1, 0)
            self.assertEqual(model.rowCount(root), 1)
            self.assertEqual(model.rowCount(model.index(0, 0, root)), 0)
        finally:
            child.subLayerPaths = []
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_create_sublayer_selects_and_reveals_new_layer(self):
        parent = Sdf.Layer.CreateAnonymous("parent.usda")
        self.root.subLayerPaths = [parent.identifier]
        panel = _make_panel()
        self.addCleanup(panel.deleteLater)
        panel.show()
        panel._search_toggle.setChecked(True)
        panel.search.setText("parent")
        self.app.set_layer_selection({parent})
        create_button = next(
            button
            for button in panel.findChildren(QtWidgets.QPushButton)
            if button.toolTip() == "Create Sublayer"
        )
        create_button.click()
        self.qt_app.processEvents()

        child = Sdf.Layer.Find(parent.subLayerPaths[0])
        self.assertEqual(self.app.get_layer_selection(), {child})
        self.assertEqual(panel.search.text(), "")
        index = panel.view.selectionModel().selectedRows()[0]
        self.assertEqual(index, panel.view.currentIndex())
        self.assertTrue(panel.view.isExpanded(index.parent()))

    def test_context_add_sublayer_selects_child_of_clicked_layer(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        self.root.subLayerPaths = [first.identifier, second.identifier]
        panel = _make_panel()
        self.addCleanup(panel.deleteLater)
        self.app.set_layer_selection({first, second})
        menu = QtWidgets.QMenu()
        _on_context_menu(panel, menu, second.identifier, self.root.identifier)
        next(action for action in menu.actions() if action.text() == "Add Sublayer").trigger()
        self.qt_app.processEvents()

        self.assertEqual(list(first.subLayerPaths), [])
        child = Sdf.Layer.Find(second.subLayerPaths[0])
        self.assertEqual(self.app.get_layer_selection(), {child})

    def test_search_toggle_clears_filter_when_hidden(self):
        panel = _make_panel()
        self.addCleanup(panel.deleteLater)
        panel.show()
        self.qt_app.processEvents()
        self.assertFalse(panel.search.isVisible())

        panel._search_toggle.click()
        self.assertTrue(panel.search.isVisible())
        self.assertEqual(panel.focusWidget(), panel.search)
        panel.search.setText("missing")
        self.assertEqual(panel.view.model().rowCount(), 0)

        panel._search_toggle.click()
        self.assertFalse(panel.search.isVisible())
        self.assertEqual(panel.search.text(), "")
        self.assertEqual(panel.view.model().rowCount(), 2)

    def test_inline_layer_text_preview(self):
        child = Sdf.Layer.CreateAnonymous("preview.usda")
        child.ImportFromString('#usda 1.0\n\ndef Xform "Original" {}\n')
        self.root.subLayerPaths = [child.identifier]
        panel = _make_panel()
        self.addCleanup(panel.deleteLater)
        panel.resize(800, 300)
        panel.show()
        self.qt_app.processEvents()

        toggle = panel._preview_toggle
        self.assertFalse(toggle.isChecked())
        self.assertIsNone(panel._text_preview)
        self.app.set_layer_selection({child})
        toggle.click()
        text = panel._text_preview.textArea.code_editor
        self.assertTrue(text.isVisible())
        self.assertTrue(text.isReadOnly())
        self.assertEqual(text.toPlainText(), child.ExportToString())

        child.ImportFromString('#usda 1.0\n\ndef Xform "Updated" {}\n')
        self.qt_app.processEvents()
        self.assertIn('"Original"', text.toPlainText())
        refresh = next(
            button
            for button in panel.findChildren(QtWidgets.QPushButton)
            if button.toolTip() == "Refresh"
        )
        refresh.click()
        self.assertEqual(text.toPlainText(), child.ExportToString())

        self.app.set_layer_selection({self.root, child})
        self.assertEqual(text.toPlainText(), "")
        self.app.set_layer_selection({self.root})
        self.assertEqual(text.toPlainText(), self.root.ExportToString())

        panel._splitter.setSizes([350, 450])
        sizes = panel._splitter.sizes()
        toggle.click()
        self.assertFalse(text.isVisible())
        self.app.set_layer_selection({child})
        toggle.click()
        self.assertEqual(text.toPlainText(), child.ExportToString())
        self.assertEqual(panel._splitter.sizes(), sizes)

    def test_inline_layer_text_apply_and_discard(self):
        child = Sdf.Layer.CreateAnonymous("editable.usda")
        self.root.subLayerPaths = [child.identifier]
        panel = _make_panel()
        self.addCleanup(panel.deleteLater)
        panel.show()
        self.app.set_layer_selection({child})
        panel._preview_toggle.click()

        layer_text = panel._text_preview
        editor = layer_text.textArea.code_editor
        self.assertTrue(editor.isReadOnly())
        layer_text.editableCheck_action.trigger()
        self.assertFalse(editor.isReadOnly())
        editor.setPlainText('#usda 1.0\n\ndef Xform "Applied" {}\n')
        layer_text.save_action.trigger()
        self.assertTrue(child.GetPrimAtPath("/Applied"))
        self.stack.undo()
        self.assertFalse(child.GetPrimAtPath("/Applied"))
        self.stack.redo()
        self.assertTrue(child.GetPrimAtPath("/Applied"))

        draft = '#usda 1.0\n\ndef Xform "Draft" {}\n'
        editor.setPlainText(draft)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Cancel
        ):
            self.app.set_layer_selection({self.root})
            self.assertEqual(editor.toPlainText(), draft)
            panel._preview_toggle.click()
            self.assertTrue(panel._preview_toggle.isChecked())
            self.assertTrue(editor.isVisible())
            self.assertFalse(panel.close())
        self.assertFalse(child.GetPrimAtPath("/Draft"))

        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
        ):
            panel._preview_toggle.click()
        self.assertFalse(editor.isVisible())
        panel._preview_toggle.click()
        self.assertEqual(editor.toPlainText(), self.root.ExportToString())
        self.assertTrue(editor.isReadOnly())

    def test_drop_reorders_and_reparents_sublayers(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        child = Sdf.Layer.CreateAnonymous("child.usda")
        sibling = Sdf.Layer.CreateAnonymous("sibling.usda")
        self.root.subLayerPaths = [first.identifier, second.identifier]
        first.subLayerPaths = [child.identifier, sibling.identifier]
        panel = _make_panel()
        self.addCleanup(panel.deleteLater)
        model = panel.view.model()

        def indexes():
            root = model.index(1, 0)
            return root, model.index(0, 0, root), model.index(1, 0, root)

        root, source, target = indexes()
        self.assertFalse(model.flags(root) & QtCore.Qt.ItemIsDragEnabled)
        child_index = model.index(0, 0, source)
        mime = model.mimeData([child_index])
        action = QtCore.Qt.MoveAction
        self.assertFalse(model.canDropMimeData(mime, action, -1, -1, child_index))
        self.assertTrue(model.canDropMimeData(mime, action, 2, 0, source))
        self.assertTrue(model.dropMimeData(mime, action, 2, 0, source))
        self.qt_app.processEvents()
        self.assertEqual(list(first.subLayerPaths), [sibling.identifier, child.identifier])

        _, source, target = indexes()
        mime = model.mimeData([model.index(1, 0, source)])
        self.assertTrue(model.canDropMimeData(mime, action, -1, -1, target))
        self.assertTrue(model.dropMimeData(mime, action, -1, -1, target))
        self.qt_app.processEvents()
        self.assertEqual(list(first.subLayerPaths), [sibling.identifier])
        self.assertEqual(list(second.subLayerPaths), [child.identifier])
        self.stack.undo()
        self.assertEqual(list(first.subLayerPaths), [sibling.identifier, child.identifier])
        self.assertEqual(list(second.subLayerPaths), [])
        self.stack.redo()
        self.assertEqual(list(second.subLayerPaths), [child.identifier])

    def test_open_with_default_handlers_follow_package_lifetime(self):
        entry_point = UsdLayersEntryPoint()
        entry_point.uninitialize(None)
        self.addCleanup(entry_point.initialize, None)

        menu = QtWidgets.QMenu()
        _on_context_menu(None, menu, self.root.identifier, "")
        self.assertNotIn("Open With", [action.text() for action in menu.actions()])

    def test_open_with_adds_layer_to_session(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        menu = QtWidgets.QMenu()
        _on_context_menu(None, menu, child.identifier, self.root.identifier)
        submenu = next(action.menu() for action in menu.actions() if action.text() == "Open With")
        actions = {action.text(): action for action in submenu.actions()}
        self.assertFalse(actions["New OpenDCC Instance"].isEnabled())
        actions["Add to Session"].trigger()
        self.assertEqual(self.session.get_current_stage().GetRootLayer(), child)
        self.assertIn(self.stage, self.session.get_stage_list())
        self.assertEqual(list(self.root.subLayerPaths), [child.identifier])

    def test_open_with_launches_saved_file_in_new_process(self):
        with tempfile.TemporaryDirectory() as directory:
            layer = Sdf.Layer.CreateNew(str(Path(directory) / "file with spaces.usda"))
            menu = QtWidgets.QMenu()
            _on_context_menu(None, menu, layer.identifier, "")
            submenu = next(
                action.menu() for action in menu.actions() if action.text() == "Open With"
            )
            action = next(a for a in submenu.actions() if a.text() == "New OpenDCC Instance")
            self.assertTrue(action.isEnabled())
            with patch.object(QtCore.QProcess, "startDetached", return_value=(True, 42)) as start:
                action.trigger()
            start.assert_called_once_with(
                QtCore.QCoreApplication.applicationFilePath(), ["--", layer.realPath]
            )

    def test_open_with_custom_handler_can_be_filtered_replaced_and_removed(self):
        name = "test_editor"
        self.addCleanup(open_with.unregister_handler, name)
        received = []

        def actions_for_layer():
            menu = QtWidgets.QMenu()
            _on_context_menu(None, menu, self.root.identifier, "")
            submenu = next(a.menu() for a in menu.actions() if a.text() == "Open With")
            return menu, {a.text(): a for a in submenu.actions()}

        open_with.register_handler(name, "Editor", received.append, can_open=lambda layer: False)
        menu, actions = actions_for_layer()
        self.assertFalse(actions["Editor"].isEnabled())
        open_with.register_handler(name, "Replacement Editor", received.append)
        menu, actions = actions_for_layer()
        self.assertNotIn("Editor", actions)
        actions["Replacement Editor"].trigger()
        self.assertEqual(received, [self.root])
        open_with.unregister_handler(name)
        menu, actions = actions_for_layer()
        self.assertNotIn("Replacement Editor", actions)

    def test_copy_layer_resolved_path(self):
        menu = QtWidgets.QMenu()
        _on_context_menu(None, menu, self.root.identifier, "")
        action = next(a for a in menu.actions() if a.text() == "Copy Layer Resolved Path")
        self.assertFalse(action.isEnabled())

        with tempfile.TemporaryDirectory() as directory:
            layer = Sdf.Layer.CreateNew(str(Path(directory) / "file.usda"))
            menu.clear()
            _on_context_menu(None, menu, layer.identifier, "")
            action = next(a for a in menu.actions() if a.text() == "Copy Layer Resolved Path")
            self.assertTrue(action.isEnabled())
            with patch.object(QtWidgets.QApplication, "clipboard") as clipboard:
                clipboard.return_value.supportsSelection.return_value = False
                action.trigger()
                clipboard.return_value.setText.assert_called_once_with(
                    layer.realPath, QtGui.QClipboard.Clipboard
                )

    def test_context_menu_uses_clicked_parent(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        shared = Sdf.Layer.CreateAnonymous("shared.usda")
        sibling = Sdf.Layer.CreateAnonymous("sibling.usda")
        self.root.subLayerPaths = [first.identifier, second.identifier]
        first.subLayerPaths = [shared.identifier]
        second.subLayerPaths = [shared.identifier, sibling.identifier]
        panel = _make_panel()
        try:
            panel.resize(600, 400)
            panel.show()
            self.qt_app.processEvents()
            tree = panel.findChild(QtWidgets.QTreeView)
            tree.expandAll()
            model = tree.model()
            second_index = model.index(1, 0, model.index(1, 0))
            clicked = model.index(0, 0, second_index)
            received = []

            def record_menu(menu, identifier, parent_identifier):
                received.append((identifier, parent_identifier))
                QtCore.QTimer.singleShot(0, menu.close)

            panel.context_menu_requested.connect(record_menu)
            position = tree.visualRect(clicked).center()
            event = QtGui.QContextMenuEvent(
                QtGui.QContextMenuEvent.Mouse,
                position,
                tree.viewport().mapToGlobal(position),
            )
            self.qt_app.sendEvent(tree.viewport(), event)
            self.assertEqual(received, [(shared.identifier, second.identifier)])
            menu = QtWidgets.QMenu()
            _on_context_menu(panel, menu, shared.identifier, second.identifier)
            next(a for a in menu.actions() if a.text() == "Move Down").trigger()
            self.assertEqual(list(first.subLayerPaths), [shared.identifier])
            self.assertEqual(list(second.subLayerPaths), [sibling.identifier, shared.identifier])
            self.stack.undo()
            self.assertEqual(list(second.subLayerPaths), [shared.identifier, sibling.identifier])
            next(a for a in menu.actions() if a.text() == "Remove Layer").trigger()
            self.assertEqual(list(first.subLayerPaths), [shared.identifier])
            self.assertEqual(list(second.subLayerPaths), [sibling.identifier])
            self.stack.undo()
            self.assertEqual(list(second.subLayerPaths), [shared.identifier, sibling.identifier])
            reference_menu = QtWidgets.QMenu()
            _on_context_menu(panel, reference_menu, shared.identifier, "")
            self.assertNotIn("Remove Layer", [a.text() for a in reference_menu.actions()])
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_selection_and_view_state_survive_refresh(self):
        children = [Sdf.Layer.CreateAnonymous("child%d.usda" % i) for i in range(60)]
        self.root.subLayerPaths = [layer.identifier for layer in children]
        panel = _make_panel()
        panel.resize(600, 180)
        panel.show()
        try:
            tree = panel.findChild(QtWidgets.QTreeView)
            model = tree.model()
            self.app.set_layer_selection({children[2], children[3]})
            self.assertEqual(len(tree.selectionModel().selectedRows()), 2)
            self.app.set_layer_selection(set())
            self.assertEqual(tree.selectionModel().selectedRows(), [])
            root = model.index(1, 0)
            tree.setExpanded(root, True)
            tree.selectionModel().select(
                model.index(40, 0, root),
                QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows,
            )
            tree.setCurrentIndex(model.index(40, 0, root))
            self.assertEqual(
                self.app.get_layer_selection(), {children[40]}, "before event processing"
            )
            self.qt_app.processEvents()
            self.assertEqual(
                self.app.get_layer_selection(), {children[40]}, "after event processing"
            )
            tree.verticalScrollBar().setValue(20)
            scroll = tree.verticalScrollBar().value()
            name_width = tree.columnWidth(0) + 20
            tree.setColumnWidth(0, name_width)
            refresh = next(
                b for b in panel.findChildren(QtWidgets.QPushButton) if b.toolTip() == "Refresh"
            )
            refresh.click()
            self.assertEqual(self.app.get_layer_selection(), {children[40]}, "after refresh")
            self.qt_app.processEvents()
            self.assertEqual(self.app.get_layer_selection(), {children[40]})
            self.assertEqual(
                tree.selectionModel().selectedRows()[0].data(), children[40].GetDisplayName()
            )
            self.assertTrue(tree.isExpanded(model.index(1, 0)))
            self.assertEqual(tree.verticalScrollBar().value(), scroll)
            self.assertEqual(tree.columnWidth(0), name_width)
            tree.collapseAll()
            refresh.click()
            self.assertFalse(tree.isExpanded(model.index(1, 0)))
            other = Sdf.Layer.CreateAnonymous("other.usda")
            self.session.open_stage(other)
            self.assertEqual(self.app.get_layer_selection(), {other})
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_refresh_preserves_shared_occurrence(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        shared = Sdf.Layer.CreateAnonymous("shared.usda")
        self.root.subLayerPaths = [first.identifier, second.identifier]
        first.subLayerPaths = [shared.identifier]
        second.subLayerPaths = [shared.identifier]
        panel = _make_panel()
        try:
            tree = panel.findChild(QtWidgets.QTreeView)
            model = tree.model()
            second_index = model.index(1, 0, model.index(1, 0))
            tree.selectionModel().select(
                model.index(0, 0, second_index),
                QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows,
            )
            next(
                b for b in panel.findChildren(QtWidgets.QPushButton) if b.toolTip() == "Refresh"
            ).click()
            self.assertEqual(
                tree.selectionModel().selectedRows()[0].parent().data(), second.GetDisplayName()
            )
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_python_package_model_column_and_menu(self):
        from opendcc.usd_editor.usd_layers import _usd_layers
        from opendcc.usd_editor.usd_layers.panel import UsdLayersPanel

        class PackageModel(_usd_layers.UsdLayersModel):
            def columnCount(self, parent=QtCore.QModelIndex()):
                return int(_usd_layers.UsdLayersModel.ColumnCount) + 1

            def data(self, index, role=QtCore.Qt.DisplayRole):
                if (
                    index.column() == int(_usd_layers.UsdLayersModel.ColumnCount)
                    and role == QtCore.Qt.DisplayRole
                ):
                    return "Python package column"
                return super(PackageModel, self).data(index, role)

            def headerData(self, section, orientation, role=QtCore.Qt.DisplayRole):
                if (
                    section == int(_usd_layers.UsdLayersModel.ColumnCount)
                    and role == QtCore.Qt.DisplayRole
                ):
                    return "Package"
                return super(PackageModel, self).headerData(section, orientation, role)

        model = PackageModel()
        panel = UsdLayersPanel(model=model)
        try:
            panel.resize(1200, 240)
            column = int(_usd_layers.UsdLayersModel.ColumnCount)
            panel.show()
            self.qt_app.processEvents()
            index = panel.view.model().index(1, column)
            self.assertTrue(index.isValid())
            self.assertEqual(index.data(), "Python package column")
            self.assertEqual(panel.view.model().headerData(column, QtCore.Qt.Horizontal), "Package")
            self.assertFalse(panel.grab().isNull())
            actions = []

            def build_menu(menu, identifier, parent_identifier):
                action = menu.addAction("Python package action")
                action.triggered.connect(lambda: actions.append(identifier))
                QtCore.QTimer.singleShot(0, lambda: (action.trigger(), menu.close()))

            panel.context_menu_requested.connect(build_menu)
            position = panel.view.visualRect(panel.view.model().index(1, 0)).center()
            event = QtGui.QContextMenuEvent(
                QtGui.QContextMenuEvent.Mouse,
                position,
                panel.view.viewport().mapToGlobal(position),
            )
            self.qt_app.sendEvent(panel.view.viewport(), event)
            self.assertEqual(actions, [self.root.identifier])
            self.assertTrue(panel.view.dragEnabled())
            self.assertTrue(panel.view.acceptDrops())
            panel._search_toggle.setChecked(True)
            panel.search.setText("missing")
            self.assertEqual(panel.view.model().rowCount(), 0)
            panel.search.clear()
            self.assertEqual(panel.view.model().rowCount(), 2)
            panel.close()
            self.assertEqual(panel._callbacks, [])
            panel.show()
            self.assertEqual(len(panel._callbacks), 2)
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_delegate_rendering_and_shared_edit_target_highlights(self):
        from opendcc.usd_editor.usd_layers import _usd_layers

        roles = _usd_layers.UsdLayersModel
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        shared = Sdf.Layer.CreateAnonymous("shared.usda")
        first.subLayerPaths = [shared.identifier]
        second.subLayerPaths = [shared.identifier]
        self.root.subLayerPaths = [first.identifier, second.identifier]
        panel = _make_panel()
        panel.resize(800, 360)
        panel.show()
        try:
            self.qt_app.processEvents()
            tree = panel.view
            tree.expandAll()
            self.app.set_layer_selection(set())
            model = tree.model()
            root = model.index(1, 0)
            parents = [model.index(i, 0, root) for i in range(2)]
            before = [tree.viewport().grab(tree.visualRect(index)).toImage() for index in parents]
            self.stage.SetEditTarget(shared)
            self.qt_app.processEvents()
            for index, image in zip(parents, before):
                self.assertTrue(index.data(roles.LayerIsEditTargetParentRole))
                after = tree.viewport().grab(tree.visualRect(index)).toImage()
                self.assertFalse(after.isNull())
                self.assertNotEqual(image, after)
            shared.documentation = "updated"
            self.qt_app.processEvents()
            self.assertTrue(all(index.data(roles.LayerIsEditTargetParentRole) for index in parents))

            def render_cell(index):
                image = QtGui.QImage(140, 24, QtGui.QImage.Format_ARGB32)
                image.fill(QtCore.Qt.white)
                painter = QtGui.QPainter(image)
                option = QtWidgets.QStyleOptionViewItem()
                option.initFrom(tree)
                option.rect = image.rect()
                option.decorationSize = tree.iconSize()
                tree.itemDelegate().paint(painter, option, index)
                painter.end()
                return image

            child = model.index(0, 0, parents[0])
            self.assertIsNone(child.data(QtCore.Qt.DecorationRole))
            self.assertNotEqual(
                render_cell(root.sibling(root.row(), int(roles.DirtyColumn))),
                render_cell(root.sibling(root.row(), int(roles.VersionColumn))),
            )
            self.assertNotEqual(render_cell(child), render_cell(child.sibling(child.row(), 2)))
            target_before = render_cell(child.sibling(child.row(), int(roles.EditTargetColumn)))
            self.stage.SetEditTarget(self.root)
            self.qt_app.processEvents()
            self.assertNotEqual(
                target_before, render_cell(child.sibling(child.row(), int(roles.EditTargetColumn)))
            )
            mute_index = child.sibling(child.row(), int(roles.MutedColumn))
            mute_before = render_cell(mute_index)
            self.run_command("mute_layer", shared.identifier, mute=True)
            self.assertNotEqual(mute_before, render_cell(mute_index))
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_queued_notices_are_safe_after_panel_destruction(self):
        panel = _make_panel()
        panel.show()
        child = Sdf.Layer.CreateAnonymous("pending.usda")
        self.root.subLayerPaths = [child.identifier]
        panel.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.assertEqual(panel._callbacks, [])
        self.qt_app.processEvents()
        self.root.subLayerPaths = []
        self.qt_app.processEvents()

    def test_package_presentation_roles_override_default_delegate(self):
        from opendcc.usd_editor.usd_layers import UsdLayersModel, UsdLayersPanel

        class PackageModel(UsdLayersModel):
            def data(self, index, role=QtCore.Qt.DisplayRole):
                if role == QtCore.Qt.BackgroundRole:
                    return QtGui.QBrush(QtGui.QColor("#00aa00"))
                if role == QtCore.Qt.DecorationRole and index.column() == int(
                    UsdLayersModel.NameColumn
                ):
                    pixmap = QtGui.QPixmap(16, 16)
                    pixmap.fill(QtGui.QColor("#ff0000"))
                    return QtGui.QIcon(pixmap)
                return super(PackageModel, self).data(index, role)

        model = PackageModel()
        panel = UsdLayersPanel(model=model)
        try:
            tree = panel.view
            self.app.set_layer_selection(set())
            image = QtGui.QImage(160, 24, QtGui.QImage.Format_ARGB32)
            image.fill(QtCore.Qt.white)
            painter = QtGui.QPainter(image)
            option = QtWidgets.QStyleOptionViewItem()
            option.initFrom(tree)
            option.rect = image.rect()
            option.decorationSize = tree.iconSize()
            tree.itemDelegate().paint(painter, option, tree.model().index(1, 0))
            painter.end()
            self.assertEqual(image.pixelColor(150, 12), QtGui.QColor("#00aa00"))
            self.assertTrue(
                any(
                    image.pixelColor(x, y) == QtGui.QColor("#ff0000")
                    for x in range(24)
                    for y in range(24)
                )
            )
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_package_model_can_replace_the_row_structure(self):
        from opendcc.usd_editor.usd_layers import UsdLayersModel, UsdLayersPanel

        root_layer = self.root

        class FlatLayerModel(UsdLayersModel):
            def rowCount(self, parent=QtCore.QModelIndex()):
                return 0 if parent.isValid() else 1

            def index(self, row, column, parent=QtCore.QModelIndex()):
                if parent.isValid() or row != 0:
                    return QtCore.QModelIndex()
                return self.createIndex(row, column)

            def parent(self, index):
                return QtCore.QModelIndex()

            def data(self, index, role=QtCore.Qt.DisplayRole):
                if not index.isValid():
                    return None
                if role == QtCore.Qt.DisplayRole and index.column() == 0:
                    return "Package root"
                if role == int(UsdLayersModel.LayerIdentifierRole):
                    return root_layer.identifier
                if role == int(UsdLayersModel.LayerIsRootRole):
                    return True
                return None

        model = FlatLayerModel()
        panel = UsdLayersPanel(model=model)
        try:
            self.assertEqual(panel.view.model().rowCount(), 1)
            self.app.set_layer_selection({self.root})
            self.assertEqual(panel.view.selectionModel().selectedRows()[0].data(), "Package root")
            self.app.set_layer_selection(set())
            panel.view.selectionModel().select(
                panel.view.model().index(0, 0),
                QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows,
            )
            self.assertEqual(self.app.get_layer_selection(), {self.root})
            panel._search_toggle.setChecked(True)
            panel.search.setText("Package")
            self.assertEqual(panel.view.model().rowCount(), 1)
            panel.search.setText("missing")
            self.assertEqual(panel.view.model().rowCount(), 0)
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_panel_column_labels_and_visible_columns_are_configurable(self):
        from opendcc.usd_editor.usd_layers import UsdLayersModel, UsdLayersPanel

        columns = UsdLayersModel
        default_panel = UsdLayersPanel()
        try:
            visible = {
                column
                for column in range(default_panel.view.model().columnCount())
                if not default_panel.view.isColumnHidden(column)
            }
            self.assertEqual(
                visible,
                {
                    int(columns.NameColumn),
                    int(columns.EditTargetColumn),
                    int(columns.MutedColumn),
                    int(columns.DirtyColumn),
                },
            )
        finally:
            default_panel.close()
            default_panel.deleteLater()
            self.qt_app.processEvents()
        panel = UsdLayersPanel(
            column_labels={int(columns.NameColumn): "Package Layers"},
            visible_columns={int(columns.NameColumn), int(columns.EditTargetColumn)},
        )
        try:
            model = panel.view.model()
            self.assertEqual(model.headerData(0, QtCore.Qt.Horizontal), "Package Layers")
            self.assertFalse(panel.view.isColumnHidden(int(columns.NameColumn)))
            self.assertFalse(panel.view.isColumnHidden(int(columns.EditTargetColumn)))
            self.assertTrue(panel.view.isColumnHidden(int(columns.MutedColumn)))
            self.assertTrue(panel.view.isColumnHidden(int(columns.VersionColumn)))
        finally:
            panel.close()
            panel.deleteLater()
            self.qt_app.processEvents()

    def test_reload_cancel_preserves_edits_and_does_not_call_a_command(self):
        with tempfile.TemporaryDirectory() as directory:
            layer = Sdf.Layer.CreateNew(str(Path(directory) / "reload.usda"))
            layer.Save()
            layer.ImportFromString('#usda 1.0\n\ndef Xform "Keep" {}\n')
            before = layer.ExportToString()
            with patch.object(
                QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Cancel
            ), patch.object(utils.cmds, "reload_layer") as command:
                self.assertFalse(utils.reload_layer(layer.identifier))
                command.assert_not_called()
            self.assertEqual(layer.ExportToString(), before)

    def test_save_anonymous_root_as_exports_without_scene_mutation(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(utils, "_add_recent_file"):
            self.root.ImportFromString('#usda 1.0\n\ndef Xform "RootCopy" {}\n')
            self.stack.clear()
            path = str(Path(directory) / "root_copy.usda")
            with patch.object(QtWidgets.QFileDialog, "getSaveFileName", return_value=(path, "")):
                self.assertTrue(utils.save_layer_as(self.root.identifier))
            saved = Sdf.Layer.FindOrOpen(path)
            self.assertTrue(saved.GetPrimAtPath("/RootCopy"))
            self.assertEqual(self.stage.GetRootLayer(), self.root)
            self.assertEqual(self.stack.get_size(), 0)
