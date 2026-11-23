# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
import time
import unittest
from Qt import QtCore, QtWidgets, __binding__
from opendcc.ui.scene_outliner import Column, DataSource, SceneItemId, SceneOutliner, Selection

if __binding__ == "PySide6":
    import shiboken6 as shiboken
else:
    import shiboken2 as shiboken


def item(key):
    return SceneItemId("test.source", key)


def flush():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()
    QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)


class Source(DataSource):
    def __init__(self, choice="first"):
        super().__init__()
        self.tree = {"root": ["a", "b"], "a": ["leaf"], "b": [], "leaf": []}
        self.queries = []
        self.choice = choice
        self.edits = []

    def roots(self):
        return [item("root")]

    def children(self, parent):
        self.queries.append(parent.key)
        return [item(key) for key in self.tree.get(parent.key, [])]

    def parent(self, child):
        for parent, children in self.tree.items():
            if child.key in children:
                return item(parent)
        return SceneItemId()

    def has_item(self, child):
        return child.owner == "test.source" and child.key in self.tree

    def has_children(self, parent):
        return bool(self.tree.get(parent.key))

    def display_name(self, child):
        return child.key

    def can_rename(self, child):
        return self.has_item(child)

    def columns(self):
        name = Column()
        name.id = "name"
        name.title = "Name"
        name.role = Column.Role.TREE
        name.stretch = True
        name.editable = True
        type_column = Column()
        type_column.id = "type"
        type_column.title = "Type"
        type_column.editable = True
        type_column.choices = [self.choice] if self.choice else []
        return [name, type_column]

    def column_text(self, child, column):
        if column == "name":
            return self.display_name(child)
        if column == "type":
            return self.type_name(child)
        return ""

    def set_column_text(self, child, column, value):
        self.edits.append((child, column, value))
        return self.has_item(child)


class LocalSelection(Selection):
    def __init__(self):
        super().__init__()
        self.items = []
        self.mutations = 0

    def selected_items(self):
        return self.items

    def set_selection(self, items):
        self.items = list(items)
        self.mutations += 1
        self.notify()

    def add_to_selection(self, items):
        self.set_selection(self.items + items)

    def remove_from_selection(self, items):
        self.set_selection([item for item in self.items if item not in items])


def wait_for(predicate):
    deadline = time.monotonic() + 3
    while not predicate() and time.monotonic() < deadline:
        flush()
        time.sleep(0.005)
    return predicate()


class CallbackSource(Source):
    def __init__(self):
        super().__init__()
        self.callbacks = {}
        self.calls = []

    def columns(self):
        toggle = Column()
        toggle.id = "toggle"
        toggle.role = Column.Role.TOGGLE
        toggle.icon_on = ":/icons/eye_open"
        toggle.icon_off = ":/icons/eye_closed"
        return super().columns() + [toggle]

    def run_callback(self, name):
        self.calls.append(name)
        callback = self.callbacks.pop(name, None)
        if callback:
            callback()

    def toggle_state(self, child, column):
        self.run_callback("state")
        return True

    def toggle_enabled(self, child, column):
        self.run_callback("enabled")
        return True

    def can_rename(self, child):
        self.run_callback("rename")
        return True

    def can_reparent(self):
        self.run_callback("reparent")
        return True

    def set_column_text(self, child, column, value):
        self.run_callback("edit")
        return True

    def toggle_column(self, child, column):
        self.run_callback("toggle")


class ForestSource(Source):
    def __init__(self):
        super().__init__()
        self.root_names = ["a", "b"]
        self.tree["b"] = ["bleaf"]
        self.tree["bleaf"] = []

    def roots(self):
        return [item(name) for name in self.root_names]

    def parent(self, child):
        return SceneItemId() if child in self.roots() else super().parent(child)

    def matches_filter(self, child, filter_id):
        if not child:
            raise AssertionError("The internal root must not reach the backend")
        return child == item("a")


def begin_search(widget, query):
    widget.findChild(QtWidgets.QLineEdit).setText(query)
    for timer in widget.findChildren(QtCore.QTimer):
        if timer.isSingleShot():
            timer.timeout.emit()


class SceneOutlinerTest(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(QtWidgets.QApplication.instance())
        self.source = Source()
        self.selection = LocalSelection()
        self.widget = SceneOutliner(self.source, self.selection)
        self.widget.resize(500, 400)
        self.widget.show()
        self.addCleanup(self.destroy)
        flush()
        self.view = self.widget.findChild(QtWidgets.QTreeView)
        self.proxy = self.view.model()
        self.model = self.widget.model()

    def destroy(self):
        self.widget.close()
        self.widget.deleteLater()
        flush()
