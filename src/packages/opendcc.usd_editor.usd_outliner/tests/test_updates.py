# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore
import threading
from pxr import Sdf, Usd, UsdGeom, UsdShade
import opendcc.core as core
from opendcc import cmds
from opendcc.usd_editor.usd_outliner import usd_item_id
from outliner_test_utils import OutlinerTest, flush, wait_for


class TestUpdates(OutlinerTest):
    def test_duplicate_keeps_the_selection_until_the_replacement_row_exists(self):
        path = Sdf.Path("/Plane")
        UsdGeom.Mesh.Define(self.stage, path)
        flush()
        self.checked(cmds.select, core.SelectionList([path]), replace=True)
        flush()
        self.assertEqual(self.widget.selected_items(), [usd_item_id(path)])

        observed = []
        self.view.selectionModel().selectionChanged.connect(
            lambda *_: observed.append(self.widget.selected_items())
        )
        removed = []
        self.proxy.rowsRemoved.connect(lambda *args: removed.append(args[1:]))
        expected = []
        for number in range(1, 4):
            self.checked(cmds.duplicate_prim, paths=[path])
            duplicate = [usd_item_id(Sdf.Path(f"/Plane{number}"))]
            self.assertTrue(wait_for(lambda: self.widget.selected_items() == duplicate))
            expected.append(duplicate)
        self.assertEqual(observed, expected)
        self.assertEqual(removed, [])

    def test_application_stage_worker_updates_are_coalesced_on_the_gui_thread(self):
        owner = threading.get_ident()
        source = self.widget.data_source()
        root = self.widget.index_for_item(usd_item_id(Sdf.Path("/Root")))
        self.model.rowCount(root)
        delivered = []
        source.changed.connect(
            lambda change: delivered.append(threading.get_ident()), QtCore.Qt.DirectConnection
        )
        ready = threading.Event()
        release = threading.Event()
        errors = []

        def write():
            ready.set()
            release.wait()
            try:
                for number in range(100):
                    self.stage.DefinePrim(f"/Root/Worker{number}", "Xform")
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=write)
        worker.start()
        ready.wait()
        release.set()
        # The GUI does not read the stage or process events until writing finishes.
        worker.join()
        self.assertEqual(errors, [])
        self.assertEqual(delivered, [])
        flush()
        self.assertEqual(delivered, [owner])
        self.assertTrue(self.find("Worker99").isValid())

    def test_inherited_columns_notify_materialized_children(self):
        root = self.stage.GetPrimAtPath("/Root")
        root.SetMetadata("kind", "component")
        model = UsdGeom.ModelAPI.Apply(root)
        model.CreateModelDrawModeAttr().Set("bounds")
        first = UsdShade.Material.Define(self.stage, "/Mat1")
        second = UsdShade.Material.Define(self.stage, "/Mat2")
        binding = UsdShade.MaterialBindingAPI.Apply(root)
        binding.Bind(first)
        flush()
        child = usd_item_id(Sdf.Path("/Root/Child"))
        index = self.model.ensure_index_for_item(child)
        changes = []
        self.model.dataChanged.connect(lambda first, last, *args: changes.append((first, last)))

        def assert_child_changed():
            self.assertTrue(
                any(
                    first.parent() == index.parent() and first.row() <= index.row() <= last.row()
                    for first, last in changes
                )
            )
            changes.clear()

        model.GetModelDrawModeAttr().Set("cards")
        flush()
        self.assertEqual(self.widget.data_source().column_text(child, "drawmode"), "cards")
        assert_child_changed()
        binding.Bind(second)
        flush()
        self.assertEqual(self.widget.data_source().column_text(child, "material:full"), "/Mat2")
        assert_child_changed()
        root.RemoveProperty("model:drawMode")
        flush()
        assert_child_changed()

    def test_edit_target_changes_refresh_badges_without_resetting_the_tree(self):
        layer = Sdf.Layer.CreateAnonymous()
        self.stage.GetRootLayer().subLayerPaths.append(layer.identifier)
        flush()
        child = usd_item_id(Sdf.Path("/Root/Child"))
        source = self.widget.data_source()
        before = source.icon(child).cacheKey()
        changes, resets = [], []
        self.model.dataChanged.connect(lambda *args: changes.append(True))
        self.model.modelReset.connect(lambda: resets.append(True))
        self.stage.SetEditTarget(layer)
        flush()
        self.assertNotEqual(source.icon(child).cacheKey(), before)
        self.assertTrue(changes)
        self.assertFalse(resets)

    def test_selection_synchronizes_both_directions(self):
        self.view.expand(self.find("Root"))
        self.view.selectionModel().select(
            self.find("Child"),
            QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows,
        )
        self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Root/Child")])
        self.checked(cmds.select, core.SelectionList([Sdf.Path("/Other")]), replace=True)
        self.assertTrue(self.view.selectionModel().isSelected(self.find("Other")))

    def test_search_preserves_external_selection_and_refreshes_changes(self):
        self.checked(
            cmds.select,
            core.SelectionList([Sdf.Path("/Root/Child"), Sdf.Path("/Other")]),
            replace=True,
        )
        selection = list(self.app.get_prim_selection())
        count = self.undo.get_size()
        self.filter("Child")
        self.assertTrue(self.find("Child").isValid())
        self.assertFalse(self.find("Other").isValid())
        self.assertEqual(list(self.app.get_prim_selection()), selection)
        self.assertEqual(self.undo.get_size(), count)
        UsdGeom.Xform.Define(self.stage, "/Root/NewChild")
        flush()
        self.assertTrue(self.find("NewChild").isValid())
        self.checked(cmds.set_prim_active, [Sdf.Path("/Root")], False)
        self.assertFalse(self.find("NewChild").isValid())
        self.undo.undo()
        flush()
        self.assertTrue(self.find("NewChild").isValid())
        with Sdf.ChangeBlock():
            self.stage.GetRootLayer().GetPrimAtPath("/Root").nameChildren[
                "NewChild"
            ].name = "NewOther"
        flush()
        self.assertFalse(self.find("NewOther").isValid())
        self.filter("does not match")
        self.assertEqual(self.proxy.rowCount(), 0)
        self.filter("")
        self.assertTrue(self.view.selectionModel().isSelected(self.find("Other")))
        self.assertEqual(list(self.app.get_prim_selection()), selection)
        self.filter("red")
        self.checked(cmds.set_variant_selection, Sdf.Path("/Root"), "look", "blue")
        self.assertFalse(self.find("red").isValid())
        self.undo.undo()
        flush()
        self.assertTrue(self.find("red").isValid())

    def test_empty_switched_closed_stages_and_wide_batched_updates(self):
        empty = Usd.Stage.CreateInMemory()
        self.addCleanup(self.close_stage, empty)
        self.session.set_current_stage(empty)
        flush()
        self.assertEqual(self.proxy.rowCount(self.proxy.index(0, 0)), 0)
        layer = Sdf.Layer.CreateAnonymous()
        with Sdf.ChangeBlock():
            root = Sdf.PrimSpec(layer, "Wide", Sdf.SpecifierDef, "Xform")
            for i in range(5000):
                Sdf.PrimSpec(root, "Item" + str(i), Sdf.SpecifierDef, "Xform")
        wide = Usd.Stage.Open(layer)
        self.addCleanup(self.close_stage, wide)
        self.session.set_current_stage(wide)
        flush()
        index = self.model.index(0, 0, self.model.index(0, 0))
        self.assertTrue(wait_for(lambda: self.model.rowCount(index) == 5000))
        layouts = []
        insertions = []
        self.model.layoutChanged.connect(lambda *args: layouts.append(True))
        self.model.rowsInserted.connect(
            lambda parent, first, last: insertions.append((first, last))
        )
        with Sdf.ChangeBlock():
            for i in range(100):
                Sdf.PrimSpec(root, "Added" + str(i), Sdf.SpecifierDef, "Xform")
        flush()
        self.assertEqual(self.model.rowCount(index), 5100)
        self.assertFalse(layouts)
        self.assertEqual(insertions, [(5000, 5099)])
        self.session.close_all()
        flush()
        self.assertEqual(self.proxy.rowCount(), 0)

    def test_stage_switch_while_a_wide_branch_is_loading(self):
        layer = Sdf.Layer.CreateAnonymous()
        with Sdf.ChangeBlock():
            wide = Sdf.PrimSpec(layer, "Wide", Sdf.SpecifierDef, "Xform")
            for i in range(2050):
                Sdf.PrimSpec(wide, "Item" + str(i), Sdf.SpecifierDef, "Xform")
        previous = Usd.Stage.Open(layer)
        replacement = Usd.Stage.CreateInMemory()
        replacement.DefinePrim("/Replacement", "Xform")
        self.addCleanup(self.close_stage, previous)
        self.addCleanup(self.close_stage, replacement)
        self.session.set_current_stage(previous)
        flush()
        branch = self.model.index(0, 0, self.model.index(0, 0))
        self.assertTrue(branch.isValid())
        published = []

        def after_insert(parent, first, last):
            if parent != branch:
                return
            published.append((first, last))
            if first == 0:
                self.session.set_current_stage(replacement)

        self.model.rowsInserted.connect(after_insert)
        self.model.fetchMore(branch)
        self.assertTrue(wait_for(lambda: self.session.get_current_stage() == replacement))
        flush()
        self.model.rowsInserted.disconnect(after_insert)
        self.assertEqual(published, [(0, 1023)])
        root = self.model.index(0, 0)
        self.assertTrue(wait_for(lambda: self.model.rowCount(root) == 1))
        self.assertEqual(self.model.index(0, self.name_column, root).data(), "Replacement")
