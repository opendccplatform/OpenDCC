# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore, QtGui, QtWidgets
from opendcc.ui.scene_outliner import (
    Change,
    Column,
    DataSource,
    SceneOutlinerModel,
    SceneOutlinerTreeView,
)
from scene_outliner_test_utils import (
    Source,
    item,
    flush,
    CallbackSource,
    SceneOutlinerTest,
    shiboken,
)


class TestModelCallbacks(SceneOutlinerTest):
    def test_hierarchy_queries_can_destroy_the_model(self):
        class RemovingSource(Source):
            callback = None

            def run_callback(self):
                callback, self.callback = self.callback, None
                if callback:
                    callback()

            def children(self, parent):
                self.run_callback()
                return super().children(parent)

            def has_item(self, child):
                self.run_callback()
                return super().has_item(child)

            def has_children(self, parent):
                self.run_callback()
                return super().has_children(parent)

        for query in ("row_count", "index", "materialize", "has_children"):
            for delete_parent in (False, True):
                with self.subTest(query=query, delete_parent=delete_parent):
                    owner = QtCore.QObject()
                    model = SceneOutlinerModel(owner)
                    source = RemovingSource()
                    source.setParent(owner)
                    model.set_data_source(source)
                    root = model.index(0, 0)
                    source.callback = lambda: shiboken.delete(owner if delete_parent else model)
                    if query == "row_count":
                        self.assertEqual(model.rowCount(root), 0)
                    elif query == "index":
                        self.assertFalse(model.index(0, 0, root).isValid())
                    elif query == "materialize":
                        self.assertFalse(model.ensure_index_for_item(item("leaf")).isValid())
                    else:
                        self.assertFalse(model.hasChildren(root))
                    self.assertFalse(shiboken.isValid(model))

    def test_item_notifications_stop_after_model_invalidation(self):
        for items in ([], [item("a"), item("b")]):
            for action in ("delete", "reset"):
                with self.subTest(items=items, action=action):
                    source = Source()
                    model = SceneOutlinerModel()
                    model.set_data_source(source)
                    model.ensure_index_for_item(item("leaf"))
                    model.ensure_index_for_item(item("b"))
                    changes = []

                    def changed(*args):
                        changes.append(True)
                        if action == "delete":
                            shiboken.delete(model)
                        else:
                            source.notify(Change.reset())

                    model.dataChanged.connect(changed)
                    source.notify(Change.changed(items))
                    self.assertEqual(changes, [True])

    def test_model_queries_stop_after_callback_invalidation(self):
        for query in ("state", "enabled", "rename", "reparent"):
            for action in ("clear", "replace", "reset", "delete_source", "delete_model"):
                with self.subTest(query=query, action=action):
                    source = CallbackSource()
                    model = SceneOutlinerModel()
                    model.set_data_source(source)
                    column = 2 if query in ("state", "enabled") else 0
                    index = model.ensure_index_for_item(item("a"), column)
                    source.callbacks[query] = {
                        "clear": lambda: model.set_data_source(None),
                        "replace": lambda: model.set_data_source(Source()),
                        "reset": lambda: source.notify(Change.reset()),
                        "delete_source": lambda: shiboken.delete(source),
                        "delete_model": lambda: shiboken.delete(model),
                    }[action]
                    if query in ("state", "enabled"):
                        self.assertIsNone(model.data(index, QtCore.Qt.DecorationRole))
                        expected = ["state"] if query == "state" else ["state", "enabled"]
                    else:
                        self.assertEqual(model.flags(index), QtCore.Qt.NoItemFlags)
                        expected = ["rename"] if query == "rename" else ["rename", "reparent"]
                    self.assertEqual(source.calls, expected)

    def test_edit_callbacks_can_destroy_the_model_or_its_parent(self):
        for operation in ("edit", "toggle"):
            for delete_parent in (False, True):
                with self.subTest(operation=operation, delete_parent=delete_parent):
                    source = CallbackSource()
                    owner = QtCore.QObject()
                    model = SceneOutlinerModel(owner)
                    model.set_data_source(source)
                    index = model.ensure_index_for_item(item("a"), 1 if operation == "edit" else 2)
                    changes = []
                    model.dataChanged.connect(lambda *args: changes.append(args))
                    source.callbacks[operation] = lambda: shiboken.delete(
                        owner if delete_parent else model
                    )
                    if operation == "edit":
                        self.assertTrue(model.setData(index, "new", QtCore.Qt.EditRole))
                    else:
                        model.toggle_column(index)
                    self.assertFalse(shiboken.isValid(model))
                    self.assertFalse(changes)

    def test_edit_callbacks_do_not_notify_stale_indexes(self):
        for operation in ("edit", "toggle"):
            for action in ("none", "clear", "replace", "reset", "remove", "reorder"):
                with self.subTest(operation=operation, action=action):
                    source = CallbackSource()
                    model = SceneOutlinerModel()
                    model.set_data_source(source)
                    index = model.ensure_index_for_item(item("a"), 1 if operation == "edit" else 2)
                    persistent = QtCore.QPersistentModelIndex(index)
                    changed = []
                    model.dataChanged.connect(
                        lambda first, last, *args: changed.append(model.item_for_index(first))
                    )

                    def change_rows():
                        if action == "remove":
                            source.tree["root"].remove("a")
                            del source.tree["a"]
                            source.notify(Change.removed([item("root")]))
                        else:
                            source.tree["root"].reverse()
                            source.notify(Change.added([item("root")]))

                    source.callbacks[operation] = {
                        "none": lambda: None,
                        "clear": lambda: model.set_data_source(None),
                        "replace": lambda: model.set_data_source(Source()),
                        "reset": lambda: source.notify(Change.reset()),
                        "remove": change_rows,
                        "reorder": change_rows,
                    }[action]
                    if operation == "edit":
                        self.assertTrue(model.setData(index, "new", QtCore.Qt.EditRole))
                    else:
                        model.toggle_column(index)
                    if action == "none":
                        self.assertEqual(changed, [item("a")])
                        self.assertTrue(persistent.isValid())
                    elif action == "reorder":
                        self.assertTrue(persistent.isValid())
                        self.assertEqual(persistent.row(), 1)
                        self.assertNotIn(item("a"), changed)
                    else:
                        self.assertFalse(persistent.isValid())
                        self.assertNotIn(item("a"), changed)

    def test_data_sources_define_columns_without_model_defaults(self):
        class NoColumns(Source):
            def columns(self):
                return DataSource.columns(self)

        self.widget.set_data_source(NoColumns())
        self.assertEqual(self.widget.model().columnCount(), 0)
        self.assertEqual(self.widget.model().columns(), [])

        class LabelSource(Source):
            def columns(self):
                label = Column()
                label.id = "label"
                label.title = "Label"
                label.role = Column.Role.TREE
                return [label]

            def column_text(self, item, column):
                return "label:" + item.key if column == "label" else ""

        self.widget.set_data_source(LabelSource())
        model = self.widget.model()
        self.assertEqual(model.columnCount(), 1)
        self.assertEqual(model.columns()[0].id, "label")
        self.assertEqual(model.headerData(0, QtCore.Qt.Horizontal), "Label")
        self.assertEqual(model.ensure_index_for_item(item("a")).data(), "label:a")

    def test_native_model_and_view_expose_item_apis(self):
        model = self.widget.model()
        view = self.widget.tree_view()
        self.assertIsInstance(model, SceneOutlinerModel)
        self.assertIsInstance(view, SceneOutlinerTreeView)
        self.assertIs(model.data_source(), self.source)
        self.assertEqual(model.root_item(), item("root"))
        index = model.ensure_index_for_item(item("leaf"), 1)
        self.assertEqual(model.item_for_index(index), item("leaf"))
        self.assertEqual(len(model.columns()), 2)
        view.set_expanded_items([item("root"), item("a")])
        self.assertEqual(set(view.expanded_items()), {item("root"), item("a")})
        view.set_busy_items([item("a")])
        view.set_busy_items([])
        view.collapse_location(item("a"))
        self.assertNotIn(item("a"), view.expanded_items())
        view.expand_location(item("a"))
        self.assertIn(item("a"), view.expanded_items())

    def test_resync_queries_stop_after_model_or_source_destruction(self):
        class ResyncSource(Source):
            callback = None
            query = ""
            remaining = 0

            def invoke(self, name):
                if self.callback and name == self.query:
                    self.remaining -= 1
                    if self.remaining == 0:
                        callback, self.callback = self.callback, None
                        callback()

            def children(self, parent):
                self.invoke("children")
                return super().children(parent)

            def has_item(self, child):
                self.invoke("has_item")
                return super().has_item(child)

            def parent(self, child):
                self.invoke("parent")
                return super().parent(child)

        for update in ("insert", "remove", "layout"):
            queries = (
                ("children", "has_item", "parent")
                if update == "layout"
                else (
                    "children",
                    "has_item",
                )
            )
            for query in queries:
                for target in ("model", "source"):
                    with self.subTest(update=update, query=query, target=target):
                        source = ResyncSource()
                        model = SceneOutlinerModel()
                        model.set_data_source(source)
                        model.ensure_index_for_item(item("leaf"))
                        layouts = []

                        def layout_changed(*args):
                            root = model.index(0, 0)
                            layouts.append(
                                (
                                    model.rowCount(root),
                                    all(
                                        model.parent(model.index(row, 0, root)) == root
                                        for row in range(2)
                                    ),
                                )
                            )

                        model.layoutChanged.connect(layout_changed)
                        if update == "insert":
                            source.tree["a"].append("new")
                            source.tree["new"] = []
                            parent = item("a")
                        elif update == "remove":
                            source.tree["a"].clear()
                            del source.tree["leaf"]
                            parent = item("a")
                        else:
                            source.tree["root"].reverse()
                            parent = item("root")
                        source.query = query
                        source.remaining = 2 if update == "layout" and query != "parent" else 1
                        source.callback = lambda: shiboken.delete(
                            model if target == "model" else source
                        )
                        change = Change.removed if update == "remove" else Change.added
                        source.notify(change([parent]))
                        self.assertIsNone(source.callback)
                        if target == "model":
                            self.assertFalse(shiboken.isValid(model))
                        else:
                            if update == "layout":
                                self.assertEqual(layouts, [(2, True)])
                            flush()
                            self.assertIsNone(model.data_source())
                            self.assertEqual(model.rowCount(), 0)
                            shiboken.delete(model)

    def test_disabled_toggles_ignore_model_edits_and_mouse_clicks(self):
        class ToggleSource(CallbackSource):
            enabled = False
            interactive = True

            def __init__(self):
                super().__init__()
                self.edited = []

            def columns(self):
                columns = super().columns()
                columns[-1].interactive = self.interactive
                columns[-1].width = 30
                return columns

            def toggle_enabled(self, child, column):
                return self.enabled

            def toggle_column(self, child, column):
                self.edited.append(child)

        source = ToggleSource()
        self.widget.set_data_source(source)
        view, model = self.widget.tree_view(), self.widget.model()
        view.expandAll()
        flush()
        cell = model.ensure_index_for_item(item("a"), 2)
        model.toggle_column(cell)
        position = view.visualRect(view.model().mapFromSource(cell)).center()
        for event_type, buttons in (
            (QtCore.QEvent.MouseButtonPress, QtCore.Qt.LeftButton),
            (QtCore.QEvent.MouseButtonRelease, QtCore.Qt.NoButton),
        ):
            QtCore.QCoreApplication.sendEvent(
                view.viewport(),
                QtGui.QMouseEvent(
                    event_type,
                    QtCore.QPointF(position),
                    QtCore.Qt.LeftButton,
                    buttons,
                    QtCore.Qt.NoModifier,
                ),
            )
        self.assertEqual(source.edited, [])
        source.enabled = True
        model.toggle_column(cell)
        self.assertEqual(source.edited, [item("a")])
        source.interactive = False
        source.notify(Change.reset())
        model.toggle_column(model.ensure_index_for_item(item("a"), 2))
        self.assertEqual(source.edited, [item("a")])

    def test_generic_text_edits_preserve_surrounding_whitespace(self):
        model = self.widget.model()
        cell = model.ensure_index_for_item(item("a"), 1)
        for value in ("  padded  ", "\ttext\n", "   ", ""):
            with self.subTest(value=value):
                self.assertTrue(model.setData(cell, value, QtCore.Qt.EditRole))
                self.assertEqual(self.source.edits[-1], (item("a"), "type", value))

    def test_header_layout_follows_the_source_schema(self):
        view = self.widget.tree_view()
        view.setColumnHidden(1, True)
        header = view.header()
        header.setSectionResizeMode(0, QtWidgets.QHeaderView.Interactive)
        header.resizeSection(0, 173)
        header.moveSection(0, 1)
        before = header.saveState()
        self.source.notify(Change.reset())
        flush()
        self.assertEqual(header.saveState(), before)
        self.assertTrue(view.isColumnHidden(1))
        self.widget.set_data_source(Source())
        flush()
        self.assertFalse(view.isColumnHidden(1))
