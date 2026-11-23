# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtWidgets
from opendcc.ui.scene_outliner import (
    Change,
    Filter,
    SceneOutliner,
)
from scene_outliner_test_utils import (
    Source,
    item,
    flush,
    wait_for,
    ForestSource,
    begin_search,
    SceneOutlinerTest,
    shiboken,
)


class TestFiltering(SceneOutlinerTest):
    def test_show_filters_survive_resets_and_use_defaults_for_a_new_source(self):
        class FilteredSource(Source):
            def filters(self):
                category = Filter()
                category.id = "blocked"
                category.label = "Blocked"
                category.shown_by_default = False
                return [category]

            def matches_filter(self, child, category):
                return child == item("a")

        source = FilteredSource()
        self.widget.set_data_source(source)
        self.widget.set_filter_shown("blocked", True)
        source.notify(Change.reset())
        self.assertTrue(self.widget.filter_shown("blocked"))
        proxy = self.widget.tree_view().model()
        self.assertTrue(proxy.mapFromSource(self.widget.index_for_item(item("a"))).isValid())
        self.widget.set_data_source(FilteredSource())
        self.assertFalse(self.widget.filter_shown("blocked"))
        self.assertFalse(proxy.mapFromSource(self.widget.index_for_item(item("a"))).isValid())

    def test_category_callbacks_can_destroy_their_widget_model_or_source(self):
        for target in ("widget", "model", "source"):
            with self.subTest(target=target):

                class FilteringSource(Source):
                    callback = None

                    def matches_filter(self, row, category):
                        callback, self.callback = self.callback, None
                        if callback:
                            callback()
                        return False

                source = FilteringSource()
                widget = SceneOutliner(source)
                objects = {"widget": widget, "model": widget.model(), "source": source}
                source.callback = lambda: shiboken.delete(objects[target])
                try:
                    widget.set_filter_shown("test", False)
                    self.assertFalse(shiboken.isValid(objects[target]))
                finally:
                    if shiboken.isValid(widget):
                        shiboken.delete(widget)

    def test_category_callbacks_for_new_rows_run_outside_qt_filtering(self):
        class FilteringSource(Source):
            callback = None

            def matches_filter(self, row, category):
                if row == item("leaf") and self.callback:
                    callback, self.callback = self.callback, None
                    callback()
                return False

        source = FilteringSource()
        widget = SceneOutliner(source)
        tree = widget.tree_view()
        proxy = tree.model()
        widget.set_filter_shown("test", False)
        flush()
        source_index = widget.model().ensure_index_for_item(item("a"))
        self.assertTrue(wait_for(lambda: proxy.mapFromSource(source_index).isValid()))
        index = proxy.mapFromSource(source_index)
        source.callback = lambda: shiboken.delete(widget)
        proxy.rowCount(index)
        self.assertTrue(shiboken.isValid(widget))
        flush()
        self.assertFalse(shiboken.isValid(widget))

    def test_filter_callbacks_can_destroy_or_replace_the_widget_source(self):
        class MenuSource(Source):
            callback = None
            label = "Original"

            def filters(self):
                callback, self.callback = self.callback, None
                if callback:
                    callback()
                category = Filter()
                category.id = self.label
                category.label = self.label
                return [category]

        for action in ("delete", "replace"):
            with self.subTest(action=action):
                source = MenuSource()
                widget = SceneOutliner(source)
                replacement = MenuSource()
                replacement.label = "Replacement"
                events = []
                widget.model().modelReset.connect(lambda: events.append("reset"))

                def change():
                    events.append("filters")
                    if action == "delete":
                        shiboken.delete(widget)
                    else:
                        widget.set_data_source(replacement)

                source.callback = change
                try:
                    source.notify(Change.reset())
                    self.assertEqual(events[:2], ["reset", "filters"])
                    if action == "delete":
                        self.assertFalse(shiboken.isValid(widget))
                    else:
                        self.assertIs(widget.data_source(), replacement)
                        labels = {entry.label for entry in widget.data_source().filters()}
                        self.assertIn("Replacement", labels)
                        self.assertNotIn("Original", labels)
                finally:
                    if shiboken.isValid(widget):
                        shiboken.delete(widget)

    def test_cold_proxy_queries_stop_after_hierarchy_callback_changes(self):
        class ColdSource(Source):
            callback = None

            def children(self, parent):
                if parent == item("a") and self.callback:
                    callback, self.callback = self.callback, None
                    callback()
                return super().children(parent)

        for query in ("rowCount", "index", "columnCount", "expand", "expandAll", "qt_expandAll"):
            for change in ("delete", "clear", "replace", "reset"):
                with self.subTest(query=query, change=change):
                    source = ColdSource()
                    widget = SceneOutliner(source)
                    model = widget.model()
                    proxy = widget.tree_view().model()
                    parent = proxy.mapFromSource(model.ensure_index_for_item(item("a")))
                    if query == "expand":
                        widget.show()
                        flush()
                    source.callback = {
                        "delete": lambda: shiboken.delete(widget),
                        "clear": lambda: widget.set_data_source(None),
                        "replace": lambda: widget.set_data_source(Source()),
                        "reset": lambda: source.notify(Change.reset()),
                    }[change]
                    try:
                        if query == "index":
                            self.assertFalse(proxy.index(0, 0, parent).isValid())
                        elif query == "rowCount":
                            self.assertEqual(proxy.rowCount(parent), 0)
                        elif query == "columnCount":
                            self.assertEqual(proxy.columnCount(parent), model.columnCount())
                        elif query == "expand":
                            widget.tree_view().expand(parent)
                        elif query == "qt_expandAll":
                            QtWidgets.QTreeView.expandAll(widget.tree_view())
                        else:
                            widget.tree_view().expandAll()
                        if query != "expandAll":
                            self.assertIsNotNone(source.callback)
                            self.assertTrue(shiboken.isValid(widget))
                        flush()
                        self.assertIsNone(source.callback)
                        if change == "delete":
                            self.assertFalse(shiboken.isValid(widget))
                        else:
                            self.assertTrue(shiboken.isValid(widget))
                            if change == "clear":
                                self.assertIsNone(widget.data_source())
                            else:
                                self.assertTrue(widget.index_for_item(item("leaf")).isValid())
                    finally:
                        if shiboken.isValid(widget):
                            shiboken.delete(widget)

    def test_multiple_roots_support_search_and_category_filters(self):
        self.widget.set_data_source(ForestSource())
        view = self.widget.tree_view()
        begin_search(self.widget, "bleaf")
        self.assertTrue(wait_for(lambda: not self.widget.search_pending()))
        self.assertEqual(view.model().rowCount(), 1)
        self.assertEqual(view.model().index(0, 0).data(), "b")
        begin_search(self.widget, "")
        self.assertTrue(wait_for(lambda: not self.widget.search_pending()))
        self.widget.set_filter_shown("blocked", False)
        flush()
        self.assertEqual(view.model().rowCount(), 1)
        self.widget.set_filter_shown("blocked", True)
        flush()
        self.assertEqual(view.model().rowCount(), 2)

    def test_search_matches_unicode_case_pairs(self):
        class NamedSource(Source):
            def display_name(self, row):
                return {"a": "CAF\u00c9", "b": "\u041f\u0420\u0418\u041c"}.get(row.key, row.key)

        source = NamedSource()
        self.widget.set_data_source(source)
        view = self.widget.tree_view()
        for query, expected in (("caf\u00e9", "a"), ("\u043f\u0440\u0438\u043c", "b")):
            with self.subTest(query=query):
                self.assertEqual(source.search(query), [item(expected)])
                begin_search(self.widget, query)
                self.assertTrue(wait_for(lambda: not self.widget.search_pending()))
                self.assertTrue(
                    view.model().mapFromSource(self.widget.index_for_item(item(expected))).isValid()
                )
