# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
import gc
import threading
import weakref
from Qt import QtCore
from opendcc.ui.scene_outliner import (
    Change,
    SceneOutliner,
    SceneOutlinerModel,
    SceneOutlinerProxyModel,
)
from scene_outliner_test_utils import (
    Source,
    LocalSelection,
    item,
    flush,
    wait_for,
    SceneOutlinerTest,
    shiboken,
)


class TestSourceUpdates(SceneOutlinerTest):
    def test_layout_rechecks_children_after_a_structural_callback(self):
        source = self.source
        source.tree["a"].append("other")
        source.tree["other"] = []
        model = SceneOutlinerModel()
        model.set_data_source(source)
        leaf = QtCore.QPersistentModelIndex(model.ensure_index_for_item(item("leaf")))
        old_parent = model.index_for_item(item("a"))
        new_parent = model.ensure_index_for_item(item("b"))
        model.rowCount(new_parent)
        layouts = []

        def move_leaf(*args):
            model.layoutAboutToBeChanged.disconnect(move_leaf)
            source.tree["a"].remove("leaf")
            source.tree["b"].append("leaf")
            source.notify(Change.added([item("a"), item("b")]))

        def record_layout(*args):
            layouts.append((model.rowCount(old_parent), model.rowCount(new_parent), leaf.parent()))

        model.layoutAboutToBeChanged.connect(move_leaf)
        model.layoutChanged.connect(record_layout)
        source.tree["a"].reverse()
        source.notify(Change.added([item("a")]))
        self.assertEqual(layouts[0], (2, 0, old_parent))
        flush()
        self.assertEqual(layouts[-1], (1, 1, new_parent))

    def test_new_rows_wait_for_confirmed_branch_indicators(self):
        root = self.proxy.mapFromSource(self.model.ensure_index_for_item(item("root")))
        self.view.expand(root)
        flush()
        self.source.tree["root"].extend(["new_leaf", "new_branch"])
        self.source.tree.update(new_leaf=[], new_branch=["branch_child"], branch_child=[])
        self.source.notify(Change.added([item("root")]))
        indices = [
            self.proxy.mapFromSource(self.model.ensure_index_for_item(item(name)))
            for name in ("new_leaf", "new_branch")
        ]
        self.view.doItemsLayout()
        rects = [self.view.visualRect(index) for index in indices]
        self.assertTrue(all(rect.isValid() for rect in rects))

        def indicator_top_pixels():
            grab = self.view.viewport().grab()
            image = grab.toImage()
            scale = grab.devicePixelRatio()
            return [
                image.pixelColor(
                    round((rect.left() - self.view.indentation() // 2) * scale),
                    round((rect.top() + rect.height() // 2 - 4) * scale),
                )
                for rect in rects
            ]

        unresolved = indicator_top_pixels()
        flush()
        resolved = indicator_top_pixels()
        self.assertEqual(unresolved[0], resolved[0])
        self.assertNotEqual(unresolved[1], resolved[1])
        expected_height = max(self.view.fontMetrics().height(), self.view.iconSize().height())
        self.assertEqual([rect.height() for rect in rects], [expected_height, expected_height])

        self.source.tree["root"].append("another_leaf")
        self.source.tree["another_leaf"] = []
        self.source.notify(Change.added([item("root")]))
        self.assertEqual(indicator_top_pixels(), resolved)
        flush()
        self.assertEqual(indicator_top_pixels(), resolved)

    def test_source_deleted_during_reset_preserves_the_current_source(self):
        model = SceneOutlinerModel()
        model.set_data_source(self.source)
        replacement = Source()
        model.modelAboutToBeReset.connect(lambda: shiboken.delete(replacement))
        model.set_data_source(replacement)
        self.assertFalse(shiboken.isValid(replacement))
        self.assertIs(model.data_source(), self.source)
        self.assertTrue(model.ensure_index_for_item(item("leaf")).isValid())

    def test_data_source_notifications_exclude_backend_updates(self):
        model = SceneOutlinerModel()
        changes = []
        model.data_source_changed.connect(lambda: changes.append(model.data_source()))
        first, second = Source(), Source()
        model.set_data_source(first)
        for change in (Change.changed([item("a")]), Change.added([item("root")]), Change.reset()):
            first.notify(change)
        self.assertEqual(changes, [first])
        model.set_data_source(second)
        model.set_data_source(None)
        self.assertEqual(changes, [first, second, None])

    def test_workers_cannot_replace_gui_outliner_dependencies(self):
        calls = []

        class TracedSource(Source):
            def columns(self):
                calls.append("columns")
                return super().columns()

        class TracedSelection(LocalSelection):
            def selected_items(self):
                calls.append("selection")
                return super().selected_items()

        source, selection = TracedSource(), TracedSelection()
        model, proxy = SceneOutlinerModel(), SceneOutlinerProxyModel()
        proxy.setSourceModel(model)
        for method, arguments in (
            (model.set_data_source, (source,)),
            (model.set_data_source, (None,)),
            (self.widget.set_data_source, (source,)),
            (self.widget.set_data_source, (None,)),
            (self.widget.set_selection_context, (selection,)),
            (self.widget.set_selection_context, (None,)),
        ):
            with self.subTest(method=method, arguments=arguments):
                errors = []

                def replace():
                    try:
                        method(*arguments)
                    except BaseException as error:
                        errors.append(error)

                worker = threading.Thread(target=replace)
                worker.start()
                worker.join()
                self.assertEqual(len(errors), 1)
                self.assertIsInstance(errors[0], RuntimeError)
                self.assertEqual(calls, [])
                self.assertIs(self.widget.data_source(), self.source)
                self.assertIsNone(model.data_source())

    def test_worker_notifications_and_selection_return_to_gui_thread(self):
        owner = threading.get_ident()
        observed = []
        model = self.widget.model()
        model.modelReset.connect(lambda: observed.append(threading.get_ident()))
        worker = threading.Thread(target=lambda: self.source.notify(Change.reset()))
        worker.start()
        worker.join()
        self.assertEqual(observed, [])
        self.assertTrue(wait_for(lambda: len(observed) == 1))
        self.assertEqual(observed, [owner])
        selection = self.widget.selection_context()
        worker = threading.Thread(target=lambda: selection.set_selection([item("leaf")]))
        worker.start()
        worker.join()
        self.assertTrue(wait_for(lambda: self.widget.selected_items() == [item("leaf")]))

    def test_stale_worker_notice_after_replacement_is_ignored(self):
        resets = []
        self.widget.model().modelReset.connect(lambda: resets.append(True))
        for restore_original in (False, True):
            with self.subTest(restore_original=restore_original):
                self.widget.set_data_source(self.source)
                flush()
                resets.clear()
                worker = threading.Thread(target=lambda: self.source.notify(Change.reset()))
                worker.start()
                worker.join()
                self.widget.set_data_source(Source())
                if restore_original:
                    self.widget.set_data_source(self.source)
                expected = 2 if restore_original else 1
                self.assertEqual(len(resets), expected)
                flush()
                self.assertEqual(len(resets), expected)

    def test_queued_reset_precedes_an_earlier_child_hint(self):
        hints = []

        class HintSource(Source):
            def has_children(self, parent):
                hints.append(parent.key)
                return super().has_children(parent)

        source = HintSource()
        source.tree["a"] = ["cold"]
        source.tree["cold"] = ["leaf"]
        self.widget.set_data_source(source)
        flush()
        branch = self.model.ensure_index_for_item(item("cold"))
        self.assertTrue(branch.isValid())
        self.assertNotIn("cold", hints)
        resets = []

        def request_update(_affects_search):
            self.model.source_about_to_change.disconnect(request_update)
            self.model.hasChildren(branch)
            source.notify(Change.reset())

        self.model.source_about_to_change.connect(request_update)
        self.model.modelReset.connect(lambda: resets.append(True))
        source.notify(Change.changed([item("b")], False))

        self.assertEqual(resets, [])
        self.assertTrue(wait_for(lambda: resets == [True]))
        flush()
        self.assertNotIn("cold", hints)
        self.assertTrue(self.model.ensure_index_for_item(item("leaf")).isValid())

    def test_update_notifications_defer_reentrant_model_changes(self):
        model = self.widget.model()
        events = []

        def notify_again(*args):
            model.source_about_to_change.disconnect(notify_again)
            events.append("about")
            self.source.notify(Change.reset())
            events.append("returned")

        model.source_about_to_change.connect(notify_again)
        model.modelReset.connect(lambda: events.append("reset"))
        self.source.notify(Change.reset())
        self.assertEqual(events, ["about", "returned", "reset"])
        flush()
        self.assertEqual(events, ["about", "returned", "reset", "reset"])

    def test_source_destruction_preserves_a_queued_live_replacement(self):
        for order in ("replace_first", "destroy_first"):
            for candidate in ("live", "destroyed", "wrong_thread"):
                with self.subTest(order=order, candidate=candidate):
                    source, replacement = Source(), Source()
                    worker = QtCore.QThread()
                    worker.finished.connect(replacement.deleteLater)
                    widget = SceneOutliner(source)
                    model = widget.model()
                    model.ensure_index_for_item(item("leaf"))

                    def replace(*args):
                        model.rowsAboutToBeInserted.disconnect(replace)
                        if order == "destroy_first":
                            shiboken.delete(source)
                        widget.set_data_source(replacement)
                        if candidate == "destroyed":
                            shiboken.delete(replacement)
                        elif candidate == "wrong_thread":
                            replacement.moveToThread(worker)
                            worker.start()
                        if order == "replace_first":
                            shiboken.delete(source)
                        QtCore.QCoreApplication.processEvents()

                    model.rowsAboutToBeInserted.connect(replace)
                    try:
                        source.tree["a"].append("new")
                        source.tree["new"] = []
                        source.notify(Change.added([item("a")]))
                        flush()
                        if candidate != "live":
                            self.assertIsNone(widget.data_source())
                            self.assertEqual(model.rowCount(), 0)
                        else:
                            self.assertIs(widget.data_source(), replacement)
                            self.assertTrue(model.ensure_index_for_item(item("leaf")).isValid())
                    finally:
                        shiboken.delete(widget)
                        worker.quit()
                        self.assertTrue(worker.wait(3000))

    def test_source_changes_wait_for_complete_row_transactions(self):
        for operation in ("insert", "remove"):
            for change in ("replace", "reset", "clear", "destroy", "destroy_active"):
                with self.subTest(operation=operation, change=change):
                    source = Source()
                    model = SceneOutlinerModel()
                    model.set_data_source(source)
                    model.ensure_index_for_item(item("leaf"))
                    events, references, row_states = [], [], []
                    before = getattr(
                        model,
                        (
                            "rowsAboutToBeInserted"
                            if operation == "insert"
                            else "rowsAboutToBeRemoved"
                        ),
                    )
                    after = getattr(
                        model, "rowsInserted" if operation == "insert" else "rowsRemoved"
                    )

                    def replace(*args):
                        before.disconnect(replace)
                        events.append("begin")
                        if change == "replace":
                            fresh = Source()
                            references.append(weakref.ref(fresh))
                            model.set_data_source(fresh)
                        elif change == "clear":
                            model.set_data_source(None)
                        elif change == "destroy":
                            owner = QtCore.QObject()
                            fresh = Source()
                            fresh.setParent(owner)
                            model.set_data_source(fresh)
                            shiboken.delete(owner)
                        elif change == "destroy_active":
                            shiboken.delete(source)
                        else:
                            source.notify(Change.reset())

                    def completed(parent, first, last):
                        events.append("end")
                        child = model.index(0, 0, parent)
                        row_states.append(
                            (
                                model.rowCount(parent),
                                model.rowCount(),
                                model.index(0, 0).isValid(),
                                not child.isValid() or model.parent(child) == parent,
                            )
                        )

                    before.connect(replace)
                    after.connect(completed)
                    model.modelReset.connect(lambda: events.append("reset"))
                    if operation == "insert":
                        source.tree["a"].append("new")
                        source.tree["new"] = []
                        source.notify(Change.added([item("a")]))
                    else:
                        source.tree["a"].clear()
                        del source.tree["leaf"]
                        source.notify(Change.removed([item("a")]))
                    gc.collect()
                    self.assertEqual(events, ["begin", "end"])
                    self.assertEqual(
                        row_states, [(2 if operation == "insert" else 0, 1, True, True)]
                    )
                    if references:
                        self.assertIsNotNone(references[0]())
                    if change == "destroy":
                        flush()
                        self.assertEqual(events, ["begin", "end"])
                    else:
                        self.assertTrue(wait_for(lambda: events == ["begin", "end", "reset"]))
                    expected = (
                        references[0]()
                        if references
                        else (None if change in ("clear", "destroy_active") else source)
                    )
                    self.assertIs(model.data_source(), expected)
                    shiboken.delete(model)

    def test_selection_replacement_discards_the_previous_adapters_items(self):
        fresh = LocalSelection()
        fresh.items = [item("b")]
        widget = self.widget

        class ReplacingSelection(LocalSelection):
            def selected_items(self):
                widget.set_selection_context(fresh)
                return [item("a")]

        widget.set_selection_context(ReplacingSelection())
        self.assertIs(widget.selection_context(), fresh)
        self.assertEqual(widget.selected_items(), [item("b")])
        flush()
        self.assertEqual(widget.selected_items(), [item("b")])

    def test_later_source_request_cancels_a_deferred_replacement(self):
        source = Source()
        model = SceneOutlinerModel()
        model.set_data_source(source)
        model.ensure_index_for_item(item("leaf"))
        references = []

        def replace(*args):
            model.rowsAboutToBeInserted.disconnect(replace)
            pending = Source()
            references.append(weakref.ref(pending))
            model.set_data_source(pending)

        model.rowsAboutToBeInserted.connect(replace)
        source.tree["a"].append("new")
        source.tree["new"] = []
        source.notify(Change.added([item("a")]))
        latest = Source()
        model.set_data_source(latest)
        flush()
        gc.collect()
        self.assertIs(model.data_source(), latest)
        self.assertIsNone(references[0]())
        shiboken.delete(model)

    def test_widget_same_source_request_cancels_pending_changes(self):
        for pending in ("replace", "clear"):
            for timing in ("inside", "after"):
                with self.subTest(pending=pending, timing=timing):
                    source = Source()
                    widget = SceneOutliner(source)
                    model = widget.model()
                    model.ensure_index_for_item(item("leaf"))
                    references = []
                    resets = []
                    model.modelReset.connect(lambda: resets.append(True))

                    def replace(*args):
                        model.rowsAboutToBeInserted.disconnect(replace)
                        replacement = Source() if pending == "replace" else None
                        if replacement:
                            references.append(weakref.ref(replacement))
                        widget.set_data_source(replacement)
                        if timing == "inside":
                            widget.set_data_source(source)

                    model.rowsAboutToBeInserted.connect(replace)
                    try:
                        source.tree["a"].append("new")
                        source.tree["new"] = []
                        source.notify(Change.added([item("a")]))
                        if timing == "after":
                            widget.set_data_source(source)
                        flush()
                        gc.collect()
                        self.assertIs(widget.data_source(), source)
                        self.assertFalse(resets)
                        if references:
                            self.assertIsNone(references[0]())
                    finally:
                        shiboken.delete(widget)

    def test_source_replacement_clears_expansion_but_backend_resets_preserve_it(self):
        view = self.widget.tree_view()
        for replace in (self.widget.set_data_source, self.widget.model().set_data_source):
            view.set_expanded_items([item("root"), item("a")])
            source = self.widget.data_source()
            source.notify(Change.reset())
            flush()
            self.assertIn(item("a"), view.expanded_items())
            replace(source)
            flush()
            self.assertIn(item("a"), view.expanded_items())
            replace(Source())
            flush()
            self.assertNotIn(item("a"), view.expanded_items())
            index = view.model().mapFromSource(self.widget.index_for_item(item("a")))
            self.assertFalse(view.isExpanded(index))
            self.widget.data_source().notify(Change.reset())
            flush()
            root = view.model().mapFromSource(self.widget.index_for_item(item("root")))
            self.assertTrue(view.isExpanded(root))
            self.assertFalse(
                view.isExpanded(view.model().mapFromSource(self.widget.index_for_item(item("a"))))
            )
