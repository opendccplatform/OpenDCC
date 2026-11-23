# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf, UsdGeom
from opendcc import cmds
import opendcc.core as core
from opendcc.ui.scene_outliner import SceneOutliner, Change
from opendcc.usd_editor.usd_outliner import UsdStageDataSource
from outliner_test_utils import OutlinerTest, click, flush, wait_for


def key(widget, code, text="", modifiers=QtCore.Qt.NoModifier):
    for event_type in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtCore.QCoreApplication.sendEvent(
            widget, QtGui.QKeyEvent(event_type, code, modifiers, text)
        )
    flush()


class TestGui(OutlinerTest):
    def test_default_layout_docks_the_usd_outliner_next_to_viewport(self):
        main_window = self.app.get_main_window()
        menu = main_window.menuBar().findChild(QtWidgets.QMenu, "layouts_menu")
        self.assertIsNotNone(menu)
        menu.actions()[0].trigger()
        flush()
        widgets = [
            widget for widget in main_window.findChildren(SceneOutliner) if widget.isVisible()
        ]
        self.assertEqual(len(widgets), 1)
        self.assertIs(widgets[0].window(), main_window)
        dock = core.create_panel("outliner", floating=False)
        viewport = main_window.findChild(QtWidgets.QWidget, "viewport")
        self.assertIsNotNone(viewport)
        self.assertIsNotNone(dock.dockAreaWidget())
        self.assertLess(
            widgets[0].mapToGlobal(QtCore.QPoint()).x(), viewport.mapToGlobal(QtCore.QPoint()).x()
        )

    def test_external_selection_expands_only_after_f_in_the_outliner(self):
        UsdGeom.Xform.Define(self.stage, "/Other/Leaf")
        UsdGeom.Xform.Define(self.stage, "/FocusCandidate")
        flush()
        self.view.collapseAll()
        expansion = set(self.view.expanded_items())
        scroll = self.view.verticalScrollBar().value()
        paths = [Sdf.Path("/Root/Child"), Sdf.Path("/Other/Leaf")]
        self.checked(cmds.select, core.SelectionList(paths), replace=True)
        selection = list(self.app.get_prim_selection())
        self.assertEqual(set(self.view.expanded_items()), expansion)
        self.assertEqual(self.view.verticalScrollBar().value(), scroll)
        count = self.undo.get_size()
        self.focus_view()
        shortcut_type = getattr(QtGui, "QShortcut", None) or QtWidgets.QShortcut
        shortcut = shortcut_type(QtGui.QKeySequence("F"), self.widget)
        shortcut.setContext(QtCore.Qt.ApplicationShortcut)
        triggered = []
        shortcut.activated.connect(lambda: triggered.append(True))
        override = QtGui.QKeyEvent(
            QtCore.QEvent.ShortcutOverride, QtCore.Qt.Key_F, QtCore.Qt.NoModifier
        )
        override.ignore()
        QtCore.QCoreApplication.sendEvent(self.view, override)
        self.assertTrue(override.isAccepted())
        key(self.view, QtCore.Qt.Key_F, "f")
        for name in ("Root", "Other"):
            self.assertTrue(self.view.isExpanded(self.find(name).sibling(self.find(name).row(), 0)))
        self.assertFalse(triggered)
        self.assertEqual(list(self.app.get_prim_selection()), selection)
        self.assertEqual(self.undo.get_size(), count)
        self.checked(cmds.select, core.SelectionList(), clear=True)
        count = self.undo.get_size()
        key(self.view, QtCore.Qt.Key_F, "f")
        self.assertFalse(self.widget.selected_items())
        self.assertEqual(self.undo.get_size(), count)

    def test_empty_layout_creates_only_viewport(self):
        core.close_panels()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        flush()
        path = Path.cwd() / "empty-layout.ini"
        path.write_text("")
        core.load_layout(str(path))
        flush()
        main_window = self.app.get_main_window()
        self.assertIsNotNone(main_window.findChild(QtWidgets.QWidget, "viewport"))
        self.assertFalse(
            any(widget.isVisible() for widget in main_window.findChildren(SceneOutliner))
        )

    def test_floating_panel_closure_keeps_saved_layout_restoration_safe(self):
        core.create_panel("outliner", floating=True)
        flush()
        core.close_panels()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        flush()
        core.create_panel("logger", floating=False)
        path = Path.cwd() / "after-floating-layout.ini"
        core.save_layout(str(path))
        core.load_layout(str(path))
        flush()
        self.assertIsNotNone(self.app.get_main_window().findChild(QtWidgets.QWidget, "logger"))

    def test_saved_layout_does_not_reinsert_the_default_outliner(self):
        core.close_panels()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        flush()
        core.create_panel("logger", floating=False)
        path = Path.cwd() / "logger-layout.ini"
        core.save_layout(str(path))
        core.load_layout(str(path))
        flush()
        main_window = self.app.get_main_window()
        self.assertFalse(
            any(widget.isVisible() for widget in main_window.findChildren(SceneOutliner))
        )
        self.assertIsNotNone(main_window.findChild(QtWidgets.QWidget, "logger"))

    def row_click(self, name, modifiers=QtCore.Qt.NoModifier):
        index = self.find(name)
        self.assertTrue(index.isValid())
        self.view.scrollTo(index)
        # Queued filter updates can invalidate the proxy index used to scroll.
        flush()
        index = self.find(name)
        self.assertTrue(index.isValid(), name)
        viewport = self.view.viewport()
        rect = self.view.visualRect(index).intersected(viewport.rect())
        self.assertFalse(rect.isEmpty(), name)
        position = rect.center()
        self.assertEqual(self.view.indexAt(position), index, name)
        click(viewport, position, modifiers)

    def test_mouse_selection_keyboard_expansion_and_delete_shortcut(self):
        self.focus_view()
        self.row_click("Root")
        key(self.view, QtCore.Qt.Key_Right)
        root = self.find("Root")
        self.assertTrue(self.view.isExpanded(root.sibling(root.row(), 0)))
        self.row_click("Child")
        self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Root/Child")])
        self.row_click("Other", QtCore.Qt.ControlModifier)
        self.assertEqual(
            set(self.app.get_prim_selection()), {Sdf.Path("/Root/Child"), Sdf.Path("/Other")}
        )
        self.row_click("Child")
        self.assertTrue(self.view.hasFocus())
        key(self.view, QtCore.Qt.Key_Delete)
        self.assertFalse(self.stage.GetPrimAtPath("/Root/Child"))
        self.assertTrue(self.undo.can_undo())
        self.undo.undo()
        flush()
        self.assertTrue(self.find("Child").isValid())

    def test_search_button_keyboard_input_and_clearing(self):
        self.focus_view()
        self.row_click("Other")
        button = next(b for b in self.widget.findChildren(QtWidgets.QToolButton) if b.isCheckable())
        click(button, button.rect().center())
        field = self.widget.findChild(QtWidgets.QLineEdit)
        self.assertTrue(field.isVisible())
        self.assertTrue(field.hasFocus())
        for character in "Child":
            key(field, ord(character.upper()), character)
        self.assertTrue(
            wait_for(
                lambda: not self.widget.search_pending()
                and self.find("Child").isValid()
                and not self.find("Other").isValid()
            )
        )
        self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Other")])

        click(button, button.rect().center())
        self.assertFalse(field.isVisible())
        self.assertEqual(field.text(), "")
        self.assertTrue(wait_for(lambda: self.find("Other").isValid()))
        self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Other")])

    def test_inline_editor_keyboard_commits_and_escape_cancels(self):
        self.focus_view()
        self.view.expand(self.find("Root"))
        for name in ("Renamed", "\u041f\u0440\u0438\u043c_\u6a21\u578b_\u00e9_\U00010400"):
            with self.subTest(name=name):
                if not Sdf.Path.IsValidIdentifier(name):
                    self.skipTest("This USD SDK does not support Unicode prim identifiers")
                path = Sdf.Path("/Root/" + name)
                self.row_click("Child")
                key(self.view, QtCore.Qt.Key_F2)
                flush()
                editor = next(
                    e for e in self.view.findChildren(QtWidgets.QLineEdit) if e.isVisible()
                )
                key(editor, QtCore.Qt.Key_A, "a", QtCore.Qt.ControlModifier)
                if name.isascii():
                    for character in name:
                        key(editor, ord(character.upper()), character)
                else:
                    event = QtGui.QInputMethodEvent()
                    event.setCommitString(name)
                    QtCore.QCoreApplication.sendEvent(editor, event)
                self.assertEqual(editor.text(), name)
                key(editor, QtCore.Qt.Key_Return)
                self.assertTrue(self.stage.GetPrimAtPath(path))
                self.row_click(name)
                key(self.view, QtCore.Qt.Key_F2)
                flush()
                editor = next(
                    e for e in self.view.findChildren(QtWidgets.QLineEdit) if e.isVisible()
                )
                key(editor, QtCore.Qt.Key_Delete)
                self.assertTrue(self.stage.GetPrimAtPath(path))
                key(editor, QtCore.Qt.Key_Escape)
                self.assertTrue(self.stage.GetPrimAtPath(path))
                self.undo.undo()
                flush()
                self.assertTrue(self.find("Child").isValid())
                self.undo.redo()
                flush()
                self.assertTrue(self.stage.GetPrimAtPath(path))
                self.undo.undo()
                flush()

    def test_visibility_changes_pixels_and_undo_restores_them(self):
        import OpenImageIO as oiio
        from image_utils import compare_images

        self.view.expand(self.find("Root"))
        self.view.clearFocus()
        flush()
        paths = [
            Path.cwd() / ("outliner-visibility-" + name + ".png")
            for name in ("before", "hidden", "undo")
        ]
        self.assertTrue(self.view.viewport().grab().save(str(paths[0])))
        self.checked(cmds.set_prim_visibility, [Sdf.Path("/Root/Child")], False)
        self.assertTrue(self.view.viewport().grab().save(str(paths[1])))
        self.undo.undo()
        flush()
        self.assertTrue(self.view.viewport().grab().save(str(paths[2])))
        buffers = [oiio.ImageBuf(str(path)) for path in paths]
        for buffer in buffers:
            self.assertTrue(buffer.read(0, 0, True), buffer.geterror())
        self.assertGreater(compare_images(buffers[0], buffers[1]).nfail, 0)
        self.assertEqual(compare_images(buffers[0], buffers[2]).nfail, 0)

    def test_header_menu_actions_follow_column_ids_after_schema_changes(self):
        class ReorderedSource(UsdStageDataSource):
            reverse_columns = False

            def columns(self):
                columns = [column for column in super().columns() if column.id in ("name", "type")]
                for column in columns:
                    column.default_visible = True
                return list(reversed(columns)) if self.reverse_columns else columns

        for replace_source in (False, True):
            with self.subTest(replace_source=replace_source):
                source = ReorderedSource()
                self.widget.set_data_source(source)
                menu = QtWidgets.QMenu(self.widget)
                self.panel._populate_header_menu(menu)
                action = next(a for a in menu.actions() if a.text() == "Type")
                if replace_source:
                    replacement = ReorderedSource()
                    replacement.reverse_columns = True
                    self.widget.set_data_source(replacement)
                else:
                    source.reverse_columns = True
                    source.notify(Change.reset())
                action.trigger()
                self.assertFalse(self.view.isColumnHidden(self.widget.model().tree_column()))
                self.assertEqual(self.view.isColumnHidden(0), not replace_source)
                menu.deleteLater()
