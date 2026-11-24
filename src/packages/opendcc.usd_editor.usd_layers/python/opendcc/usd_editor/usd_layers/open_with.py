# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtWidgets
import opendcc.core as dcc_core
from opendcc.i18n import i18n

_handlers = {}


def register_handler(name, label, callback, *, can_open=None):
    """Add or replace an Open With action by name.

    The callback and optional can_open predicate receive the clicked Sdf.Layer.
    The built-in names are "new_opendcc_instance" and "add_to_session".
    """
    _handlers[name] = (label, callback, can_open)


def unregister_handler(name):
    """Remove an Open With action by name."""
    _handlers.pop(name, None)


def _can_open_in_new_instance(layer):
    return (
        not layer.anonymous and bool(layer.realPath) and QtCore.QFileInfo(layer.realPath).isFile()
    )


def _open_in_new_instance(layer):
    executable = QtCore.QCoreApplication.applicationFilePath()
    started, _ = QtCore.QProcess.startDetached(executable, ["--", layer.realPath])
    if not started:
        QtWidgets.QMessageBox.warning(
            dcc_core.Application.instance().get_main_window(),
            i18n("panels.usd_layers", "Open With"),
            i18n("panels.usd_layers", "Could not start OpenDCC for:\n{0}").format(layer.realPath),
        )


def _add_to_session(layer):
    dcc_core.Application.instance().get_session().open_stage(layer)


def _add_menu(menu, layer):
    if not _handlers:
        return

    submenu = QtWidgets.QMenu(i18n("panels.usd_layers", "Open With"), menu)
    menu.addMenu(submenu)
    for label, callback, can_open in tuple(_handlers.values()):
        action = submenu.addAction(label)
        action.setEnabled(can_open is None or can_open(layer))
        action.triggered.connect(lambda checked=False, callback=callback: callback(layer))
    submenu.setEnabled(any(action.isEnabled() for action in submenu.actions()))
