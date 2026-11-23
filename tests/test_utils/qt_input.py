# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from contextlib import contextmanager

from Qt import QtCore, QtGui


def _send_mouse_event(widget, event_type, position, button, buttons, modifiers):
    event = QtGui.QMouseEvent(
        event_type,
        QtCore.QPointF(position),
        QtCore.QPointF(widget.mapToGlobal(position)),
        button,
        buttons,
        modifiers,
    )
    QtCore.QCoreApplication.sendEvent(widget, event)


def mouse_move(widget, position, modifiers=QtCore.Qt.NoModifier, *, buttons=QtCore.Qt.NoButton):
    """Send a move in widget coordinates without changing the platform cursor."""
    _send_mouse_event(
        widget, QtCore.QEvent.MouseMove, position, QtCore.Qt.NoButton, buttons, modifiers
    )


def mouse_click(widget, position, modifiers=QtCore.Qt.NoModifier, *, button=QtCore.Qt.LeftButton):
    """Send a press and release directly to the widget."""
    _send_mouse_event(widget, QtCore.QEvent.MouseButtonPress, position, button, button, modifiers)
    _send_mouse_event(
        widget, QtCore.QEvent.MouseButtonRelease, position, button, QtCore.Qt.NoButton, modifiers
    )


def drag_events(mime, position, action=QtCore.Qt.CopyAction, modifiers=QtCore.Qt.NoModifier):
    """Create drag and drop events for delivery to a widget without starting a native drag."""
    return (
        QtGui.QDragEnterEvent(position, action, mime, QtCore.Qt.LeftButton, modifiers),
        QtGui.QDragMoveEvent(position, action, mime, QtCore.Qt.LeftButton, modifiers),
        QtGui.QDropEvent(QtCore.QPointF(position), action, mime, QtCore.Qt.LeftButton, modifiers),
    )


@contextmanager
def offscreen_cursor_at(widget, position):
    """Temporarily place the offscreen cursor for code that reads QCursor.pos()."""
    if QtGui.QGuiApplication.platformName() != "offscreen":
        raise RuntimeError("Cursor positioning requires Qt's offscreen platform")
    previous = QtGui.QCursor.pos()
    QtGui.QCursor.setPos(widget.mapToGlobal(position))
    try:
        yield
    finally:
        QtGui.QCursor.setPos(previous)
