# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
import unittest

from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf, Usd, UsdGeom
from opendcc import cmds
import opendcc.core as core
from opendcc.ui.scene_outliner import SceneOutliner
from outliner_test_utils import OutlinerTest, click, flush, wait_for
from qt_input import offscreen_cursor_at


@unittest.skipUnless(
    QtGui.QGuiApplication.platformName() == "offscreen", "Requires virtual mouse input"
)
class TestPointer(OutlinerTest):
    def click_at_cursor(self, widget, pos):
        with offscreen_cursor_at(widget, pos):
            flush()
            click(widget, pos)

    def test_open_context_menu_cannot_delete_from_a_replacement_stage(self):
        self.focus_view()
        self.view.expand(self.find("Root"))
        child = self.find("Child")
        self.view.scrollTo(child)
        flush()
        replacement = Usd.Stage.CreateInMemory()
        UsdGeom.Xform.Define(replacement, "/Root/Child")
        self.addCleanup(self.close_stage, replacement)
        position = self.view.visualRect(child).center()
        before = self.stage.GetRootLayer().ExportToString()
        replacement_before = replacement.GetRootLayer().ExportToString()
        failures, handled, undo_sizes = [], [], []

        def switch_and_delete():
            menu = QtWidgets.QApplication.activePopupWidget()
            try:
                self.assertIsInstance(menu, QtWidgets.QMenu)
                self.session.set_current_stage(replacement)
                undo_sizes.append(self.undo.get_size())
                action = next(a for a in menu.actions() if a.text() == "Delete")
                action.trigger()
                handled.append(True)
            except Exception as error:
                failures.append(error)
            finally:
                if menu:
                    menu.close()

        QtCore.QTimer.singleShot(30, switch_and_delete)
        QtCore.QCoreApplication.sendEvent(
            self.view.viewport(),
            QtGui.QContextMenuEvent(
                QtGui.QContextMenuEvent.Mouse,
                position,
                self.view.viewport().mapToGlobal(position),
            ),
        )
        self.assertEqual(failures, [])
        self.assertEqual(handled, [True])
        self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
        self.assertEqual(replacement.GetRootLayer().ExportToString(), replacement_before)
        self.assertEqual(self.undo.get_size(), undo_sizes[0])

    def test_hovered_outliner_reveals_selection_from_another_focused_panel(self):
        main_window = self.app.get_main_window()
        self.panel.hide()
        core.close_panels()
        self.addCleanup(core.close_panels)
        outliner_dock = core.create_panel("outliner", False)
        core.create_panel(
            "logger", False, outliner_dock.dockAreaWidget(), core.DockWidgetArea.RightDockWidgetArea
        )
        outliner = next(
            widget for widget in main_window.findChildren(SceneOutliner) if widget.isVisible()
        )
        view = outliner.tree_view()
        logger = main_window.findChild(QtWidgets.QWidget, "logger")
        focused = QtWidgets.QWidget(logger)
        focused.setFocusPolicy(QtCore.Qt.StrongFocus)
        focused.show()
        editor = QtWidgets.QLineEdit(logger)
        editor.show()
        self.addCleanup(focused.deleteLater)
        self.addCleanup(editor.deleteLater)
        QtWidgets.QApplication.setActiveWindow(main_window)
        self.assertTrue(wait_for(main_window.isActiveWindow))
        self.checked(cmds.select, core.SelectionList([Sdf.Path("/Root/Child")]), replace=True)
        view.collapseAll()
        expansion = set(view.expanded_items())
        count = self.undo.get_size()

        def send_f(target):
            viewport = view.viewport()
            with offscreen_cursor_at(viewport, viewport.rect().center()):
                self.assertIs(QtWidgets.QApplication.widgetAt(QtGui.QCursor.pos()), viewport)
                for event_type in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                    QtCore.QCoreApplication.sendEvent(
                        target,
                        QtGui.QKeyEvent(event_type, QtCore.Qt.Key_F, QtCore.Qt.NoModifier, "f"),
                    )

        focused.setFocus()
        self.assertTrue(wait_for(focused.hasFocus))
        send_f(focused)
        self.assertTrue(view.hasFocus())
        self.assertNotEqual(set(view.expanded_items()), expansion)
        self.assertEqual(self.undo.get_size(), count)
        self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Root/Child")])

        view.collapseAll()
        expansion = set(view.expanded_items())
        editor.setFocus()
        self.assertTrue(wait_for(editor.hasFocus))
        send_f(editor)
        self.assertTrue(editor.hasFocus())
        self.assertEqual(editor.text(), "f")
        self.assertEqual(set(view.expanded_items()), expansion)

    def test_panels_menu_opens_the_usd_outliner(self):
        main_window = next(
            widget
            for widget in QtWidgets.QApplication.topLevelWidgets()
            if isinstance(widget, QtWidgets.QMainWindow)
        )
        menu_bar = main_window.menuBar()
        self.addCleanup(main_window.setGeometry, main_window.geometry())
        main_window.resize(main_window.size().expandedTo(menu_bar.sizeHint()))
        main_window.move(QtGui.QGuiApplication.primaryScreen().availableGeometry().topLeft())
        flush()
        panels_action = next(
            action for action in menu_bar.actions() if action.text().replace("&", "") == "Panels"
        )
        menu = panels_action.menu()
        actions = menu.actions()
        action = next(action for action in actions if action.text() == "USD Outliner")
        self.assertFalse(any(entry.isSeparator() for entry in actions[: actions.index(action)]))
        QtWidgets.QApplication.setActiveWindow(main_window)
        self.assertTrue(wait_for(main_window.isActiveWindow))
        position = menu_bar.actionGeometry(panels_action).center()
        self.assertTrue(menu_bar.rect().contains(position))
        self.click_at_cursor(menu_bar, position)
        self.assertTrue(wait_for(menu.isVisible))
        self.click_at_cursor(menu, menu.actionGeometry(action).center())
        self.assertTrue(
            wait_for(
                lambda: any(
                    isinstance(widget, SceneOutliner)
                    and widget is not self.widget
                    and widget.isVisible()
                    for widget in QtWidgets.QApplication.allWidgets()
                )
            )
        )
        core.close_panels()
        flush()

    def test_context_menu_click_authors_visibility_and_restores_on_undo(self):
        self.focus_view()
        self.view.expand(self.find("Root"))
        child = self.find("Child")
        self.view.scrollTo(child)
        flush()
        position = self.view.visualRect(child).center()
        seen = []
        failures = []

        def choose_action():
            menu = QtWidgets.QApplication.activePopupWidget()
            try:
                self.assertIsInstance(menu, QtWidgets.QMenu)
                seen.append(list(self.app.get_prim_selection()))
                action = next(a for a in menu.actions() if a.text() == "Make Invisible")
                self.click_at_cursor(menu, menu.actionGeometry(action).center())
            except Exception as error:
                failures.append(error)
                if menu:
                    menu.close()

        timer = QtCore.QTimer(self.widget)
        timer.setSingleShot(True)
        timer.timeout.connect(choose_action)
        timer.start(30)
        event = QtGui.QContextMenuEvent(
            QtGui.QContextMenuEvent.Mouse,
            position,
            self.view.viewport().mapToGlobal(position),
        )
        QtCore.QCoreApplication.sendEvent(self.view.viewport(), event)
        flush()
        self.assertEqual(failures, [])
        self.assertEqual(seen, [[Sdf.Path("/Root/Child")]])
        visibility = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root/Child")).GetVisibilityAttr()
        self.assertEqual(visibility.Get(), "invisible")
        self.undo.undo()
        flush()
        self.assertEqual(visibility.Get(), "inherited")

    def test_header_menu_mouse_click_hides_and_shows_column(self):
        self.focus_view()
        header = self.view.header()
        column = self.column("Type")
        errors = []

        def choose():
            menu = QtWidgets.QApplication.activePopupWidget()
            try:
                self.assertIsInstance(menu, QtWidgets.QMenu)
                action = next(a for a in menu.actions() if a.text() == "Type")
                self.assertTrue(action.isEnabled())
                self.click_at_cursor(menu, menu.actionGeometry(action).center())
            except Exception as error:
                errors.append(error)
                if menu:
                    menu.close()

        initial = self.view.isColumnHidden(column)
        for hidden in (not initial, initial):
            position = QtCore.QPoint(20, header.height() // 2)
            timer = QtCore.QTimer(self.widget)
            timer.setSingleShot(True)
            timer.timeout.connect(choose)
            timer.start(30)
            QtCore.QCoreApplication.sendEvent(
                header.viewport(),
                QtGui.QContextMenuEvent(
                    QtGui.QContextMenuEvent.Mouse,
                    position,
                    header.mapToGlobal(position),
                ),
            )
            self.assertEqual(errors, [])
            self.assertEqual(self.view.isColumnHidden(column), hidden)
