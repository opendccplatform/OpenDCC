# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
import opendcc.core as dcc_core
from opendcc.i18n import i18n


def viewport_layout_button(parent=None):
    button = QtWidgets.QToolButton(parent)
    button.setObjectName("viewport_layout_button")
    button.setPopupMode(QtWidgets.QToolButton.InstantPopup)
    button.setFocusPolicy(QtCore.Qt.NoFocus)
    button.setIcon(QtGui.QIcon(":/icons/layout"))
    button.setToolTip(i18n("toolbars.tools", "Viewport Layout (Space)"))
    app = dcc_core.Application.instance()

    def update_menu():
        view = app.get_active_view()
        widget = view.get_widget() if view else None
        pane = widget.parentWidget() if widget else None
        panel = pane
        while panel and not isinstance(panel, dcc_core.ViewportPanel):
            panel = panel.parentWidget()
        menu = None
        if panel and panel.isVisible():
            menu = pane.findChild(QtWidgets.QMenu, "viewport_layout_menu")
        button.setMenu(menu)
        button.setEnabled(menu is not None)

    handles = [
        (event, app.register_event_callback(event, update_menu))
        for event in ("active_view_changed", "active_view_scene_context_changed")
    ]

    def unregister_callbacks():
        for event, handle in handles:
            app.unregister_event_callback(event, handle)

    button.destroyed.connect(unregister_callbacks)
    update_menu()
    return button
