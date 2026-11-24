# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import tempfile
import unittest
from pathlib import Path

from Qt import QtCore, QtWidgets
from pxr import Sdf
from opendcc import cmds
import opendcc.core as core
from opendcc.usd_editor.usd_layers import UsdLayersModel, UsdLayersPanel
from image_utils import ImageDiffingTestCase


class LayerRenderingTests(ImageDiffingTestCase):
    @classmethod
    def setUpClass(cls):
        cls.qt_app = QtWidgets.QApplication.instance()
        stylesheet = QtCore.QFile(":/stylesheets/application_stylesheet.qss")
        if not stylesheet.open(QtCore.QIODevice.ReadOnly):
            raise RuntimeError("OpenDCC application stylesheet resource is missing")
        expected_stylesheet = bytes(stylesheet.readAll()).decode("utf-8")
        stylesheet.close()
        if not expected_stylesheet or cls.qt_app.styleSheet() != expected_stylesheet:
            raise RuntimeError("OpenDCC dark application stylesheet was not loaded")

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        fixture_dir = Path(__file__).parent / "fixtures"
        for path in fixture_dir.glob("*.usda"):
            shutil.copyfile(path, Path(self.directory.name) / path.name)
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.root = Sdf.Layer.FindOrOpen(str(Path(self.directory.name) / "layers.usda"))
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.stack = self.app.get_undo_stack()
        self.stack.set_enabled(True)
        self.stack.clear()
        self.app.set_layer_selection(set())
        self.panel = UsdLayersPanel()
        self.panel.setFixedSize(760, 330)
        self.panel.show()
        self.panel.view.expandAll()
        self.panel.clearFocus()
        self.qt_app.processEvents()

    def tearDown(self):
        self.panel.close()
        self.panel.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.stack.clear()
        self.session.close_all()
        self.qt_app.processEvents()
        self.root = None
        self.stage = None

    def assert_panel_matches(self, name):
        self.qt_app.processEvents()
        first = self.panel.grab().toImage()
        self.qt_app.processEvents()
        image = self.panel.grab().toImage()
        self.assertFalse(image.isNull())
        self.assertEqual(image, first, "Panel rendering did not settle")
        self.assertEqual(image.devicePixelRatio(), 1)
        actual = Path(name + ".png")
        self.assertTrue(image.save(str(actual)))
        reference = Path(__file__).parent / "baseline" / actual.name
        if os.environ.get("OPENDCC_UPDATE_LAYER_BASELINES") == "1":
            reference.parent.mkdir(parents=True, exist_ok=True)
            self.assertTrue(image.save(str(reference)))
        self.assertTrue(reference.is_file(), "Missing reference image: " + str(reference))
        self.assert_images_close(reference, actual, max_mean_error=1e-5)

    def test_default_panel(self):
        self.assert_panel_matches("default")

    def test_inline_layer_text_preview(self):
        layer = Sdf.Layer.CreateAnonymous("lighting-preview.usda")
        layer.ImportFromString('#usda 1.0\n\ndef Xform "Lighting" {}\n')
        self.root.subLayerPaths.append(layer.identifier)
        self.qt_app.processEvents()
        self.panel.view.expandAll()
        self.app.set_layer_selection({layer})
        self.panel._preview_toggle.setChecked(True)
        self.assert_panel_matches("inline-layer-text")

    def test_edit_target_muted_and_dirty(self):
        shared = Sdf.Layer.FindRelativeToLayer(self.root, "shared.usda")
        muted = Sdf.Layer.FindRelativeToLayer(self.root, "muted.usda")
        self.assertTrue(cmds.set_edit_target(shared.identifier).is_successful())
        self.assertTrue(cmds.mute_layer(muted.identifier, mute=True).is_successful())
        text = shared.ExportToString() + '\ndef Xform "Edited" {}\n'
        self.assertTrue(cmds.apply_layer_text(shared.identifier, text).is_successful())
        self.panel.view.expandAll()
        self.assert_panel_matches("edit-target-muted-dirty")

    def test_filtered_panel(self):
        self.panel._search_toggle.setChecked(True)
        self.panel.search.setText("shared")
        self.panel.search.clearFocus()
        self.panel.view.expandAll()
        self.assert_panel_matches("filtered")

    def test_references_toggle(self):
        model = self.panel.view.layer_model()
        reference = Sdf.Layer.FindRelativeToLayer(self.root, "prop.usda")
        root_index = model.index(1, 0)
        index = next(
            model.index(row, 0, root_index)
            for row in range(model.rowCount(root_index))
            if model.index(row, 0, root_index).data(UsdLayersModel.LayerIdentifierRole)
            == reference.identifier
        )
        self.assertTrue(index.isValid())
        self.assertTrue(index.data(UsdLayersModel.LayerIsReferenceRole))
        self.assertFalse(self.stage.HasLocalLayer(reference))
        self.assertTrue(self.stage.GetPrimAtPath("/World/Prop/Geometry"))
        proxy = self.panel.view.model()
        self.assertTrue(proxy.mapFromSource(index).isValid())
        toggle = next(
            checkbox
            for checkbox in self.panel.findChildren(QtWidgets.QCheckBox)
            if checkbox.text() == "References"
        )
        toggle.setChecked(False)
        self.assertFalse(proxy.mapFromSource(index).isValid())
        self.assertFalse(proxy.mapFromSource(model.index(0, 0, index)).isValid())
        self.assert_panel_matches("references-hidden")
        self.panel._search_toggle.setChecked(True)
        self.panel.search.setText("prop_geometry")
        self.assertEqual(proxy.rowCount(), 0)
        self.panel._search_toggle.setChecked(False)
        toggle.setChecked(True)
        self.assertTrue(proxy.mapFromSource(index).isValid())
        self.panel.view.expandAll()
        self.assertTrue(proxy.mapFromSource(model.index(0, 0, index)).isValid())
