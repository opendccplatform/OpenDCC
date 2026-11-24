# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtGui, QtWidgets
from pxr import Sdf


def add_layer_copy_actions(menu, layer):
    """Add identifier and resolved-path actions for a layer to a context menu."""

    def copy_text(text):
        clipboard = QtWidgets.QApplication.clipboard()
        if clipboard.supportsSelection():
            clipboard.setText(text, QtGui.QClipboard.Selection)
        clipboard.setText(text, QtGui.QClipboard.Clipboard)

    identifier = layer.identifier
    action = menu.addAction("Copy Layer Identifier")
    action.triggered.connect(lambda: copy_text(identifier))
    resolved_path = layer.realPath
    action = menu.addAction("Copy Layer Resolved Path")
    action.setEnabled(bool(resolved_path))
    action.triggered.connect(lambda: copy_text(resolved_path))


def reload_layers(layers, widget_parent):
    if any([layer.dirty for layer in layers]):
        buttons = QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel
        answer = QtWidgets.QMessageBox.question(
            widget_parent,
            "Confirm Reload Layer",
            "Layer you want to reload has changes. Do you still want to reload it and discard changes?",
            buttons=buttons,
            defaultButton=QtWidgets.QMessageBox.Yes,
        )
        if answer == QtWidgets.QMessageBox.Cancel:
            return
    for layer in layers:
        layer.Reload()


def remove_prims(prims_paths, stage, ask=True, messagebox_parent=None):
    buttons = QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel
    if len(prims_paths) > 1:
        buttons |= QtWidgets.QMessageBox.YesToAll
    with Sdf.ChangeBlock():
        for path in prims_paths:
            if ask:
                answer = QtWidgets.QMessageBox.question(
                    messagebox_parent,
                    "Confirm Prim Removal",
                    "Remove prim/prim edits (and any children) at {0}?".format(path),
                    buttons=buttons,
                    defaultButton=QtWidgets.QMessageBox.Yes,
                )
                if answer == QtWidgets.QMessageBox.Cancel:
                    return
                elif answer == QtWidgets.QMessageBox.YesToAll:
                    ask = False
            stage.RemovePrim(path)
