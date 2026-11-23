# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore
from pxr import Sdf
from opendcc.ui.scene_outliner import Change
from opendcc.usd_editor.usd_outliner import UsdStageDataSource, UsdSelectionContext, usd_item_id
from outliner_test_utils import OutlinerTest, flush


class TestUsdExtensions(OutlinerTest):
    def test_unloading_outliner_preserves_other_panel_registrations(self):
        import opendcc.core as core
        from opendcc.usd_editor.usd_outliner import UsdOutlinerEntryPoint

        registry = core.PanelFactory.instance()
        before = set(registry.get_registered_panels())
        self.assertIn("outliner", before)
        entry = UsdOutlinerEntryPoint()
        try:
            entry.uninitialize(None)
            self.assertEqual(set(registry.get_registered_panels()), before - {"outliner"})
        finally:
            entry.initialize(None)

    def test_adapter_getters_retain_adapters_after_widget_deletion(self):
        from opendcc.usd_editor.usd_outliner import UsdOutliner

        for through_model in (False, True):
            with self.subTest(through_model=through_model):
                panel = UsdOutliner()
                self.addCleanup(panel.deleteLater)
                widget = panel.outliner
                source = widget.model().data_source() if through_model else widget.data_source()
                selection = widget.selection_context()
                widget.deleteLater()
                QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
                flush()
                self.assertEqual(source.stage, self.stage)
                self.assertEqual(source.roots(), [usd_item_id(Sdf.Path("/"))])
                selection.set_selection([usd_item_id(Sdf.Path("/Other"))])
                self.assertEqual(selection.selected_items(), [usd_item_id(Sdf.Path("/Other"))])

    def test_selection_callbacks_may_destroy_the_adapter_or_its_parent(self):
        other = usd_item_id(Sdf.Path("/Other"))
        root = usd_item_id(Sdf.Path("/Root"))
        for operation, items, expected in (
            ("set_selection", [root], [Sdf.Path("/Root")]),
            ("add_to_selection", [root], [Sdf.Path("/Other"), Sdf.Path("/Root")]),
            ("remove_from_selection", [other], []),
        ):
            for delete_parent in (False, True):
                with self.subTest(operation=operation, delete_parent=delete_parent):
                    owner = QtCore.QObject()
                    selection = UsdSelectionContext(owner if delete_parent else None)
                    selection.set_selection([other])
                    calls = []

                    def destroy():
                        calls.append(True)
                        (owner if delete_parent else selection).deleteLater()
                        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

                    selection.changed.connect(destroy)
                    getattr(selection, operation)(items)
                    self.assertEqual(calls, [True])
                    self.assertEqual(set(self.app.get_prim_selection()), set(expected))
                    with self.assertRaises(RuntimeError):
                        selection.selected_items()

    def test_python_can_swap_a_native_usd_backend(self):
        original = self.widget.data_source()
        replacement = UsdStageDataSource()
        self.widget.set_data_source(replacement)
        self.assertIs(self.widget.data_source(), replacement)
        child = usd_item_id(Sdf.Path("/Root/Child"))
        self.assertTrue(self.widget.index_for_item(child).isValid())
        original.notify(Change.reset())
        self.assertIs(self.widget.data_source(), replacement)
        self.assertTrue(self.widget.index_for_item(child).isValid())
        self.widget.set_data_source(original)
        self.assertTrue(self.widget.index_for_item(usd_item_id(Sdf.Path("/Root/Child"))).isValid())
