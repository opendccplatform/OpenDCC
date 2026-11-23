# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore, QtGui
from opendcc.ui.scene_outliner import Change, SceneOutliner, SceneOutlinerModel
from scene_outliner_test_utils import Source, item, flush, SceneOutlinerTest, shiboken


class TestDragData(SceneOutlinerTest):
    def test_drag_validation_stops_after_callback_invalidation(self):
        class CheckingSource(Source):
            callback = None

            def has_item(self, child):
                callback, self.callback = self.callback, None
                if callback:
                    callback()
                return super().has_item(child)

        for action in ("none", "clear", "replace", "reset", "delete_source", "delete_model"):
            with self.subTest(action=action):
                source = CheckingSource()
                model = SceneOutlinerModel()
                model.set_data_source(source)
                items = [item("a"), item("b")]
                mime = model.mimeData([model.ensure_index_for_item(row) for row in items])
                source.callback = {
                    "none": lambda: None,
                    "clear": lambda: model.set_data_source(None),
                    "replace": lambda: model.set_data_source(Source()),
                    "reset": lambda: source.notify(Change.reset()),
                    "delete_source": lambda: shiboken.delete(source),
                    "delete_model": lambda: shiboken.delete(model),
                }[action]
                try:
                    self.assertEqual(
                        model.items_for_mime_data(mime), items if action == "none" else []
                    )
                    self.assertIsNone(source.callback)
                finally:
                    if shiboken.isValid(model):
                        shiboken.delete(model)

    def test_mime_creation_stops_after_provider_callback_invalidation(self):
        class DragSource(Source):
            callback = None
            query = ""

            def invoke(self, query):
                if query == self.query and self.callback:
                    callback, self.callback = self.callback, None
                    callback()

            def drag_mime_type(self):
                self.invoke("type")
                return "application/x-outliner-test"

            def drag_mime_data(self, items):
                self.invoke("payload")
                return QtCore.QByteArray(b"custom drag")

            def column_text(self, child, column):
                self.invoke("text")
                return super().column_text(child, column)

        for query in ("type", "payload", "text"):
            for action in ("none", "clear", "replace", "reset", "delete_source", "delete_model"):
                with self.subTest(query=query, action=action):
                    source = DragSource()
                    model = SceneOutlinerModel()
                    model.set_data_source(source)
                    index = model.ensure_index_for_item(item("a"))
                    source.query = query
                    source.callback = {
                        "none": lambda: None,
                        "clear": lambda: model.set_data_source(None),
                        "replace": lambda: model.set_data_source(Source()),
                        "reset": lambda: source.notify(Change.reset()),
                        "delete_source": lambda: shiboken.delete(source),
                        "delete_model": lambda: shiboken.delete(model),
                    }[action]
                    try:
                        mime = model.mimeData([index])
                        self.assertIsNone(source.callback)
                        if action == "none":
                            self.assertEqual(
                                bytes(mime.data("application/x-outliner-test")), b"custom drag"
                            )
                            target = QtGui.QStandardItemModel()
                            self.assertTrue(
                                target.dropMimeData(
                                    mime, QtCore.Qt.CopyAction, 0, 0, QtCore.QModelIndex()
                                )
                            )
                            self.assertEqual(target.item(0, 0).text(), "a")
                            self.assertEqual(model.items_for_mime_data(mime), [item("a")])
                        else:
                            self.assertIsNone(mime)
                    finally:
                        if shiboken.isValid(model):
                            shiboken.delete(model)

    def test_drag_handlers_stop_after_provider_callback_invalidation(self):
        class DragSource(Source):
            callback = None
            query = ""

            def invoke(self, query):
                if self.callback and self.query == query:
                    callback, self.callback = self.callback, None
                    callback()

            def can_reparent(self):
                self.invoke("capability")
                return True

            def has_item(self, child):
                self.invoke("validation")
                return super().has_item(child)

        for handler in ("dragEnterEvent", "dragMoveEvent", "dropEvent"):
            for query in ("capability", "validation"):
                for action in (
                    "clear",
                    "replace",
                    "reset",
                    "delete_source",
                    "delete_model",
                    "delete_widget",
                ):
                    with self.subTest(handler=handler, query=query, action=action):
                        source = DragSource()
                        widget = SceneOutliner(source)
                        model, view = widget.model(), widget.tree_view()
                        widget.show()
                        flush()
                        mime = model.mimeData([widget.index_for_item(item("a"))])
                        source.query = query
                        source.callback = {
                            "clear": lambda: widget.set_data_source(None),
                            "replace": lambda: widget.set_data_source(Source()),
                            "reset": lambda: source.notify(Change.reset()),
                            "delete_source": lambda: shiboken.delete(source),
                            "delete_model": lambda: shiboken.delete(model),
                            "delete_widget": lambda: shiboken.delete(widget),
                        }[action]
                        event_type = {
                            "dragEnterEvent": QtGui.QDragEnterEvent,
                            "dragMoveEvent": QtGui.QDragMoveEvent,
                            "dropEvent": QtGui.QDropEvent,
                        }[handler]
                        point = (
                            QtCore.QPointF(4, 4) if handler == "dropEvent" else QtCore.QPoint(4, 4)
                        )
                        event = event_type(
                            point,
                            QtCore.Qt.CopyAction,
                            mime,
                            QtCore.Qt.LeftButton,
                            QtCore.Qt.NoModifier,
                        )
                        try:
                            getattr(view, handler)(event)
                            self.assertIsNone(source.callback)
                            self.assertFalse(event.isAccepted())
                        finally:
                            if shiboken.isValid(widget):
                                shiboken.delete(widget)

    def test_reentrant_mime_creation_preserves_the_latest_drag(self):
        class DragSource(Source):
            callback = None

            def drag_mime_type(self):
                callback, self.callback = self.callback, None
                if callback:
                    callback()
                return "application/x-outliner-test"

        source = DragSource()
        model = SceneOutlinerModel()
        model.set_data_source(source)
        index = model.ensure_index_for_item(item("a"))
        payloads = []
        source.callback = lambda: payloads.append(model.mimeData([index]))
        try:
            self.assertIsNone(model.mimeData([index]))
            self.assertEqual(len(payloads), 1)
            self.assertEqual(model.items_for_mime_data(payloads[0]), [item("a")])
        finally:
            shiboken.delete(model)

    def test_export_only_source_can_start_a_drag_without_accepting_drops(self):
        class ExportSource(Source):
            armed = False
            captured = None

            def drag_mime_type(self):
                return "application/x-outliner-export"

            def drag_mime_data(self, items):
                if self.armed:
                    self.armed = False
                    self.captured = list(items)
                    # End before QDrag's native input loop; the queued drag reached the backend.
                    self.notify(Change.reset())
                return QtCore.QByteArray(b"exported")

        source = ExportSource()
        self.widget.set_data_source(source)
        model, view = self.widget.model(), self.widget.tree_view()
        view.expandAll()
        flush()
        index = model.ensure_index_for_item(item("a"))
        self.assertTrue(model.flags(index) & QtCore.Qt.ItemIsDragEnabled)
        self.assertFalse(model.flags(index) & QtCore.Qt.ItemIsDropEnabled)
        mime = model.mimeData([index])
        self.assertEqual(bytes(mime.data("application/x-outliner-export")), b"exported")
        event = QtGui.QDragEnterEvent(
            QtCore.QPoint(), QtCore.Qt.CopyAction, mime, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier
        )
        view.dragEnterEvent(event)
        self.assertFalse(event.isAccepted())
        view.selectionModel().select(
            view.model().mapFromSource(index),
            QtCore.QItemSelectionModel.ClearAndSelect | QtCore.QItemSelectionModel.Rows,
        )
        source.armed = True
        view.startDrag(QtCore.Qt.CopyAction)
        flush()
        self.assertEqual(source.captured, [item("a")])
