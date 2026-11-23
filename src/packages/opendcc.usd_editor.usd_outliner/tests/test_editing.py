# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore
from pxr import Gf, Sdf, Usd, UsdGeom
import opendcc.core as core
from opendcc import cmds
from opendcc.usd_editor.usd_outliner import usd_item_id, usd_path_of
from outliner_test_utils import OutlinerTest, flush
from qt_input import drag_events, mouse_click


class TestEditing(OutlinerTest):
    def test_activation_variants_and_payload_refresh_hierarchy(self):
        cases = (
            (cmds.set_prim_active, ([Sdf.Path("/Root")], False), "Child", False),
            (cmds.set_variant_selection, (Sdf.Path("/Root"), "look", "blue"), "blue", True),
            (cmds.set_payload_loaded, ([Sdf.Path("/Root/Payload")], False), "Inside", False),
        )
        for command, args, name, expected in cases:
            with self.subTest(command=command.__name__):
                self.checked(command, *args)
                self.assertEqual(self.find(name).isValid(), expected)
                self.undo.undo()
                flush()
                self.assertEqual(self.find(name).isValid(), not expected)

    def test_text_columns_author_undoable_commands_and_reject_invalid_values(self):
        root = self.stage.GetPrimAtPath("/Root")
        for title, value, check in (
            ("Type", "Scope", lambda: root.GetTypeName() == "Scope"),
            ("Kind", "component", lambda: root.GetMetadata("kind") == "component"),
            (
                "Draw Mode",
                "cards",
                lambda: UsdGeom.ModelAPI(root).GetModelDrawModeAttr().Get() == "cards",
            ),
        ):
            with self.subTest(column=title):
                index = self.proxy.mapToSource(self.find("Root"))
                cell = index.sibling(index.row(), self.column(title))
                self.assertTrue(self.model.setData(cell, value, QtCore.Qt.EditRole))
                flush()
                self.assertTrue(check())
                self.assertTrue(self.undo.can_undo())
                self.undo.undo()
                flush()
        index = self.proxy.mapToSource(self.find("Root"))
        self.assertFalse(
            self.model.setData(
                index.sibling(index.row(), self.column("Draw Mode")), "invalid", QtCore.Qt.EditRole
            )
        )

    def click_toggle(self, name, column):
        self.view.setColumnHidden(column, False)
        self.view.expandAll()
        index = self.find(name)
        index = index.sibling(index.row(), column)
        self.view.scrollTo(index)
        flush()
        rect = self.view.visualRect(index)
        self.assertFalse(rect.isEmpty())
        mouse_click(self.view.viewport(), rect.center())
        flush()

    def test_toggle_columns_route_commands_without_changing_selection(self):
        self.checked(cmds.select, core.SelectionList([Sdf.Path("/Other")]), replace=True)
        for name, column, check in (
            (
                "Child",
                0,
                lambda: UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root/Child"))
                .GetVisibilityAttr()
                .Get()
                == "invisible",
            ),
            ("Root", 1, lambda: not self.stage.GetPrimAtPath("/Root").IsActive()),
            ("Payload", 2, lambda: not self.stage.GetPrimAtPath("/Root/Payload").IsLoaded()),
        ):
            with self.subTest(column=column):
                before = list(self.app.get_prim_selection())
                self.click_toggle(name, column)
                self.assertTrue(check())
                self.assertEqual(list(self.app.get_prim_selection()), before)
                self.assertTrue(self.undo.can_undo())
                self.undo.undo()
                flush()

    def test_reparent_uses_captured_drag_items_preserves_world_transform(self):
        index = self.proxy.mapToSource(self.find("Child"))
        mime = self.model.mimeData([index.sibling(index.row(), 0)])
        self.assertEqual(bytes(mime.data("application/x-sdfpaths")), b"/Root/Child")
        before = UsdGeom.Xformable(
            self.stage.GetPrimAtPath("/Root/Child")
        ).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
        self.checked(cmds.select, core.SelectionList([Sdf.Path("/Root")]), replace=True)
        undo_size = self.undo.get_size()
        self.widget.data_source().reparent(
            [usd_item_id(Sdf.Path("/Root/Child"))], usd_item_id(Sdf.Path("/Root"))
        )
        self.assertEqual(self.undo.get_size(), undo_size)
        self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Root")])
        self.view.expandAll()
        self.view.scrollTo(self.find("Other"))
        flush()
        position = self.view.visualRect(self.find("Other")).center()
        for event in drag_events(mime, position):
            QtCore.QCoreApplication.sendEvent(self.view.viewport(), event)
            self.assertTrue(event.isAccepted())
        flush()
        after = UsdGeom.Xformable(
            self.stage.GetPrimAtPath("/Other/Child")
        ).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
        self.assertTrue(Gf.IsClose(before, after, 1e-6))
        self.undo.undo()
        flush()
        self.assertTrue(self.find("Child").isValid())

    def test_foreign_drag_cannot_author_or_add_undo(self):
        self.view.expandAll()
        flush()
        position = QtCore.QPoint(20, self.view.viewport().height() - 10)
        self.assertFalse(self.view.indexAt(position).isValid())
        for forge in (False, True):
            with self.subTest(forged_usd_paths=forge):
                mime = QtCore.QMimeData()
                mime.setText("foreign")
                if forge:
                    mime.setData("application/x-sdfpaths", b"/Root/Child")
                before = self.stage.GetRootLayer().ExportToString()
                count = self.undo.get_size()
                for event in drag_events(mime, position):
                    QtCore.QCoreApplication.sendEvent(self.view.viewport(), event)
                    self.assertFalse(event.isAccepted())
                self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
                self.assertEqual(self.undo.get_size(), count)

    def test_failed_rename_does_not_enqueue_a_later_edit(self):
        source = self.widget.data_source()
        child = usd_item_id(Sdf.Path("/Root/Child"))
        index = self.find("Child")
        layer = self.stage.GetRootLayer()
        before = layer.ExportToString()
        self.assertTrue(index.flags() & QtCore.Qt.ItemIsEditable)
        layer.SetPermissionToEdit(False)
        try:
            self.assertFalse(index.flags() & QtCore.Qt.ItemIsEditable)
            self.assertFalse(source.set_column_text(child, "name", "Renamed"))
            self.assertFalse(self.undo.can_undo())
        finally:
            layer.SetPermissionToEdit(True)
        self.undo.undo()
        self.assertEqual(layer.ExportToString(), before)

    def test_rename_requires_a_spec_in_a_local_edit_target(self):
        layer = Sdf.Layer.CreateAnonymous()
        self.stage.GetRootLayer().subLayerPaths.append(layer.identifier)
        self.stage.SetEditTarget(layer)
        flush()
        child = self.find("Child")
        self.assertTrue(child.isValid())
        self.assertFalse(child.flags() & QtCore.Qt.ItemIsEditable)

        self.stage.SetEditTarget(self.stage.GetRootLayer())
        variants = self.stage.GetPrimAtPath("/Root").GetVariantSet("look")
        with variants.GetVariantEditContext():
            flush()
            self.assertFalse(self.find("red").flags() & QtCore.Qt.ItemIsEditable)
            self.assertFalse(self.find("Child").flags() & QtCore.Qt.ItemIsEditable)

    def test_usd_names_trim_whitespace_and_reject_empty_names(self):
        source = self.widget.data_source()
        child = usd_item_id(Sdf.Path("/Root/Child"))
        self.assertFalse(source.set_column_text(child, "name", " \t "))
        self.assertFalse(self.undo.can_undo())
        for name in ("Renamed", "CAF\u00c9", "\u041f\u0440\u0438\u043c", "\u6a21\u578b"):
            with self.subTest(name=name):
                if not Sdf.Path.IsValidIdentifier(name):
                    self.skipTest("This USD SDK does not support Unicode prim identifiers")
                self.undo.clear()
                path = Sdf.Path("/Root/" + name)
                self.widget.selection_context().set_selection([child])
                self.undo.clear()
                before = self.stage.GetRootLayer().ExportToString()
                self.assertTrue(source.set_column_text(child, "name", "  " + name + "  "))
                flush()
                self.assertTrue(self.stage.GetPrimAtPath(path))
                self.assertEqual(self.find(name).data(), name)
                self.assertEqual(list(self.app.get_prim_selection()), [path])
                self.undo.undo()
                flush()
                self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
                self.assertEqual(list(self.app.get_prim_selection()), [Sdf.Path("/Root/Child")])
                self.undo.redo()
                flush()
                self.assertTrue(self.stage.GetPrimAtPath(path))
                self.assertEqual(list(self.app.get_prim_selection()), [path])
                self.undo.undo()
                flush()

    def test_unicode_names_survive_search_selection_and_reparenting(self):
        destination = Sdf.Path("/Other/\u5bb9\u5668")
        if not Sdf.Path.IsValidIdentifier(destination.name):
            self.skipTest("This USD SDK does not support Unicode prim identifiers")
        self.stage.DefinePrim(destination, "Xform")
        for name, query in (
            ("CAF\u00c9", "caf\u00e9"),
            ("\u041f\u0420\u0418\u041c", "\u043f\u0440\u0438\u043c"),
            ("\u6a21\u578b", "\u6a21\u578b"),
            ("\u0645\u062c\u0633\u0645", "\u0645\u062c\u0633\u0645"),
            ("\U00010400", "\U00010428"),
        ):
            with self.subTest(name=name):
                if not Sdf.Path.IsValidIdentifier(name):
                    self.skipTest("This USD SDK does not support Unicode prim identifiers")
                path = Sdf.Path("/Root/" + name)
                self.stage.DefinePrim(path, "Xform")
                flush()
                item = usd_item_id(path)
                expected = [item]
                index = self.widget.index_for_item(item)
                self.assertEqual(usd_path_of(item), path)
                self.assertEqual(self.model.item_for_index(index), item)
                self.assertEqual(index.sibling(index.row(), self.name_column).data(), name)
                self.assertEqual(self.widget.data_source().search(query), expected)
                self.filter(query)
                self.assertTrue(self.proxy.mapFromSource(self.model.index_for_item(item)).isValid())
                self.widget.selection_context().set_selection(expected)
                self.assertEqual(list(self.app.get_prim_selection()), [path])
                mime = self.model.mimeData([self.model.index_for_item(item)])
                self.assertEqual(
                    bytes(mime.data("application/x-sdfpaths")), str(path).encode("utf-8")
                )
                self.filter("")
                before = self.stage.GetRootLayer().ExportToString()
                self.widget.data_source().reparent([item], usd_item_id(destination))
                flush()
                moved = destination.AppendChild(name)
                self.assertTrue(self.stage.GetPrimAtPath(moved))
                self.assertEqual(self.find(name).data(), name)
                self.undo.undo()
                flush()
                self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
                self.undo.redo()
                flush()
                self.assertTrue(self.stage.GetPrimAtPath(moved))
                self.undo.undo()
                flush()
