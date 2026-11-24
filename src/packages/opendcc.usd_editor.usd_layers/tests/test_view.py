# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import gc
import unittest

from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf
import opendcc.core as core
from opendcc.usd_editor.usd_layers import UsdLayersModel, UsdLayersView


class LayerViewTests(unittest.TestCase):
    def setUp(self):
        self.qt_app = QtWidgets.QApplication.instance()
        self.session = core.Application.instance().get_session()
        self.root = Sdf.Layer.CreateAnonymous("view.usda")
        self.child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [self.child.identifier]
        self.session.open_stage(self.root)
        self.parent_widget = QtWidgets.QWidget()
        self.view = UsdLayersView(self.parent_widget)
        self.view.set_current_stage()

    def tearDown(self):
        self.parent_widget.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.session.close_all()
        self.qt_app.processEvents()

    def test_bound_view_and_selection_signals(self):
        self.assertIsInstance(self.view, QtWidgets.QTreeView)
        self.assertIs(self.view.parentWidget(), self.parent_widget)
        self.assertIsInstance(self.view.layer_model(), UsdLayersModel)
        model = self.view.layer_model()
        model.setHeaderData(0, QtCore.Qt.Horizontal, "View Layers", QtCore.Qt.DisplayRole)
        del model
        gc.collect()
        self.assertEqual(self.view.layer_model().headerData(0, QtCore.Qt.Horizontal), "View Layers")
        selected = []
        self.view.layers_selected.connect(lambda identifiers: selected.append(list(identifiers)))
        self.view.select_layer_identifiers([self.child.identifier])
        self.assertEqual(selected, [])
        self.assertEqual(self.view.selectionModel().selectedRows()[0].data(), "child.usda")
        self.view.selectionModel().clearSelection()
        self.assertEqual(selected, [[]])
        root = self.view.model().index(1, 0)
        self.view.selectionModel().select(
            root, QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows
        )
        self.assertEqual(selected[-1], [self.root.identifier])

    def test_model_replacement_filtering_and_refresh(self):
        model = UsdLayersModel(self.parent_widget)
        self.view.set_layer_model(model)
        self.view.set_current_stage()
        self.assertIs(self.view.layer_model(), model)
        self.view.set_filter_text("child")
        root = self.view.model().index(0, 0)
        self.assertEqual(self.view.model().rowCount(root), 1)
        self.view.set_filter_text("missing")
        self.assertEqual(self.view.model().rowCount(), 0)
        self.view.set_filter_text("")
        self.view.set_show_only_empty(True)
        self.assertEqual(self.view.model().rowCount(), 2)
        self.view.set_show_only_empty(False)
        self.view.setColumnWidth(0, 315)
        self.view.setColumnHidden(int(UsdLayersModel.VersionColumn), True)
        self.view.refresh()
        self.assertEqual(self.view.columnWidth(0), 315)
        self.assertTrue(self.view.isColumnHidden(int(UsdLayersModel.VersionColumn)))

    def test_empty_filter_updates_after_content_edit(self):
        self.view.set_show_only_empty(True)
        proxy = self.view.model()
        root = proxy.index(1, 0)
        self.assertEqual(proxy.rowCount(root), 1)

        Sdf.CreatePrimInLayer(self.child, "/Prim")
        self.qt_app.processEvents()
        self.assertEqual(proxy.rowCount(root), 0)

    def test_python_view_subclass_rendering_and_context_menu(self):
        class PackageView(UsdLayersView):
            def __init__(self, parent):
                super(PackageView, self).__init__(parent)
                self.painted_rows = 0

            def drawRow(self, painter, option, index):
                self.painted_rows += 1
                super(PackageView, self).drawRow(painter, option, index)

        view = PackageView(self.parent_widget)
        view.set_current_stage()
        view.resize(700, 240)
        self.parent_widget.resize(700, 240)
        self.parent_widget.show()
        view.show()
        self.qt_app.processEvents()
        self.assertFalse(view.grab().isNull())
        self.assertGreater(view.painted_rows, 0)
        received = []

        def build_menu(menu, identifier, parent_identifier):
            received.append((identifier, parent_identifier))
            menu.addAction("Package action")
            QtCore.QTimer.singleShot(0, menu.close)

        view.context_menu_requested.connect(build_menu)
        child = view.model().index(0, 0, view.model().index(1, 0))
        position = view.visualRect(child).center()
        event = QtGui.QContextMenuEvent(
            QtGui.QContextMenuEvent.Mouse, position, view.viewport().mapToGlobal(position)
        )
        self.qt_app.sendEvent(view.viewport(), event)
        self.assertEqual(received, [(self.child.identifier, self.root.identifier)])

    def test_parent_destruction_invalidates_python_wrapper(self):
        self.parent_widget.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        with self.assertRaises(RuntimeError):
            self.view.model()
        self.parent_widget = QtWidgets.QWidget()

    def test_cleared_model_is_safe(self):
        proxy = self.view.model()
        self.view.setModel(None)
        self.view.set_current_stage()
        self.view.refresh()
        self.view.add_sublayer()
        self.view.set_filter_text("missing")
        self.view.set_show_references(False)
        self.view.set_show_only_dirty(True)
        self.view.set_show_only_anonymous(True)
        self.view.set_show_only_empty(True)
        self.view.set_layer_model(UsdLayersModel(self.parent_widget))
        self.assertIsNone(self.view.layer_model())
        self.view.setModel(proxy)
        self.assertIsNotNone(self.view.layer_model())
