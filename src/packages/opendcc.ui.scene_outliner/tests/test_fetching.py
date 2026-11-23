# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from collections import Counter

from Qt import QtCore

from opendcc.ui.scene_outliner import Change, Filter
from scene_outliner_test_utils import SceneOutlinerTest, Source, flush, item, wait_for


class WideSource(Source):
    def __init__(self, filtered=False):
        super().__init__()
        self.rows = [item("sibling{:04d}".format(row)) for row in range(2050)]
        self.destination = []
        self.filtered = filtered
        self.filter_calls = Counter()

    def children(self, parent):
        if parent == item("a"):
            self.queries.append("a")
            return self.rows
        if parent == item("b"):
            return self.destination
        return super().children(parent)

    def has_children(self, parent):
        if parent == item("a"):
            return bool(self.rows)
        if parent == item("b"):
            return bool(self.destination)
        return super().has_children(parent)

    def has_item(self, row):
        return super().has_item(row) or row in self.rows or row in self.destination

    def parent(self, row):
        if row in self.destination:
            return item("b")
        if row in self.rows:
            return item("a")
        return super().parent(row)

    def filters(self):
        if not self.filtered:
            return []
        category = Filter()
        category.id = "sibling"
        category.label = "Siblings"
        category.shown_by_default = False
        return [category]

    def matches_filter(self, row, category):
        if row.key.startswith("sibling"):
            self.filter_calls[row.key] += 1
            return row.key != "sibling2049"
        return False


class TestFetching(SceneOutlinerTest):
    def prepare_branch(self, source):
        self.widget.set_data_source(source)
        model = self.widget.model()
        branch = model.ensure_index_for_item(item("a"))
        flush()
        return model, branch

    def test_small_branch_is_published_as_one_insertion(self):
        source = WideSource()
        source.rows = source.rows[:3]
        model, branch = self.prepare_branch(source)
        insertions = []

        def after_insert(parent, first, last):
            if parent == branch:
                insertions.append((first, last))

        model.rowsInserted.connect(after_insert)
        model.fetchMore(branch)
        self.assertTrue(wait_for(lambda: model.rowCount(branch) == 3))
        self.assertEqual(insertions, [(0, 2)])
        self.assertFalse(model.canFetchMore(branch))

    def test_fetch_continues_after_filtered_batches_and_deduplicates_requests(self):
        source = WideSource(filtered=True)
        model, branch = self.prepare_branch(source)
        tree = self.widget.tree_view()
        proxy = tree.model()
        proxy_branch = proxy.mapFromSource(branch)
        self.assertTrue(model.canFetchMore(branch))
        self.assertFalse(model.canFetchMore(branch.sibling(branch.row(), 1)))
        insertions = []
        model.rowsInserted.connect(
            lambda parent, first, last: (
                insertions.append((first, last)) if parent == branch else None
            )
        )

        tree.expand(proxy_branch)
        model.fetchMore(branch)
        model.fetchMore(branch)
        proxy.rowCount(proxy_branch)
        self.assertTrue(wait_for(lambda: proxy.rowCount(proxy_branch) == 1))
        self.assertEqual(insertions, [(0, 1023), (1024, 2047), (2048, 2049)])
        self.assertEqual(source.queries.count("a"), 1)
        self.assertEqual(len(source.filter_calls), len(source.rows))
        self.assertEqual(set(source.filter_calls.values()), {1})
        self.assertEqual(
            model.item_for_index(proxy.mapToSource(proxy.index(0, 0, proxy_branch))),
            source.rows[-1],
        )

    def test_materializing_a_late_item_finishes_loading_before_returning(self):
        source = WideSource()
        model, branch = self.prepare_branch(source)
        resolved = []

        def materialize():
            index = model.ensure_index_for_item(source.rows[-1])
            resolved.append((model.item_for_index(index), index.row(), model.rowCount(branch)))

        def after_insert(parent, first, last):
            if parent == branch and first == 0 and last == 1023:
                QtCore.QTimer.singleShot(0, materialize)

        model.rowsInserted.connect(after_insert)
        model.fetchMore(branch)
        self.assertTrue(wait_for(lambda: bool(resolved)))
        self.assertEqual(resolved, [(source.rows[-1], 2049, 2050)])
        self.assertEqual(source.queries.count("a"), 1)

    def test_structural_changes_reconcile_published_rows_and_discard_the_old_tail(self):
        source = WideSource()
        model, branch = self.prepare_branch(source)
        moved = source.rows[0]
        removed = source.rows[-1]
        retained = []

        def mutate():
            source.destination.append(source.rows.pop(0))
            source.rows.pop()
            source.notify(Change.removed([item("a"), item("b")]))

        def after_insert(parent, first, last):
            if parent == branch and first == 0 and last == 1023:
                retained.append(QtCore.QPersistentModelIndex(model.index(0, 0, branch)))
                QtCore.QTimer.singleShot(0, mutate)

        model.rowsInserted.connect(after_insert)
        model.fetchMore(branch)
        self.assertTrue(wait_for(lambda: model.rowCount(branch) == 2048))
        self.assertEqual(source.queries.count("a"), 2)
        self.assertFalse(model.ensure_index_for_item(removed).isValid())
        self.assertEqual(retained, [model.index_for_item(moved)])
        self.assertEqual(retained[0].parent(), model.index_for_item(item("b")))
