# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from Qt import QtCore
from pxr import Sdf
import opendcc.core as core
from opendcc.usd_editor.usd_layers import UsdLayersModel


class LayerModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.qt_app = QtCore.QCoreApplication.instance() or QtCore.QCoreApplication([])

    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.root = Sdf.Layer.CreateAnonymous("model.usda")
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.model = UsdLayersModel()
        self.model.set_current_stage()

    def tearDown(self):
        self.model.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.session.close_all()
        self.qt_app.processEvents()

    def test_indexes_and_parent_relationships(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        self.model.refresh()
        self.qt_app.processEvents()
        root = self.model.index(1, int(UsdLayersModel.NameColumn))
        index = self.model.index(0, 0, root)
        self.assertEqual(self.model.rowCount(), 2)
        self.assertEqual(index.parent(), root)
        self.assertEqual(index.data(UsdLayersModel.LayerIdentifierRole), child.identifier)
        self.assertEqual(self.model.rowCount(root.sibling(root.row(), 1)), 0)
        for row, column, parent in (
            (-1, 0, QtCore.QModelIndex()),
            (2, 0, QtCore.QModelIndex()),
            (0, -1, root),
            (0, int(UsdLayersModel.ColumnCount), root),
            (0, 0, root.sibling(root.row(), 1)),
        ):
            self.assertFalse(self.model.index(row, column, parent).isValid())
        self.assertFalse(self.model.parent(QtCore.QModelIndex()).isValid())

    def test_unnamed_anonymous_layers_have_readable_labels(self):
        root = Sdf.Layer.CreateAnonymous(".usda")
        child = Sdf.Layer.CreateAnonymous("lighting.usda")
        root.subLayerPaths = [child.identifier]
        self.session.open_stage(root)
        self.model.set_current_stage()

        session_index = self.model.index(0, 0)
        root_index = self.model.index(1, 0)
        self.assertEqual(session_index.data(), "Session Layer")
        self.assertEqual(root_index.data(), "Anonymous Layer")
        self.assertEqual(root_index.data(UsdLayersModel.LayerIdentifierRole), root.identifier)
        self.assertEqual(self.model.index(0, 0, root_index).data(), "lighting.usda")

    def test_header_labels_are_supplied_by_the_caller(self):
        self.assertIsNone(self.model.headerData(0, QtCore.Qt.Horizontal))
        changed = []
        self.model.headerDataChanged.connect(lambda *args: changed.append(args))
        self.assertTrue(
            self.model.setHeaderData(
                0, QtCore.Qt.Horizontal, "Package Layer", QtCore.Qt.DisplayRole
            )
        )
        self.assertEqual(self.model.headerData(0, QtCore.Qt.Horizontal), "Package Layer")
        self.assertEqual(len(changed), 1)
        self.model.refresh()
        self.assertEqual(self.model.headerData(0, QtCore.Qt.Horizontal), "Package Layer")
        self.assertFalse(
            self.model.setHeaderData(-1, QtCore.Qt.Horizontal, "Invalid", QtCore.Qt.DisplayRole)
        )
        self.assertFalse(
            self.model.setHeaderData(0, QtCore.Qt.Vertical, "Invalid", QtCore.Qt.DisplayRole)
        )
        self.assertEqual(len(changed), 1)

    def test_layer_roles_and_notice_updates_without_ui(self):
        self.assertFalse(self.app.is_ui_available())
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        self.model.refresh()
        self.qt_app.processEvents()
        root = self.model.index(1, 0)
        index = self.model.index(0, 0, root)
        changed = []
        self.model.dataChanged.connect(lambda *args: changed.append(args))
        self.stage.SetEditTarget(child)
        self.qt_app.processEvents()
        self.assertTrue(index.data(UsdLayersModel.LayerIsEditTargetRole))
        self.assertTrue(root.data(UsdLayersModel.LayerIsEditTargetParentRole))
        self.assertTrue(root.data(UsdLayersModel.LayerIsRootRole))
        self.assertFalse(index.data(UsdLayersModel.LayerIsRootRole))
        self.assertTrue(index.data(UsdLayersModel.LayerIsDirtyRole))
        self.assertIsNone(index.data(QtCore.Qt.DecorationRole))
        self.assertIsNone(index.data(QtCore.Qt.BackgroundRole))
        self.assertTrue(changed)
        self.stage.SetEditTarget(self.root)
        self.stage.MuteLayer(child.identifier)
        self.qt_app.processEvents()
        self.assertTrue(index.data(UsdLayersModel.LayerIsMutedRole))

    def test_shared_layers_and_cycles(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        shared = Sdf.Layer.CreateAnonymous("shared.usda")
        self.root.subLayerPaths = [first.identifier, second.identifier]
        first.subLayerPaths = [shared.identifier]
        second.subLayerPaths = [shared.identifier]
        self.model.refresh()
        self.qt_app.processEvents()
        root = self.model.index(1, 0)
        parents = [self.model.index(i, 0, root) for i in range(2)]
        self.assertTrue(all(self.model.rowCount(index) == 1 for index in parents))
        self.stage.SetEditTarget(shared)
        self.qt_app.processEvents()
        self.assertTrue(
            all(index.data(UsdLayersModel.LayerIsEditTargetParentRole) for index in parents)
        )
        self.stage.SetEditTarget(self.root)
        shared.subLayerPaths = [self.root.identifier]
        try:
            self.model.refresh()
            root = self.model.index(1, 0)
            for row in range(2):
                parent = self.model.index(row, 0, root)
                self.assertEqual(self.model.rowCount(self.model.index(0, 0, parent)), 0)
        finally:
            shared.subLayerPaths = []

    def test_structural_notice_and_stage_close(self):
        resets = []
        self.model.modelReset.connect(lambda: resets.append(True))
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        self.qt_app.processEvents()
        self.assertEqual(self.model.rowCount(self.model.index(1, 0)), 1)
        self.assertTrue(resets)
        self.session.close_all()
        self.model.set_current_stage()
        self.assertEqual(self.model.rowCount(), 0)
        self.assertFalse(self.model.is_model_valid())

    def test_reference_edits_update_tree_without_refresh(self):
        reference = Sdf.Layer.CreateAnonymous("reference.usda")
        Sdf.CreatePrimInLayer(reference, "/Asset")
        prim = self.stage.DefinePrim("/Instance")
        resets = []
        self.model.modelReset.connect(lambda: resets.append(True))

        def reference_identifiers():
            root = self.model.index(1, 0)
            return [
                self.model.index(row, 0, root).data(UsdLayersModel.LayerIdentifierRole)
                for row in range(self.model.rowCount(root))
            ]

        self.assertTrue(prim.GetReferences().AddReference(reference.identifier, "/Asset"))
        self.qt_app.processEvents()
        self.assertEqual(reference_identifiers(), [reference.identifier])
        self.assertEqual(len(resets), 1)

        self.assertTrue(prim.GetReferences().ClearReferences())
        self.qt_app.processEvents()
        self.assertEqual(reference_identifiers(), [])
        self.assertEqual(len(resets), 2)

        self.assertTrue(prim.GetReferences().AddReference(reference.identifier, "/Asset"))
        self.qt_app.processEvents()
        self.assertTrue(self.stage.RemovePrim("/Instance"))
        self.qt_app.processEvents()
        self.assertEqual(reference_identifiers(), [])
        self.assertEqual(len(resets), 4)

    def test_content_notices_only_signal_changed_layer_state(self):
        with tempfile.TemporaryDirectory() as directory:
            layer = Sdf.Layer.CreateNew(str(Path(directory) / "tracked.usda"))
            sibling = Sdf.Layer.CreateAnonymous("sibling.usda")
            self.root.subLayerPaths = [layer.identifier, sibling.identifier]
            self.qt_app.processEvents()
            root = self.model.index(1, 0)
            tracked = self.model.index(0, 0, root)
            self.assertTrue(tracked.data(UsdLayersModel.LayerIsEmptyRole))
            self.assertFalse(tracked.data(UsdLayersModel.LayerIsDirtyRole))

            changes = []
            self.model.dataChanged.connect(
                lambda first, last, roles: changes.append((first, last, roles))
            )
            resets = []
            self.model.modelReset.connect(lambda: resets.append(True))

            def edit_layer():
                for i in range(25):
                    Sdf.CreatePrimInLayer(layer, "/Prim{}".format(i))

            with ThreadPoolExecutor(max_workers=1) as pool:
                pool.submit(edit_layer).result()
            self.assertEqual(changes, [])
            self.qt_app.processEvents()
            self.assertEqual(len(changes), 1)
            first, last, roles = changes.pop()
            self.assertEqual(first, tracked)
            self.assertEqual(last.row(), tracked.row())
            self.assertEqual(last.parent(), tracked.parent())
            self.assertEqual(
                set(roles),
                {UsdLayersModel.LayerIsDirtyRole, UsdLayersModel.LayerIsEmptyRole},
            )

            Sdf.CreatePrimInLayer(layer, "/AnotherPrim")
            self.qt_app.processEvents()
            self.assertEqual(changes, [])
            self.assertTrue(layer.Save())
            self.qt_app.processEvents()
            self.assertEqual(
                [(first, roles) for first, _, roles in changes],
                [(tracked, [UsdLayersModel.LayerIsDirtyRole])],
            )
            self.assertEqual(resets, [])

    def test_stage_switch_discards_pending_notices(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        replacement = Sdf.Layer.CreateAnonymous("replacement.usda")
        self.session.open_stage(replacement)
        self.model.set_current_stage()
        resets = []
        self.model.modelReset.connect(lambda: resets.append(True))
        self.qt_app.processEvents()
        self.assertEqual(self.model.index(1, 0).data(), replacement.GetDisplayName())
        self.assertEqual(resets, [])

    def test_referenced_layer_tree(self):
        fixture = Path(__file__).parent / "fixtures" / "layers.usda"
        self.root = Sdf.Layer.FindOrOpen(str(fixture))
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.model.set_current_stage()
        reference = Sdf.Layer.FindRelativeToLayer(self.root, "prop.usda")
        root_index = self.model.index(1, 0)
        index = next(
            self.model.index(row, 0, root_index)
            for row in range(self.model.rowCount(root_index))
            if self.model.index(row, 0, root_index).data(UsdLayersModel.LayerIdentifierRole)
            == reference.identifier
        )
        self.assertTrue(index.isValid())
        self.assertEqual(index.data(), "prop.usda")
        self.assertTrue(index.data(UsdLayersModel.LayerIsReferenceRole))
        self.assertEqual(index.parent(), root_index)
        self.assertEqual(self.model.rowCount(index), 1)
        child = self.model.index(0, 0, index)
        self.assertEqual(child.data(), "prop_geometry.usda")
        self.assertFalse(child.data(UsdLayersModel.LayerIsReferenceRole))
        self.assertFalse(self.stage.HasLocalLayer(reference))
        self.assertTrue(self.stage.GetPrimAtPath("/World/Prop/Geometry"))
