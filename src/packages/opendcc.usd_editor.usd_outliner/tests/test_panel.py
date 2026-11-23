# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf, Usd, UsdGeom
import opendcc.core as core
from opendcc import cmds
from opendcc.usd_editor.usd_outliner import UsdOutliner, UsdStageDataSource, usd_item_id
from outliner_test_utils import OutlinerTest, flush
from opendcc.ui.scene_outliner import Change, SceneOutliner


class TestPanel(OutlinerTest):
    def test_panels_menu_opens_multiple_usd_outliners(self):
        main_window = next(
            widget
            for widget in QtWidgets.QApplication.topLevelWidgets()
            if isinstance(widget, QtWidgets.QMainWindow)
        )
        menu_bar = main_window.menuBar()
        panels_action = next(
            action for action in menu_bar.actions() if action.text().replace("&", "") == "Panels"
        )
        menu = panels_action.menu()
        action = next(action for action in menu.actions() if action.text() == "USD Outliner")

        def outliners():
            return {
                widget
                for widget in QtWidgets.QApplication.allWidgets()
                if isinstance(widget, SceneOutliner) and widget.isVisible()
            }

        existing = outliners()
        self.addCleanup(core.close_panels)
        action.trigger()
        action.trigger()
        flush()
        opened = outliners() - existing
        self.assertEqual(len(opened), 2)
        first, second = opened

        path = Sdf.Path("/Root/Child")
        child = usd_item_id(path)
        self.assertTrue(first.data_source().has_item(child))
        self.assertTrue(second.data_source().has_item(child))
        first.selection_context().set_selection([child])
        flush()
        self.assertEqual(first.selected_items(), [child])
        self.assertEqual(second.selected_items(), [child])
        self.assertEqual(list(self.app.get_prim_selection()), [path])

        first.set_search_text("Child")
        first.tree_view().set_show_root(False)
        self.assertEqual(second.search_text(), "")
        self.assertTrue(second.tree_view().shows_root())

    def test_show_menu_toggles_pseudo_root(self):
        menu = self.panel._show_menu
        view = self.widget.tree_view()
        for shown in (False, True):
            menu.aboutToShow.emit()
            action = next(action for action in menu.actions() if action.text() == "Pseudo-root")
            self.assertEqual(action.isChecked(), not shown)
            action.trigger()
            self.assertEqual(action.isChecked(), shown)
            self.assertEqual(view.shows_root(), shown)
            self.assertEqual(view.rootIndex().isValid(), not shown)

    def test_context_menu_rename_visibility_and_variant_actions(self):
        self.checked(cmds.select, core.SelectionList([Sdf.Path("/Root/Child")]), replace=True)
        menu = QtWidgets.QMenu(self.widget)
        self.widget.context_menu_requested.emit(menu)
        actions = {action.text(): action for action in menu.actions()}
        self.assertTrue({"Copy Prim Path", "Rename", "Duplicate", "Delete"}.issubset(actions))
        self.view.expand(self.find("Root"))
        self.view.setCurrentIndex(self.find("Child"))
        actions["Rename"].trigger()
        flush()
        editor = self.view.findChild(QtWidgets.QLineEdit)
        self.assertIsNotNone(editor)
        self.assertEqual(editor.text(), "Child")
        editor.setText("RenamedChild")
        QtCore.QCoreApplication.sendEvent(
            editor,
            QtGui.QKeyEvent(QtCore.QEvent.KeyPress, QtCore.Qt.Key_Return, QtCore.Qt.NoModifier),
        )
        flush()
        self.assertTrue(self.stage.GetPrimAtPath("/Root/RenamedChild"))
        self.undo.undo()
        flush()
        actions["Make Invisible"].trigger()
        flush()
        self.assertEqual(
            UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root/Child")).GetVisibilityAttr().Get(),
            "invisible",
        )
        self.undo.undo()
        flush()
        variants = QtWidgets.QMenu(self.widget)
        self.panel._add_variants_submenu(variants, self.stage.GetPrimAtPath("/Root"))
        submenu = variants.actions()[0].menu().actions()[0].menu()
        next(a for a in submenu.actions() if a.text() == "blue").trigger()
        flush()
        self.assertTrue(self.find("blue").isValid())
        submenu.actions()[0].trigger()
        flush()
        self.assertEqual(
            self.stage.GetPrimAtPath("/Root").GetVariantSets().GetVariantSelection("look"), ""
        )

    def test_show_filters_preserve_external_selection(self):
        self.checked(cmds.set_prim_active, [Sdf.Path("/Other")], False)
        self.checked(
            cmds.select,
            core.SelectionList([Sdf.Path("/Root/Child"), Sdf.Path("/Other")]),
            replace=True,
        )
        selection = list(self.app.get_prim_selection())
        count = self.undo.get_size()
        menu_bar = self.widget.findChild(QtWidgets.QMenuBar)
        show_action = menu_bar.actions()[0]
        show_menu = show_action.menu()
        action = next(a for a in show_menu.actions() if a.text() == "Inactive Prims")
        action.setChecked(False)
        flush()
        self.assertFalse(self.find("Other").isValid())
        self.assertEqual(list(self.app.get_prim_selection()), selection)
        self.assertEqual(self.undo.get_size(), count)
        action.setChecked(True)
        flush()
        self.assertTrue(self.view.selectionModel().isSelected(self.find("Other")))
        source = self.widget.data_source()
        source.notify(Change.reset())
        action = next(a for a in show_menu.actions() if a.text() == "Inactive Prims")
        self.assertTrue(action.isChecked())
        abstract = next(a for a in show_menu.actions() if a.text() == "Abstract Prims (Classes)")
        abstract.setChecked(True)
        source.notify(Change.reset())
        abstract = next(a for a in show_menu.actions() if a.text() == "Abstract Prims (Classes)")
        self.assertTrue(abstract.isChecked())
        self.widget.set_data_source(UsdStageDataSource())
        abstract = next(a for a in show_menu.actions() if a.text() == "Abstract Prims (Classes)")
        self.assertFalse(abstract.isChecked())

    def test_widget_destruction_preserves_selection(self):
        self.checked(cmds.select, core.SelectionList([Sdf.Path("/Root/Child")]), replace=True)
        widget = UsdOutliner()
        self.assertIsInstance(widget, QtWidgets.QWidget)
        selection = list(self.app.get_prim_selection())
        count = self.undo.get_size()
        widget.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        flush()
        self.assertEqual(list(self.app.get_prim_selection()), selection)
        self.assertEqual(self.undo.get_size(), count)

    def test_variant_menu_cannot_edit_a_replacement_stage(self):
        self.app.set_prim_selection([Sdf.Path("/Root")])
        menu = QtWidgets.QMenu(self.widget)
        self.panel._populate_context_menu(menu)
        variant_menu = next(a.menu() for a in menu.actions() if a.text() == "Variants")
        look_menu = next(a.menu() for a in variant_menu.actions() if a.text() == "look")
        blue = next(a for a in look_menu.actions() if a.text() == "blue")
        replacement = Usd.Stage.CreateInMemory()
        self.addCleanup(self.close_stage, replacement)
        variants = replacement.DefinePrim("/Root").GetVariantSets().AddVariantSet("look")
        variants.AddVariant("red")
        variants.AddVariant("blue")
        variants.SetVariantSelection("red")
        self.session.set_current_stage(replacement)
        before = replacement.GetRootLayer().ExportToString()
        blue.trigger()
        self.assertEqual(replacement.GetRootLayer().ExportToString(), before)
        self.assertFalse(self.undo.can_undo())
