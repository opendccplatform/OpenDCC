# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore, QtGui
from opendcc.ui.scene_outliner import Change, SceneOutliner
from scene_outliner_test_utils import Source, item, flush, SceneOutlinerTest, shiboken


class TestDragGestures(SceneOutlinerTest):
    def test_queued_drag_uses_the_initiating_visible_selection(self):
        class DragSource(Source):
            captured = None
            armed = False

            def can_reparent(self):
                return True

            def drag_mime_type(self):
                return "application/x-outliner-test"

            def drag_mime_data(self, items):
                if self.armed:
                    self.captured = list(items)
                    self.armed = False
                    if change == "reentrant":
                        view.startDrag(QtCore.Qt.CopyAction)
                    self.notify(Change.reset())
                return QtCore.QByteArray()

        for change in (
            "selection",
            "hidden_row",
            "hidden_parent",
            "removed",
            "reset",
            "replace",
            "delete_model",
            "delete_widget",
            "reentrant",
        ):
            with self.subTest(change=change):
                source = DragSource()
                widget = SceneOutliner(source)
                model, view = widget.model(), widget.tree_view()
                widget.show()
                view.expandAll()
                flush()
                proxy = view.model()
                a, b, leaf = [
                    proxy.mapFromSource(widget.index_for_item(item(key)))
                    for key in ("a", "b", "leaf")
                ]
                select = QtCore.QItemSelectionModel.Select | QtCore.QItemSelectionModel.Rows
                if change in ("hidden_row", "hidden_parent"):
                    view.selectionModel().select(leaf, select)
                    view.selectionModel().select(b, select)
                    hidden = leaf if change == "hidden_row" else a
                    view.setRowHidden(hidden.row(), hidden.parent(), True)
                    expected = [item("b")]
                else:
                    view.selectionModel().select(a, select)
                    expected = [item("a")]
                widget.setUpdatesEnabled(False)
                source.armed = True
                try:
                    view.startDrag(QtCore.Qt.CopyAction)
                    if change == "selection":
                        view.selectionModel().select(
                            b, QtCore.QItemSelectionModel.ClearAndSelect | select
                        )
                    elif change == "removed":
                        source.tree["root"].remove("a")
                        del source.tree["a"]
                        source.notify(Change.removed([item("root")]))
                        expected = None
                    elif change == "reset":
                        source.notify(Change.reset())
                        expected = None
                    elif change == "replace":
                        widget.set_data_source(Source())
                        expected = None
                    elif change == "delete_model":
                        shiboken.delete(model)
                        expected = None
                    elif change == "delete_widget":
                        shiboken.delete(widget)
                        expected = None
                    flush()
                    self.assertEqual(source.captured, expected)
                finally:
                    source.armed = False
                    if shiboken.isValid(widget):
                        shiboken.delete(widget)

    def test_outgoing_drag_stops_after_preview_callback_invalidation(self):
        class DragSource(Source):
            callback = None
            query = ""
            counts = None

            def can_reparent(self):
                return True

            def invoke(self, query, threshold):
                if self.counts is not None:
                    self.counts[query] += 1
                    if self.query == query and self.counts[query] == threshold:
                        callback, self.callback = self.callback, None
                        self.counts = None
                        callback()

            def column_text(self, child, column):
                self.invoke("text", 3)
                return super().column_text(child, column)

            def icon(self, child):
                self.invoke("icon", 2)
                return QtGui.QIcon()

        for query in ("text", "icon"):
            for action in (
                "clear",
                "replace",
                "reset",
                "delete_source",
                "delete_model",
                "delete_widget",
            ):
                with self.subTest(query=query, action=action):
                    source = DragSource()
                    widget = SceneOutliner(source)
                    model, view = widget.model(), widget.tree_view()
                    widget.show()
                    flush()
                    index = view.model().mapFromSource(widget.index_for_item(item("a")))
                    view.selectionModel().select(index, QtCore.QItemSelectionModel.ClearAndSelect)
                    widget.setUpdatesEnabled(False)
                    source.query = query
                    source.counts = {"text": 0, "icon": 0}
                    source.callback = {
                        "clear": lambda: widget.set_data_source(None),
                        "replace": lambda: widget.set_data_source(Source()),
                        "reset": lambda: source.notify(Change.reset()),
                        "delete_source": lambda: shiboken.delete(source),
                        "delete_model": lambda: shiboken.delete(model),
                        "delete_widget": lambda: shiboken.delete(widget),
                    }[action]
                    try:
                        view.startDrag(QtCore.Qt.CopyAction)
                        self.assertEqual(source.counts, {"text": 0, "icon": 0})
                        flush()
                        self.assertIsNone(source.callback)
                        if action == "delete_widget":
                            self.assertFalse(shiboken.isValid(widget))
                    finally:
                        if shiboken.isValid(widget):
                            shiboken.delete(widget)
