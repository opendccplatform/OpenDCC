# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets
import opendcc.core as dcc_core
from pxr import Sdf
from opendcc import cmds


def _main_window():
    return dcc_core.Application.instance().get_main_window()


def _file_filter():
    return dcc_core.Application.get_file_extensions()


def _add_recent_file(identifier):
    # Best-effort; the recent-files helper lives in the (UI-only) file menu module.
    try:
        from opendcc.file_menu import add_recent_file

        add_recent_file(identifier)
    except Exception:
        pass


def load_sublayer(parent_identifier, parent_widget=None):
    """Prompt for one or more existing layer files and insert them as sublayers of
    ``parent_identifier`` (at the top of its sublayer list, in the chosen order)."""
    parent_layer = Sdf.Layer.Find(parent_identifier) or Sdf.Layer.FindOrOpen(parent_identifier)
    if not parent_layer:
        return False

    files, _ = QtWidgets.QFileDialog.getOpenFileNames(
        parent_widget or _main_window(),
        "Load Sublayer(s)",
        parent_layer.realPath or "",
        _file_filter(),
    )
    if not files:
        return False

    inserted = False
    index = 0
    for path in files:
        sublayer = Sdf.Layer.FindOrOpen(path)
        if not sublayer:
            continue
        identifier = sublayer.identifier
        if identifier in parent_layer.subLayerPaths:
            continue
        if cmds.add_sublayer(parent_identifier, identifier, index=index).is_successful():
            index += 1
            inserted = True
    return inserted


def add_anonymous_sublayer(parent_identifier):
    """Create a new empty anonymous layer and insert it at the top of the parent's sublayers."""
    parent_layer = Sdf.Layer.Find(parent_identifier) or Sdf.Layer.FindOrOpen(parent_identifier)
    if not parent_layer:
        return False
    return cmds.add_sublayer(parent_identifier, "layer.usda", anonymous=True).is_successful()


def save_layer(identifier, parent_widget=None):
    """Save a file-backed layer in place; anonymous layers are routed to ``save_layer_as``."""
    layer = Sdf.Layer.Find(identifier)
    if not layer:
        return False
    if layer.anonymous:
        return save_layer_as(identifier, parent_widget)
    if not cmds.save_layer(identifier).is_successful():
        return False
    _add_recent_file(layer.identifier)
    return True


def save_layer_as(identifier, parent_widget=None):
    """Write a layer's contents to a chosen file. For anonymous layers, any sublayer reference
    to the old (anonymous) identifier in the current stage's layer stack is repointed to the new
    file so the saved layer stays wired into the composition."""
    layer = Sdf.Layer.Find(identifier)
    if not layer:
        return False

    path, _ = QtWidgets.QFileDialog.getSaveFileName(
        parent_widget or _main_window(),
        "Save Layer As",
        layer.realPath or "",
        _file_filter(),
    )
    if not path:
        return False

    result = cmds.save_layer(identifier, path=path)
    if not result.is_successful():
        QtWidgets.QMessageBox.warning(
            parent_widget or _main_window(),
            "Save Layer As",
            "Could not save layer:\n{0}".format(path),
        )
        return False
    saved_identifier = result.get_result()
    stage = dcc_core.Application.instance().get_session().get_current_stage()
    has_parent = stage and any(
        Sdf.Layer.FindRelativeToLayer(parent, path) == layer
        for parent in stage.GetUsedLayers()
        for path in parent.subLayerPaths
    )
    if layer.anonymous and has_parent:
        if not cmds.retarget_layer(identifier, saved_identifier).is_successful():
            return False
        if not cmds.save_layer(saved_identifier).is_successful():
            return False
    _add_recent_file(saved_identifier)
    return True


def reload_layer(identifier, parent_widget=None):
    layer = Sdf.Layer.Find(identifier)
    if not layer:
        return False
    if layer.dirty:
        answer = QtWidgets.QMessageBox.question(
            parent_widget or _main_window(),
            "Confirm Reload Layer",
            "Reload this layer from disk and discard its changes?",
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.Cancel,
            QtWidgets.QMessageBox.Cancel,
        )
        if answer != QtWidgets.QMessageBox.Yes:
            return False
    return cmds.reload_layer(identifier).is_successful()


def save_all_layers(parent_widget=None):
    """Save every dirty, file-backed layer in the current stage's layer stack. Anonymous layers
    are skipped (they need an explicit Save As to choose a path)."""
    stage = dcc_core.Application.instance().get_session().get_current_stage()
    if not stage:
        return False
    saved = False
    for layer in stage.GetUsedLayers():
        if layer.anonymous or not layer.dirty:
            continue
        if cmds.save_layer(layer.identifier).is_successful():
            _add_recent_file(layer.identifier)
            saved = True
    return saved
