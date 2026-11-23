# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from Qt import QtCore, QtGui, QtWidgets
from opendcc.ui.scene_outliner import Change, SceneOutliner
from scene_outliner_test_utils import Source, item, flush, SceneOutlinerTest, shiboken


class TestDragView(SceneOutlinerTest):
    def test_drag_move_does_not_query_the_provider_inside_qt(self):
        class DragSource(Source):
            armed = False
            capabilities = 0
            formats = 0

            def can_reparent(self):
                if self.armed:
                    self.capabilities += 1
                    if self.capabilities == 2:
                        self.callback()
                return True

            def drag_mime_type(self):
                if self.armed:
                    self.formats += 1
                    self.callback()
                return "application/x-outliner-test"

        for action in ("reset", "replace", "delete_source", "delete_model", "delete_widget"):
            with self.subTest(action=action):
                source = DragSource()
                widget = SceneOutliner(source)
                model, view = widget.model(), widget.tree_view()
                widget.resize(600, 400)
                widget.show()
                flush()
                mime = model.mimeData([widget.index_for_item(item("a"))])
                target = view.model().mapFromSource(widget.index_for_item(item("b")))
                position = view.visualRect(target).center()
                source.callback = {
                    "reset": lambda: source.notify(Change.reset()),
                    "replace": lambda: widget.set_data_source(Source()),
                    "delete_source": lambda: shiboken.delete(source),
                    "delete_model": lambda: shiboken.delete(model),
                    "delete_widget": lambda: shiboken.delete(widget),
                }[action]
                source.armed = True
                try:
                    for accepted in (True, False):
                        event = QtGui.QDragMoveEvent(
                            position,
                            QtCore.Qt.CopyAction,
                            mime,
                            QtCore.Qt.LeftButton,
                            QtCore.Qt.NoModifier,
                        )
                        view.dragMoveEvent(event)
                        self.assertEqual(event.isAccepted(), accepted)
                        if accepted:
                            self.assertEqual(source.capabilities, 1)
                    self.assertEqual(source.formats, 0)
                finally:
                    source.armed = False
                    if shiboken.isValid(widget):
                        shiboken.delete(widget)

    def test_drag_hit_testing_does_not_query_display_data_during_layout(self):
        class DragSource(Source):
            callback = None

            def can_reparent(self):
                return True

            def column_text(self, child, column):
                callback, self.callback = self.callback, None
                if callback:
                    callback()
                return super().column_text(child, column)

            def reparent(self, items, parent):
                self.edits.append((list(items), parent))

        for uniform in (True, False):
            for layout in ("scheduled", "font"):
                for handler in ("dragMoveEvent", "dropEvent"):
                    for action in ("replace", "delete_widget"):
                        with self.subTest(
                            uniform=uniform, layout=layout, handler=handler, action=action
                        ):
                            source, replacement = DragSource(), Source()
                            widget = SceneOutliner(source)
                            model, view = widget.model(), widget.tree_view()
                            widget.resize(600, 400)
                            widget.show()
                            flush()
                            mime = model.mimeData([widget.index_for_item(item("a"))])
                            target = view.model().mapFromSource(widget.index_for_item(item("b")))
                            position = view.visualRect(target).center()
                            widget.setUpdatesEnabled(False)
                            view.setUniformRowHeights(uniform)
                            if layout == "font":
                                font = view.font()
                                font.setPointSize(font.pointSize() + 1)
                                view.setFont(font)
                            else:
                                view.scheduleDelayedItemsLayout()
                            callback = {
                                "replace": lambda: widget.set_data_source(replacement),
                                "delete_widget": lambda: shiboken.delete(widget),
                            }[action]
                            source.callback = callback
                            event_type = (
                                QtGui.QDropEvent if handler == "dropEvent" else QtGui.QDragMoveEvent
                            )
                            point = QtCore.QPointF(position) if handler == "dropEvent" else position
                            event = event_type(
                                point,
                                QtCore.Qt.CopyAction,
                                mime,
                                QtCore.Qt.LeftButton,
                                QtCore.Qt.NoModifier,
                            )
                            try:
                                getattr(view, handler)(event)
                                self.assertIs(source.callback, callback)
                                self.assertIs(widget.data_source(), source)
                                self.assertTrue(event.isAccepted())
                                self.assertEqual(len(source.edits), int(handler == "dropEvent"))
                            finally:
                                source.callback = None
                                if shiboken.isValid(widget):
                                    shiboken.delete(widget)

    def test_drop_rejects_provider_replacement_during_geometry(self):
        class DragSource(Source):
            def can_reparent(self):
                return True

            def reparent(self, items, parent):
                self.edits.append((list(items), parent))

        class ReentrantDelegate(QtWidgets.QStyledItemDelegate):
            callback = None

            def sizeHint(self, option, index):
                if option.rect.width() < 0 and self.callback:
                    callback, self.callback = self.callback, None
                    callback()
                return QtCore.QSize(100, 20)

        source, replacement = DragSource(), Source()
        widget = SceneOutliner(source)
        model, view = widget.model(), widget.tree_view()
        delegate = ReentrantDelegate(view)
        view.setItemDelegate(delegate)
        widget.resize(600, 400)
        widget.show()
        flush()
        mime = model.mimeData([widget.index_for_item(item("a"))])
        target = view.model().mapFromSource(widget.index_for_item(item("b")))
        event = QtGui.QDropEvent(
            QtCore.QPointF(view.visualRect(target).center()),
            QtCore.Qt.CopyAction,
            mime,
            QtCore.Qt.LeftButton,
            QtCore.Qt.NoModifier,
        )
        delegate.callback = lambda: widget.set_data_source(replacement)
        view.scheduleDelayedItemsLayout()
        try:
            view.dropEvent(event)
            self.assertIsNone(delegate.callback)
            self.assertIs(widget.data_source(), replacement)
            self.assertFalse(event.isAccepted())
            self.assertEqual(source.edits, [])
        finally:
            shiboken.delete(widget)

    def test_replacing_the_viewport_recreates_the_drop_indicator(self):
        class DragSource(Source):
            def can_reparent(self):
                return True

        source = DragSource()
        widget = SceneOutliner(source)
        model, view = widget.model(), widget.tree_view()
        widget.resize(600, 400)
        widget.show()
        flush()
        mime = model.mimeData([widget.index_for_item(item("a"))])
        try:
            for _ in range(2):
                old_viewport = view.viewport()
                old_indicator = view.findChild(QtWidgets.QRubberBand)
                view.setViewport(QtWidgets.QWidget())
                self.assertFalse(shiboken.isValid(old_viewport))
                self.assertFalse(shiboken.isValid(old_indicator))
                view.dragLeaveEvent(QtGui.QDragLeaveEvent())
                target = view.model().mapFromSource(widget.index_for_item(item("b")))
                event = QtGui.QDragMoveEvent(
                    view.visualRect(target).center(),
                    QtCore.Qt.CopyAction,
                    mime,
                    QtCore.Qt.LeftButton,
                    QtCore.Qt.NoModifier,
                )
                view.dragMoveEvent(event)
                self.assertTrue(event.isAccepted())
                indicator = view.findChild(QtWidgets.QRubberBand)
                self.assertIs(indicator.parentWidget(), view.viewport())
                self.assertTrue(indicator.isVisible())
                source.notify(Change.changed([item("b")], False))
                self.assertFalse(indicator.isVisible())
        finally:
            shiboken.delete(widget)

    def test_drop_targets_and_indicator_cleanup(self):
        class DragSource(Source):
            enabled = True

            def can_reparent(self):
                return self.enabled

            def reparent(self, items, parent):
                self.edits.append((list(items), parent))

        source = DragSource()
        widget = SceneOutliner(source)
        model, view = widget.model(), widget.tree_view()
        widget.resize(600, 400)
        widget.show()
        flush()
        mime = model.mimeData([widget.index_for_item(item("a"))])
        target = view.model().mapFromSource(widget.index_for_item(item("b")))
        rect = view.visualRect(target)
        indicator = view.findChild(QtWidgets.QRubberBand)
        try:
            for position, parent in (
                (rect.center(), item("b")),
                (QtCore.QPoint(rect.center().x(), rect.top()), None),
                (QtCore.QPoint(20, view.viewport().height() - 10), item("root")),
            ):
                move = QtGui.QDragMoveEvent(
                    position,
                    QtCore.Qt.CopyAction,
                    mime,
                    QtCore.Qt.LeftButton,
                    QtCore.Qt.NoModifier,
                )
                view.dragMoveEvent(move)
                self.assertEqual(move.isAccepted(), parent is not None)
                self.assertEqual(indicator.isVisible(), parent is not None)
                drop = QtGui.QDropEvent(
                    QtCore.QPointF(position),
                    QtCore.Qt.CopyAction,
                    mime,
                    QtCore.Qt.LeftButton,
                    QtCore.Qt.NoModifier,
                )
                before = len(source.edits)
                view.dropEvent(drop)
                self.assertEqual(drop.isAccepted(), parent is not None)
                self.assertFalse(indicator.isVisible())
                self.assertEqual(len(source.edits), before + int(parent is not None))
                if parent is not None:
                    self.assertEqual(source.edits[-1], ([item("a")], parent))
            view.dragMoveEvent(move)
            self.assertTrue(indicator.isVisible())
            view.dragLeaveEvent(QtGui.QDragLeaveEvent())
            self.assertFalse(indicator.isVisible())
            source.enabled = False
            view.dragMoveEvent(move)
            self.assertFalse(move.isAccepted())
            self.assertFalse(indicator.isVisible())
        finally:
            shiboken.delete(widget)
