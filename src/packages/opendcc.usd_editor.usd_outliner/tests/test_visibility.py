# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore, QtGui
from pxr import Sdf, Usd, UsdGeom
from opendcc.usd_editor.usd_outliner import usd_item_id
from outliner_test_utils import OutlinerTest, flush


class TestVisibility(OutlinerTest):
    def test_animated_visibility_toggle_changes_current_frame_and_is_undoable(self):
        previous_time = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous_time)
        visibility = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root/Child")).GetVisibilityAttr()
        visibility.Set("inherited", Usd.TimeCode(1))
        visibility.Set("invisible", Usd.TimeCode(2))
        self.app.set_current_time(2)
        flush()
        source = self.widget.data_source()
        child = usd_item_id(Sdf.Path("/Root/Child"))
        before = self.stage.GetRootLayer().ExportToString()
        self.assertFalse(source.is_visible(child))
        source.toggle_column(child, "visibility")
        self.assertTrue(source.is_visible(child))
        self.assertEqual(visibility.Get(Usd.TimeCode(1)), "inherited")
        self.undo.undo()
        self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
        self.assertFalse(source.is_visible(child))
        self.undo.redo()
        self.assertTrue(source.is_visible(child))

    def test_static_visibility_does_not_refresh_rows_when_time_changes(self):
        previous_time = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous_time)
        index = self.proxy.mapToSource(self.find("Child"))
        cell = index.sibling(index.row(), self.column("Visibility"))
        self.assertFalse(cell.data(QtCore.Qt.DecorationRole).isNull())
        flush()
        changes, layouts = [], []
        self.model.dataChanged.connect(lambda *args: changes.append(True))
        self.model.layoutChanged.connect(lambda *args: layouts.append(True))
        self.app.set_current_time(previous_time + 1)
        flush()
        self.assertFalse(changes)
        self.assertFalse(layouts)

    def test_visibility_icon_follows_timeline_animation_without_edits_or_reset(self):

        previous_time = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous_time)
        visibility = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root/Child")).GetVisibilityAttr()
        for frame, value in ((1, "inherited"), (2, "invisible"), (3, "inherited")):
            visibility.Set(value, Usd.TimeCode(frame))
        flush()
        self.filter("Child")
        index = self.proxy.mapToSource(self.find("Child"))
        cell = index.sibling(index.row(), self.column("Visibility"))
        persistent = QtCore.QPersistentModelIndex(cell)
        self.assertFalse(cell.data(QtCore.Qt.DecorationRole).isNull())
        resets, layouts, changes = [], [], []
        self.model.modelReset.connect(lambda: resets.append(True))
        self.model.layoutChanged.connect(lambda *args: layouts.append(True))
        self.model.dataChanged.connect(lambda first, last, *args: changes.append((first, last)))
        search_changes = []
        self.model.source_changed.connect(search_changes.append)
        before = self.stage.GetRootLayer().ExportToString()
        count = self.undo.get_size()
        for frame, icon in ((1, "eye_open"), (2, "eye_closed"), (3, "eye_open"), (2, "eye_closed")):
            self.app.set_current_time(frame)
            flush()
            self.assertEqual(
                cell.data(QtCore.Qt.DecorationRole).pixmap(16, 16).toImage(),
                QtGui.QIcon(":/icons/" + icon).pixmap(16, 16).toImage(),
            )
        self.assertTrue(changes)
        self.assertTrue(search_changes)
        self.assertFalse(any(search_changes))
        self.assertTrue(
            all(
                first.parent() == cell.parent() and first.row() == cell.row() == last.row()
                for first, last in changes
            )
        )
        self.assertFalse(resets)
        self.assertFalse(layouts)
        self.assertTrue(persistent.isValid())
        self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
        self.assertEqual(self.undo.get_size(), count)

    def test_animated_parent_visibility_and_external_edits_refresh_child_icons(self):

        previous_time = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous_time)
        visibility = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root")).GetVisibilityAttr()
        visibility.Set("inherited", Usd.TimeCode(1))
        visibility.Set("invisible", Usd.TimeCode(2))
        flush()
        index = self.proxy.mapToSource(self.find("Child"))
        cell = index.sibling(index.row(), self.column("Visibility"))
        self.app.set_current_time(2)
        flush()
        self.assertEqual(
            cell.data(QtCore.Qt.DecorationRole).pixmap(16, 16).toImage(),
            QtGui.QIcon(":/icons/eye_open_faded").pixmap(16, 16).toImage(),
        )
        changes = []
        self.model.dataChanged.connect(lambda first, last, *args: changes.append((first, last)))
        visibility.Set("inherited", Usd.TimeCode(2))
        flush()
        self.assertEqual(
            cell.data(QtCore.Qt.DecorationRole).pixmap(16, 16).toImage(),
            QtGui.QIcon(":/icons/eye_open").pixmap(16, 16).toImage(),
        )
        self.assertTrue(
            any(
                first.parent() == cell.parent()
                and first.row() <= cell.row() <= last.row()
                and first.column() <= cell.column() <= last.column()
                for first, last in changes
            )
        )

    def test_visibility_property_resync_refreshes_columns_without_rebuilding_hierarchy(self):

        prim = self.stage.GetPrimAtPath("/Root")
        prim.RemoveProperty("visibility")
        flush()
        index = self.proxy.mapToSource(self.find("Child"))
        cell = index.sibling(index.row(), self.column("Visibility"))
        layouts, resets = [], []
        self.model.layoutChanged.connect(lambda *args: layouts.append(True))
        self.model.modelReset.connect(lambda: resets.append(True))
        visibility = UsdGeom.Imageable(prim).CreateVisibilityAttr("invisible")
        flush()
        self.assertEqual(
            cell.data(QtCore.Qt.DecorationRole).pixmap(16, 16).toImage(),
            QtGui.QIcon(":/icons/eye_open_faded").pixmap(16, 16).toImage(),
        )
        prim.RemoveProperty(visibility.GetName())
        flush()
        self.assertEqual(
            cell.data(QtCore.Qt.DecorationRole).pixmap(16, 16).toImage(),
            QtGui.QIcon(":/icons/eye_open").pixmap(16, 16).toImage(),
        )
        self.assertFalse(layouts)
        self.assertFalse(resets)

    def test_inherited_visibility_crosses_non_imageable_parents(self):
        previous_time = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous_time)
        child = UsdGeom.Xform.Define(self.stage, "/Root/Untyped/Child")
        visibility = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root")).GetVisibilityAttr()
        visibility.Set("inherited", Usd.TimeCode(1))
        visibility.Set("invisible", Usd.TimeCode(2))
        flush()
        source = self.widget.data_source()
        item = usd_item_id(child.GetPath())
        cell = self.model.ensure_index_for_item(item).siblingAtColumn(self.column("Visibility"))
        before = self.stage.GetRootLayer().ExportToString()
        source.is_visible(item)
        for frame, enabled in ((1, True), (2, False), (1, True)):
            self.app.set_current_time(frame)
            flush()
            self.assertEqual(source.toggle_enabled(item, "visibility"), enabled)
            expected_icon = QtGui.QIcon(":/icons/eye_open" + ("" if enabled else "_faded"))
            self.assertEqual(
                cell.data(QtCore.Qt.DecorationRole).pixmap(16, 16).toImage(),
                expected_icon.pixmap(16, 16).toImage(),
            )
            if not enabled:
                self.model.toggle_column(cell)
                self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
                self.assertFalse(self.undo.can_undo())
        untyped = usd_item_id(Sdf.Path("/Root/Untyped"))
        self.assertFalse(source.toggle_enabled(untyped, "visibility"))
