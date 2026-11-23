# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from opendcc.ui.scene_outliner import Change, SceneOutliner, SceneItemId
from scene_outliner_test_utils import (
    Source,
    LocalSelection,
    item,
    flush,
    ForestSource,
    SceneOutlinerTest,
    shiboken,
)


class TestExpansion(SceneOutlinerTest):
    def test_expansion_restoration_stops_after_provider_callback_changes(self):
        for change in ("widget", "model", "source", "reset"):
            with self.subTest(change=change):

                class SourceWithCallback(Source):
                    callback = None

                    def parent(self, row):
                        callback, self.callback = self.callback, None
                        if callback:
                            callback()
                        return super().parent(row)

                source = SourceWithCallback()
                widget = SceneOutliner(source)
                tree = widget.tree_view()
                model = widget.model()
                events = []
                tree.expansion_changed.connect(lambda: events.append(True))

                def replace():
                    if change == "widget":
                        shiboken.delete(widget)
                    elif change == "model":
                        shiboken.delete(model)
                    elif change == "source":
                        widget.set_data_source(Source())
                    else:
                        source.notify(Change.reset())

                source.callback = replace
                try:
                    tree.set_expanded_items([item("leaf")])
                    self.assertEqual(events, [])
                    if change == "widget":
                        self.assertFalse(shiboken.isValid(widget))
                    elif change == "model":
                        self.assertFalse(shiboken.isValid(model))
                finally:
                    if shiboken.isValid(widget):
                        shiboken.delete(widget)

    def test_recursive_expansion_materializes_cold_descendants(self):
        for operation in ("expandAll", "expand_branch"):
            for show_root in (True, False):
                with self.subTest(operation=operation, show_root=show_root):
                    source = Source()
                    source.tree = {
                        "root": ["a"],
                        "a": ["b"],
                        "b": ["leaf"],
                        "leaf": [],
                    }
                    widget = SceneOutliner(source)
                    try:
                        view = widget.tree_view()
                        view.set_show_root(show_root)
                        widget.show()
                        flush()
                        if operation == "expandAll":
                            view.expandAll()
                        else:
                            view.expand_branch(item("root"))
                        for key in ("a", "b"):
                            index = view.model().mapFromSource(
                                widget.model().index_for_item(item(key))
                            )
                            self.assertTrue(index.isValid())
                            self.assertTrue(view.isExpanded(index))
                    finally:
                        shiboken.delete(widget)

    def test_expansion_state_can_be_cleared_without_a_source(self):
        self.widget.tree_view().set_expanded_items([item("root"), item("a")])
        self.widget.set_data_source(None)
        self.widget.tree_view().set_expanded_items([])
        self.assertEqual(self.widget.tree_view().expanded_items(), [])

    def test_multiple_roots_support_navigation_root_visibility_and_selection(self):
        source = ForestSource()
        self.widget.set_data_source(source)
        model, view = self.widget.model(), self.widget.tree_view()
        self.assertEqual(model.rowCount(), 2)
        self.assertFalse(model.root_item())
        for row, name in enumerate(("a", "b")):
            index = model.index(row, 0)
            self.assertEqual(model.item_for_index(index), item(name))
            self.assertFalse(model.parent(index).isValid())
        leaf = model.ensure_index_for_item(item("bleaf"))
        self.assertEqual(model.item_for_index(leaf.parent()), item("b"))
        for shown in (False, True):
            view.set_show_root(shown)
            self.assertFalse(view.rootIndex().isValid())
            self.assertEqual(view.model().rowCount(), 2)
        view.expandAll()
        flush()
        self.assertTrue({item("a"), item("b")}.issubset(view.expanded_items()))
        self.widget.selection_context().set_selection([item("leaf"), item("bleaf")])
        flush()
        self.assertEqual(set(self.widget.selected_items()), {item("leaf"), item("bleaf")})

    def test_multiple_roots_preserve_expansion_across_updates_and_reordering(self):
        source = ForestSource()
        self.widget.set_data_source(source)
        model, view = self.widget.model(), self.widget.tree_view()
        view.expandAll()
        flush()
        source.tree["b"].append("new")
        source.tree["new"] = []
        source.notify(Change.added([item("b")]))
        flush()
        self.assertTrue(model.ensure_index_for_item(item("new")).isValid())
        source.root_names = ["b", "a"]
        source.notify(Change.added([SceneItemId()]))
        flush()
        self.assertEqual([model.index(row, 0).data() for row in range(2)], ["b", "a"])
        self.assertTrue({item("a"), item("b")}.issubset(view.expanded_items()))

    def test_highlight_callbacks_can_destroy_the_proxy_model_or_widget(self):
        for target in ("proxy", "model", "widget"):
            with self.subTest(target=target):
                widget = SceneOutliner(Source())
                model, proxy = widget.model(), widget.tree_view().model()
                for key in ("root", "a", "b", "leaf"):
                    proxy.mapFromSource(widget.index_for_item(item(key)))
                objects = {"proxy": proxy, "model": model, "widget": widget}
                events = []

                def destroy(*args):
                    events.append(True)
                    shiboken.delete(objects[target])

                proxy.dataChanged.connect(destroy)
                try:
                    proxy.set_highlighted([item("a"), item("b"), item("leaf")])
                    self.assertEqual(events, [True])
                    self.assertFalse(shiboken.isValid(objects[target]))
                finally:
                    if shiboken.isValid(proxy):
                        proxy.dataChanged.disconnect(destroy)
                    if shiboken.isValid(widget):
                        shiboken.delete(widget)

    def test_reveal_stops_when_expansion_deletes_the_widget(self):
        selection = LocalSelection()
        widget = SceneOutliner(Source(), selection)
        view = widget.tree_view()
        selection.set_selection([item("leaf")])
        view.collapseAll()
        view.expansion_changed.connect(lambda: shiboken.delete(widget))
        view.reveal_selection()
        self.assertFalse(shiboken.isValid(widget))

    def test_content_reset_preserves_collapsed_root(self):
        view = self.widget.tree_view()
        view.collapseAll()
        self.source.notify(Change.reset())
        flush()
        root = view.model().mapFromSource(self.widget.index_for_item(item("root")))
        self.assertFalse(view.isExpanded(root))
        self.assertEqual(view.expanded_items(), [])
