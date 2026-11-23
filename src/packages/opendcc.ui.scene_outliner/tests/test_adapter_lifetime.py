# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
import gc
import weakref
from Qt import QtCore, QtWidgets
from opendcc.ui.scene_outliner import Change, Filter, SceneOutliner, SceneOutlinerModel
from scene_outliner_test_utils import (
    Source,
    LocalSelection,
    item,
    flush,
    wait_for,
    SceneOutlinerTest,
    shiboken,
)


class TestAdapterLifetime(SceneOutlinerTest):
    def test_provider_destruction_during_widget_teardown_does_not_reenter_the_widget(self):
        class FilteredSource(Source):
            def filters(self):
                category = Filter()
                category.id = "blocked"
                category.shown_by_default = False
                return [category]

        source = FilteredSource()
        widget = SceneOutliner(source, LocalSelection())
        model = widget.model()
        resets = []
        model.modelReset.connect(lambda: resets.append(True))
        widget.destroyed.connect(lambda *_, source=source: shiboken.delete(source))
        widget.deleteLater()
        flush()
        self.assertFalse(shiboken.isValid(widget))
        self.assertFalse(shiboken.isValid(source))
        self.assertTrue(resets)

    def test_closing_one_view_does_not_destroy_a_shared_provider(self):
        other = SceneOutliner(self.source)
        other.deleteLater()
        flush()
        self.assertIs(self.widget.data_source(), self.source)
        self.assertTrue(self.widget.model().ensure_index_for_item(item("leaf")).isValid())
        self.source.notify(Change.changed([item("a")]))
        self.assertEqual(self.widget.model().ensure_index_for_item(item("a")).data(), "a")

    def test_model_source_replacement_releases_the_previous_python_provider(self):
        source = Source()
        previous = weakref.ref(source)
        self.widget.set_data_source(source)
        del source
        gc.collect()
        self.assertIsNotNone(previous())
        self.widget.model().set_data_source(Source())
        gc.collect()
        self.assertIsNone(previous())
        self.assertTrue(self.widget.model().ensure_index_for_item(item("leaf")).isValid())

    def test_reentrant_model_source_replacement_retains_the_current_provider(self):
        model = SceneOutlinerModel()
        references = []

        def replace(*args):
            if references:
                return
            fresh = Source()
            references.append(weakref.ref(fresh))
            model.set_data_source(fresh)

        model.source_changed.connect(replace)
        model.set_data_source(Source())
        gc.collect()
        self.assertIsNotNone(references[0]())
        self.assertIs(model.data_source(), references[0]())
        self.assertTrue(model.ensure_index_for_item(item("leaf")).isValid())

    def test_reentrant_selection_replacement_retains_the_current_adapter(self):
        references = []
        widget = self.widget

        class ReplacingSelection(LocalSelection):
            def selected_items(self):
                fresh = LocalSelection()
                references.append(weakref.ref(fresh))
                widget.set_selection_context(fresh)
                return []

        widget.set_selection_context(ReplacingSelection())
        gc.collect()
        self.assertIsNotNone(references[0]())
        self.assertIs(widget.selection_context(), references[0]())

    def test_native_selection_destruction_releases_its_python_wrapper(self):
        owner = QtCore.QObject()
        selection = LocalSelection()
        selection.setParent(owner)
        reference = weakref.ref(selection)
        self.widget.set_selection_context(selection)
        del selection
        shiboken.delete(owner)
        gc.collect()
        self.assertIsNone(self.widget.selection_context())
        self.assertIsNone(reference())
        self.assertTrue(self.widget.index_for_item(item("leaf")).isValid())

    def test_default_search_stops_after_its_python_provider_is_released(self):
        widget = self.widget
        fresh = Source()

        class ReleasingSource(Source):
            armed = False

            def children(self, parent):
                rows = super().children(parent)
                if self.armed and parent == item("a"):
                    self.armed = False
                    widget.set_data_source(fresh)
                return rows

        source = ReleasingSource()
        reference = weakref.ref(source)
        widget.set_data_source(source)
        source.armed = True
        del source
        widget.findChild(QtWidgets.QLineEdit).setText("leaf")
        for timer in widget.findChildren(QtCore.QTimer):
            if timer.isSingleShot():
                timer.timeout.emit()
        self.assertTrue(wait_for(lambda: not widget.search_pending()))
        gc.collect()
        self.assertIsNone(reference())
        self.assertIs(widget.data_source(), fresh)
        proxy = widget.tree_view().model()
        self.assertTrue(proxy.mapFromSource(widget.index_for_item(item("leaf"))).isValid())

    def test_destroyed_source_callback_can_install_another_python_source(self):
        model = SceneOutlinerModel()
        source = Source()
        references = []
        model.set_data_source(source)

        def replace(*args):
            current = Source()
            references.append(weakref.ref(current))
            model.set_data_source(current)

        source.destroyed.connect(replace)
        del source
        model.set_data_source(Source())
        gc.collect()
        self.assertEqual(len(references), 1)
        self.assertIsNotNone(references[0]())
        self.assertIs(model.data_source(), references[0]())
        self.assertTrue(model.ensure_index_for_item(item("leaf")).isValid())

    def test_provider_back_reference_cycles_are_collected(self):
        source = Source()
        widget = SceneOutliner(source)
        source.widget = widget
        references = weakref.ref(source), weakref.ref(widget)
        del source, widget
        gc.collect()
        flush()
        gc.collect()
        self.assertTrue(all(reference() is None for reference in references))

    def test_menu_callbacks_can_destroy_the_menu_widget_or_parent(self):
        for kind in ("context", "header"):
            for timing in ("populate", "action"):
                for target in ("menu", "widget", "parent"):
                    with self.subTest(kind=kind, timing=timing, target=target):
                        owner = QtWidgets.QWidget()
                        widget = SceneOutliner(Source(), LocalSelection(), owner)
                        view = widget.tree_view()
                        observed = []

                        def populate(menu):
                            observed.append(menu)
                            obj = {"menu": menu, "widget": widget, "parent": owner}[target]
                            if timing == "populate":
                                shiboken.delete(obj)
                            else:
                                action = menu.addAction("Close")
                                action.triggered.connect(lambda: shiboken.delete(obj))
                                QtCore.QTimer.singleShot(0, action.trigger)

                        signal = (
                            widget.context_menu_requested
                            if kind == "context"
                            else view.header_menu_requested
                        )
                        signal.connect(populate)
                        request = (
                            view.customContextMenuRequested
                            if kind == "context"
                            else view.header().customContextMenuRequested
                        )
                        try:
                            request.emit(QtCore.QPoint())
                            flush()
                            self.assertEqual(len(observed), 1)
                            self.assertFalse(shiboken.isValid(observed[0]))
                            self.assertEqual(shiboken.isValid(widget), target == "menu")
                            self.assertEqual(shiboken.isValid(owner), target != "parent")
                        finally:
                            if shiboken.isValid(owner):
                                shiboken.delete(owner)
