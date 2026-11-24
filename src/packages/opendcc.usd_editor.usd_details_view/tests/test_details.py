# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import unittest

from Qt import QtCore, QtWidgets
from pxr import Sdf, Usd, UsdGeom, Vt
import opendcc.core as core
from opendcc.usd_ui_utils import add_layer_copy_actions
from opendcc.usd_editor.usd_details_view.usd_details_view import (
    GetRootLayerStackInfo,
    LayerStackViewColumnIndex,
    UsdDetailsView,
)


class DetailsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.qt_app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.root = Sdf.Layer.CreateAnonymous("details_test.usda")
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.prim = self.stage.DefinePrim("/World", "Xform")
        self.app.set_prim_selection([self.prim.GetPath()])
        self.panel = UsdDetailsView()

    def tearDown(self):
        self.panel.close()
        self.panel.deleteLater()
        self.qt_app.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.app.get_undo_stack().clear()
        self.app.set_layer_selection(set())
        self.session.close_stage(self.stage)

    def metadata(self):
        table = self.panel._metadata_view
        return {
            table.item(row, 0).text(): table.item(row, 1).text()
            for row in range(table.rowCount())
            if table.item(row, 1) is not None
        }

    def test_session_muted_and_shared_layers(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        left = Sdf.Layer.CreateAnonymous("left.usda")
        right = Sdf.Layer.CreateAnonymous("right.usda")
        left.subLayerPaths = [child.identifier]
        right.subLayerPaths = [child.identifier]
        self.root.subLayerPaths = [left.identifier, right.identifier]
        layers = GetRootLayerStackInfo(self.stage)
        identifiers = [info.GetIdentifier() for info in layers]
        self.assertIn(self.stage.GetSessionLayer().identifier, identifiers)
        self.assertEqual(identifiers.count(child.identifier), 2)
        self.stage.MuteLayer(left.identifier)
        layers = GetRootLayerStackInfo(self.stage)
        muted = next(info for info in layers if info.GetIdentifier() == left.identifier)
        self.assertTrue(muted.IsMuted())
        self.assertEqual(sum(info.GetIdentifier() == child.identifier for info in layers), 1)

    def test_composed_offsets(self):
        child = Sdf.Layer.CreateAnonymous("offset.usda")
        child_stage = Usd.Stage.Open(child)
        child_stage.DefinePrim("/World")
        self.root.subLayerPaths = [child.identifier]
        self.root.subLayerOffsets[0] = Sdf.LayerOffset(12, 2)
        self.panel.update_data()
        table = self.panel._layer_stack
        row = next(
            row
            for row in range(table.rowCount())
            if table.item(row, 0).identifier == child.identifier
        )
        self.assertEqual(table.item(row, LayerStackViewColumnIndex.OFFSET).text(), "12.0 , 2.0")
        self.assertFalse(table.isColumnHidden(LayerStackViewColumnIndex.OFFSET))

    def test_cycle_does_not_hide_other_sublayers(self):
        parent = Sdf.Layer.CreateAnonymous("parent.usda")
        child = Sdf.Layer.CreateAnonymous("child.usda")
        parent.subLayerPaths = [self.root.identifier, child.identifier]
        self.root.subLayerPaths = [parent.identifier]
        identifiers = [info.GetIdentifier() for info in GetRootLayerStackInfo(self.stage)]
        self.assertEqual(identifiers.count(self.root.identifier), 1)
        self.assertEqual(identifiers.count(child.identifier), 1)

    def test_metadata_inherits_and_applied_schemas(self):
        self.stage.CreateClassPrim("/Class")
        self.prim.GetInherits().AddInherit("/Class")
        UsdGeom.ModelAPI.Apply(self.prim)
        self.panel.update_data()
        values = self.metadata()
        self.assertIn("/Class", values["inherits"])
        self.assertIn("GeomModelAPI", values["[applied API schemas]"])

    def test_animated_array_and_relationship(self):
        attr = self.prim.CreateAttribute("samples", Sdf.ValueTypeNames.DoubleArray)
        attr.Set(Vt.DoubleArray([1, 2]), 1)
        attr.Set(Vt.DoubleArray([3, 4]), 2)
        selection = core.SelectionList()
        selection.add_properties(self.prim.GetPath(), ["samples"])
        self.app.set_selection(selection)
        column = LayerStackViewColumnIndex.VALUE
        self.assertEqual(self.panel._layer_stack.item(0, column).text(), "2 time samples")
        relation = self.prim.CreateRelationship("target")
        relation.AddTarget("/Target")
        selection = core.SelectionList()
        selection.add_properties(self.prim.GetPath(), ["target"])
        self.app.set_selection(selection)
        self.assertIn("/Target", self.panel._layer_stack.item(0, column).text())

    def test_variant_undo_and_refresh_preserves_selection(self):
        variants = self.prim.GetVariantSets().AddVariantSet("look")
        for name in ("red", "blue"):
            variants.AddVariant(name)
        variants.SetVariantSelection("red")
        self.panel.update_data()
        table = self.panel._metadata_view
        combo = next(
            table.cellWidget(row, 1)
            for row in range(table.rowCount())
            if isinstance(table.cellWidget(row, 1), QtWidgets.QComboBox)
        )
        stack = self.app.get_undo_stack()
        stack.set_enabled(True)
        stack.clear()
        self.app.set_layer_selection({self.root})
        combo.setCurrentIndex(combo.findText("blue"))
        self.assertEqual(variants.GetVariantSelection(), "blue")
        stack.undo()
        self.assertEqual(variants.GetVariantSelection(), "red")
        self.qt_app.processEvents()
        self.assertEqual(self.app.get_layer_selection(), {self.root})
        self.assertEqual(self.panel._get_selected_layers(), {self.root})

    def test_close_releases_callbacks_and_notices(self):
        self.panel.close()
        self.assertIsNone(self.panel.selection_callback_id)
        self.assertIsNone(self.panel._listener_obj_changed)
        self.assertIsNone(self.panel._composition._listener_obj_chgd)

    def test_layer_copy_actions(self):
        menu = QtWidgets.QMenu()
        add_layer_copy_actions(menu, self.root)
        identifier, resolved_path = menu.actions()
        identifier.trigger()
        self.assertEqual(self.qt_app.clipboard().text(), self.root.identifier)
        self.assertFalse(resolved_path.isEnabled())

    def test_parent_destruction_releases_callbacks(self):
        parent = QtWidgets.QWidget()
        self.panel.setParent(parent)
        parent.deleteLater()
        self.qt_app.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.assertIsNone(self.panel.selection_callback_id)
        self.assertIsNone(self.panel._listener_obj_changed)
        self.assertIsNone(self.panel._composition._listener_obj_chgd)
        self.panel = UsdDetailsView()
