# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
import gc
import weakref
import threading
from Qt import QtCore, QtGui, QtWidgets
from opendcc.ui.scene_outliner import Change, Column, Filter, SceneItemId, SceneOutliner
from scene_outliner_test_utils import Source, LocalSelection, item, flush, SceneOutlinerTest


class TestExtensions(SceneOutlinerTest):
    def start_search(self, text):
        self.widget.findChild(QtWidgets.QLineEdit).setText(text)
        for timer in self.widget.findChildren(QtCore.QTimer):
            timer.timeout.emit()
        flush()

    def test_external_animation_refreshes_all_column_roles_without_rebuilding(self):
        class AnimatedSource(Source):
            frame = 0

            def columns(self):
                columns = super().columns()
                for name, on, off in (
                    ("visibility", "eye_open", "eye_closed"),
                    ("animation.enabled", "activation_on", "activation_off"),
                ):
                    column = Column()
                    column.id = name
                    column.title = name
                    column.role = Column.Role.TOGGLE
                    column.icon_on = ":/icons/" + on
                    column.icon_off = ":/icons/" + off
                    column.interactive = False
                    columns.append(column)
                weight = Column()
                weight.id = "animation.weight"
                weight.title = "Weight"
                columns.append(weight)
                return columns

            def display_name(self, row):
                return f"{row.key} frame {self.frame}"

            def toggle_state(self, row, column):
                return self.frame % (2 if column == "visibility" else 3) == 0

            def column_text(self, row, column):
                if column == "animation.weight":
                    return str(self.frame / 10)
                return super().column_text(row, column)

        source = AnimatedSource()
        self.widget.set_data_source(source)
        flush()
        index = self.widget.index_for_item(item("a"))
        persistent = QtCore.QPersistentModelIndex(index)
        source.queries.clear()
        resets, layouts, deliveries = [], [], []
        self.model.modelReset.connect(lambda: resets.append(True))
        self.model.layoutChanged.connect(lambda *args: layouts.append(True))
        self.model.dataChanged.connect(lambda *args: deliveries.append(threading.get_ident()))
        expansion = set(self.view.expanded_items())
        for frame in (1, 2, 3, 0):
            source.frame = frame
            worker = threading.Thread(target=lambda: source.notify(Change.changed([])))
            worker.start()
            worker.join()
            flush()
            self.assertEqual(index.data(), f"a frame {frame}")
            for column, icon in (
                (2, "eye_open" if frame % 2 == 0 else "eye_closed"),
                (3, "activation_on" if frame % 3 == 0 else "activation_off"),
            ):
                actual = index.sibling(index.row(), column).data(QtCore.Qt.DecorationRole)
                self.assertFalse(actual.isNull())
                self.assertEqual(
                    actual.pixmap(16, 16).toImage(),
                    QtGui.QIcon(":/icons/" + icon).pixmap(16, 16).toImage(),
                )
            self.assertEqual(index.sibling(index.row(), 4).data(), str(frame / 10))
        self.assertTrue(persistent.isValid())
        self.assertFalse(resets)
        self.assertFalse(layouts)
        self.assertFalse(source.queries)
        self.assertFalse(source.edits)
        self.assertTrue(deliveries)
        self.assertEqual(set(deliveries), {threading.get_ident()})
        self.assertEqual(set(self.view.expanded_items()), expansion)

    def test_external_search_and_category_filter_preserve_hidden_selection(self):
        class Categorized(Source):
            def filters(self):
                category = Filter()
                category.id = "external.branch"
                category.label = "External Branch"
                return [category]

            def matches_filter(self, child, category):
                return category == "external.branch" and child == item("a")

        self.widget.set_data_source(Categorized())
        self.selection.set_selection([item("leaf")])
        mutations = self.selection.mutations
        field = self.widget.findChild(QtWidgets.QLineEdit)
        field.setText("LEAF")
        QtCore.QMetaObject.invokeMethod(
            self.widget, "on_filter_timeout", QtCore.Qt.DirectConnection
        )
        leaf = self.widget.index_for_item(item("leaf"))
        self.assertTrue(self.proxy.mapFromSource(leaf).isValid())
        self.assertFalse(self.proxy.mapFromSource(self.widget.index_for_item(item("b"))).isValid())
        self.widget.set_filter_shown("external.branch", False)
        flush()
        self.assertFalse(self.proxy.mapFromSource(leaf).isValid())
        self.assertEqual(self.selection.items, [item("leaf")])
        self.assertEqual(self.selection.mutations, mutations)
        self.widget.set_filter_shown("external.branch", True)
        flush()
        self.assertEqual(self.widget.selected_items(), [item("leaf")])
        field.clear()
        QtCore.QMetaObject.invokeMethod(
            self.widget, "on_filter_timeout", QtCore.Qt.DirectConnection
        )
        self.assertTrue(self.proxy.mapFromSource(self.widget.index_for_item(item("b"))).isValid())
        self.assertEqual(self.selection.mutations, mutations)

    def test_external_column_editor_commits_and_rejected_edit_keeps_value(self):
        class Editable(Source):
            def __init__(self):
                super().__init__(choice="")
                self.values = {}

            def column_text(self, child, column):
                return (
                    self.values.get(child, "")
                    if column == "type"
                    else super().column_text(child, column)
                )

            def set_column_text(self, child, column, value):
                if column != "type" or not self.has_item(child) or value == "rejected":
                    return False
                self.values[child] = value
                self.notify(Change.changed([child]))
                return True

        source = Editable()
        self.widget.set_data_source(source)
        index = self.widget.index_for_item(item("a")).siblingAtColumn(1)
        self.view.edit(self.proxy.mapFromSource(index))
        flush()
        editor = next(e for e in self.view.findChildren(QtWidgets.QLineEdit) if e.isVisible())
        editor.setText("external value")
        for event_type in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            QtCore.QCoreApplication.sendEvent(
                editor, QtGui.QKeyEvent(event_type, QtCore.Qt.Key_Return, QtCore.Qt.NoModifier)
            )
        flush()
        self.assertEqual(source.values[item("a")], "external value")
        self.assertEqual(index.data(), "external value")
        self.assertFalse(self.model.setData(index, "rejected", QtCore.Qt.EditRole))
        self.assertEqual(index.data(), "external value")

    def test_external_selection_groups_adjacent_siblings_and_highlights_their_parents(self):
        self.source.tree.update(a=["a0", "a1", "a2", "a3"], b=["b0", "b1", "b2"])
        self.source.tree.update(
            {name: [] for name in self.source.tree["a"] + self.source.tree["b"]}
        )
        self.source.notify(Change.reset())
        flush()
        self.selection.set_selection([item(name) for name in ("a3", "b2", "a1", "b1", "a0", "a1")])

        self.assertEqual(
            set(self.widget.selected_items()),
            {item(name) for name in ("a0", "a1", "a3", "b1", "b2")},
        )
        ranges = self.view.selectionModel().selection()
        self.assertEqual(len(ranges), 3)
        self.assertEqual(
            {
                (
                    self.model.item_for_index(self.proxy.mapToSource(r.parent())).key,
                    r.top(),
                    r.bottom(),
                    r.left(),
                    r.right(),
                )
                for r in ranges
            },
            {("a", 0, 1, 0, 1), ("a", 3, 3, 0, 1), ("b", 1, 2, 0, 1)},
        )

        def background(name):
            return self.proxy.mapFromSource(self.model.index_for_item(item(name))).data(
                QtCore.Qt.BackgroundRole
            )

        for name in ("root", "a", "b"):
            self.assertIsInstance(background(name), QtGui.QBrush)
        self.selection.set_selection([item("a1")])
        for name in ("root", "a"):
            self.assertIsInstance(background(name), QtGui.QBrush)
        self.assertIsNone(background("b"))

    def test_deep_external_selection_highlights_and_reveal_expands_ancestors(self):
        branches = ["depth" + str(i) for i in range(32)]
        self.source.tree = {"root": [branches[0], "side"], "side": [], "leaf": []}
        for parent, child in zip(branches, branches[1:] + ["leaf"]):
            self.source.tree[parent] = [child]
        self.source.notify(Change.reset())
        self.view.collapseAll()
        self.selection.set_selection([item("leaf")])
        flush()
        self.assertEqual(self.widget.selected_items(), [item("leaf")])
        for ancestor in ["root"] + branches:
            index = self.widget.index_for_item(item(ancestor))
            self.assertFalse(self.view.isExpanded(self.proxy.mapFromSource(index)))
            self.assertIsInstance(
                self.proxy.mapFromSource(index).data(QtCore.Qt.BackgroundRole), QtGui.QBrush
            )
        self.view.reveal_selection()
        flush()
        for ancestor in ["root"] + branches:
            with self.subTest(ancestor=ancestor):
                index = self.widget.index_for_item(item(ancestor))
                self.assertTrue(self.view.isExpanded(self.proxy.mapFromSource(index)))
                brush = self.proxy.mapFromSource(index).data(QtCore.Qt.BackgroundRole)
                self.assertIsInstance(brush, QtGui.QBrush)
                self.assertGreater(brush.color().alpha(), 0)
        for name in ("side", "leaf"):
            self.assertIsNone(
                self.proxy.mapFromSource(self.widget.index_for_item(item(name))).data(
                    QtCore.Qt.BackgroundRole
                )
            )
        root = self.proxy.mapFromSource(self.widget.index_for_item(item("root")))
        self.view.scrollTo(root, QtWidgets.QAbstractItemView.PositionAtTop)
        flush()
        point = QtCore.QPoint(
            self.view.viewport().width() - 8, self.view.visualRect(root).center().y()
        )
        highlighted = self.view.viewport().grab().toImage().pixelColor(point)
        self.selection.set_selection([])
        flush()
        self.assertNotEqual(self.view.viewport().grab().toImage().pixelColor(point), highlighted)
        for ancestor in ["root"] + branches:
            self.assertIsNone(
                self.proxy.mapFromSource(self.widget.index_for_item(item(ancestor))).data(
                    QtCore.Qt.BackgroundRole
                )
            )

    def test_public_value_types(self):
        change = Change()
        self.assertEqual(change.type, Change.Type.RESET)
        change.type = Change.Type.ITEMS_CHANGED
        self.assertEqual(change.type, Change.Type.ITEMS_CHANGED)
        self.assertEqual(item("a").type_id, "outliner_extension")
        self.assertEqual(SceneItemId().type_id, "")
        self.assertEqual(item("a"), SceneItemId("test.source", "a"))
        self.assertNotEqual(item("a"), SceneItemId("other.source", "a"))
        self.assertEqual(len({item("a"), item("a")}), 1)
        self.assertFalse(SceneItemId())
        with self.assertRaises(ValueError):
            SceneItemId("", "a")

    def test_swap_disconnects_old_source_and_releases_python_adapters(self):
        replacement = Source("second")
        replacement.tree = {"root": ["new"], "new": []}
        self.widget.set_data_source(replacement)
        self.assertIs(self.widget.data_source(), replacement)
        self.assertFalse(self.widget.index_for_item(item("a")).isValid())
        self.assertTrue(self.widget.index_for_item(item("new")).isValid())
        resets = []
        self.model.modelReset.connect(lambda: resets.append(True))
        self.source.notify(Change.reset())
        self.assertEqual(resets, [])
        ref = weakref.ref(replacement)
        del replacement
        gc.collect()
        self.assertIsNotNone(ref())
        self.widget.set_data_source(None)
        gc.collect()
        self.assertIsNone(ref())
        self.assertEqual(self.model.rowCount(), 0)

    def test_selection_swap_and_filtering_do_not_write_old_selection(self):
        replacement = LocalSelection()
        replacement.set_selection([item("leaf"), item("b")])
        self.widget.set_selection_context(replacement)
        self.assertEqual(set(self.widget.selected_items()), set(replacement.items))
        before = replacement.mutations
        field = self.widget.findChild(QtWidgets.QLineEdit)
        field.setText("leaf")
        QtCore.QMetaObject.invokeMethod(
            self.widget, "on_filter_timeout", QtCore.Qt.DirectConnection
        )
        self.assertEqual(self.widget.selected_items(), [item("leaf")])
        self.assertEqual(replacement.mutations, before)
        self.source.tree["a"].append("newleaf")
        self.source.tree["newleaf"] = []
        self.source.notify(Change.added([item("a")]))
        self.assertTrue(
            self.proxy.mapFromSource(self.widget.index_for_item(item("newleaf"))).isValid()
        )
        field.clear()
        QtCore.QMetaObject.invokeMethod(
            self.widget, "on_filter_timeout", QtCore.Qt.DirectConnection
        )
        index = self.proxy.mapFromSource(self.widget.index_for_item(item("a")))
        self.view.selectionModel().select(
            index, QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows
        )
        self.assertEqual(replacement.items, [item("a")])
        self.assertEqual(self.selection.mutations, 0)

    def test_column_delegates_refresh_choices_and_return_to_plain_editor(self):
        for choice in ("first", "second", ""):
            with self.subTest(choice=choice):
                self.widget.set_data_source(Source(choice))
                index = self.proxy.mapFromSource(self.widget.index_for_item(item("a")))
                index = index.sibling(index.row(), 1)
                delegate = self.view.itemDelegateForColumn(1) or self.view.itemDelegate()
                editor = delegate.createEditor(self.view, QtWidgets.QStyleOptionViewItem(), index)
                self.addCleanup(editor.deleteLater)
                if choice:
                    self.assertIsInstance(editor, QtWidgets.QComboBox)
                    self.assertEqual([editor.itemText(i) for i in range(editor.count())], [choice])
                else:
                    self.assertIsInstance(editor, QtWidgets.QLineEdit)

    def test_qt_parent_destruction_releases_retained_source_and_selection(self):
        parent = QtWidgets.QWidget()
        source = Source()
        selection = LocalSelection()
        source_ref, selection_ref = weakref.ref(source), weakref.ref(selection)
        widget = SceneOutliner(source, selection, parent)
        del source, selection
        gc.collect()
        self.assertIsNotNone(source_ref())
        parent.deleteLater()
        flush()
        with self.assertRaises(RuntimeError):
            widget.parentWidget()
        gc.collect()
        self.assertIsNone(source_ref())
        self.assertIsNone(selection_ref())
