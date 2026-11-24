# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import opendcc.core as dcc_core
from opendcc import cmds
from opendcc.i18n import i18n
from . import open_with, utils
from opendcc.packaging import PackageEntryPoint
from Qt import QtWidgets
from opendcc.usd_ui_utils import add_layer_copy_actions
from pxr import Sdf

from ._usd_layers import UsdLayersModel, UsdLayersView
from .panel import UsdLayersPanel

PANEL_TYPE = "usd_layers"


def _current_stage():
    session = dcc_core.Application.instance().get_session()
    return session.get_current_stage() if session else None


def _on_context_menu(panel, menu, identifier, parent_identifier):
    """Build actions for the clicked layer occurrence and its owning parent."""
    layer = Sdf.Layer.Find(identifier)
    stage = _current_stage()
    if not layer or not stage:
        return

    parent_widget = dcc_core.Application.instance().get_main_window()
    identifier = layer.identifier

    is_root = stage.GetRootLayer() == layer
    is_muted = stage.IsLayerMuted(identifier)
    is_edit_target = stage.GetEditTarget().GetLayer() == layer
    is_anonymous = layer.anonymous

    parent_layer = Sdf.Layer.Find(parent_identifier) if parent_identifier else None
    index = -1
    sibling_count = 0
    if parent_layer:
        paths = list(parent_layer.subLayerPaths)
        sibling_count = len(paths)
        for i, path in enumerate(paths):
            if Sdf.Layer.FindRelativeToLayer(parent_layer, path) == layer:
                index = i
                break

    action = menu.addAction("Set Edit Target")
    action.setEnabled(not is_edit_target and not is_muted and stage.HasLocalLayer(layer))
    action.triggered.connect(lambda: cmds.set_edit_target(identifier))

    action = menu.addAction("Unmute Layer" if is_muted else "Mute Layer")
    action.setEnabled(not is_root)
    action.triggered.connect(lambda: cmds.mute_layer(identifier, mute=not is_muted))

    menu.addSeparator()

    action = menu.addAction("Add Sublayer")
    action.setEnabled(layer.permissionToEdit)
    action.triggered.connect(lambda: panel.view.add_sublayer_to(identifier))

    action = menu.addAction("Load Sublayer From File...")
    action.setEnabled(layer.permissionToEdit)
    action.triggered.connect(lambda: utils.load_sublayer(identifier, parent_widget))

    if parent_layer is not None and index >= 0:
        parent_id = parent_layer.identifier

        action = menu.addAction("Remove Layer")
        action.setEnabled(parent_layer.permissionToEdit)
        action.triggered.connect(lambda: cmds.remove_sublayer(parent_id, identifier))

        action = menu.addAction("Move Up")
        action.setEnabled(parent_layer.permissionToEdit and index > 0)
        action.triggered.connect(lambda: cmds.move_sublayer(parent_id, identifier, index - 1))

        action = menu.addAction("Move Down")
        action.setEnabled(parent_layer.permissionToEdit and index < sibling_count - 1)
        action.triggered.connect(lambda: cmds.move_sublayer(parent_id, identifier, index + 1))

    menu.addSeparator()

    action = menu.addAction("Save Layer")
    action.setEnabled(layer.dirty and not is_anonymous)
    action.triggered.connect(lambda: utils.save_layer(identifier, parent_widget))

    action = menu.addAction("Save Layer As...")
    action.triggered.connect(lambda: utils.save_layer_as(identifier, parent_widget))

    action = menu.addAction("Save All Layers")
    action.triggered.connect(lambda: utils.save_all_layers(parent_widget))

    action = menu.addAction("Reload Layer")
    action.setEnabled(not is_anonymous and layer.permissionToEdit)
    action.triggered.connect(lambda: utils.reload_layer(identifier, parent_widget))

    open_with._add_menu(menu, layer)

    menu.addSeparator()

    add_layer_copy_actions(menu, layer)


def _make_panel():
    panel = UsdLayersPanel()
    panel.context_menu_requested.connect(
        lambda menu, identifier, parent_identifier: _on_context_menu(
            panel, menu, identifier, parent_identifier
        )
    )
    return panel


class UsdLayersEntryPoint(PackageEntryPoint):
    def initialize(self, package):
        if not QtWidgets.QApplication.instance():
            return
        dcc_core.PanelFactory.instance().register_panel(
            PANEL_TYPE,
            _make_panel,
            i18n("panels", "USD Layers"),
            singleton=True,
            icon=":/icons/panel_layers",
        )
        open_with.register_handler(
            "new_opendcc_instance",
            i18n("panels.usd_layers", "New OpenDCC Instance"),
            open_with._open_in_new_instance,
            can_open=open_with._can_open_in_new_instance,
        )
        open_with.register_handler(
            "add_to_session", i18n("panels.usd_layers", "Add to Session"), open_with._add_to_session
        )

    def uninitialize(self, package):
        open_with.unregister_handler("add_to_session")
        open_with.unregister_handler("new_opendcc_instance")
        dcc_core.PanelFactory.instance().unregister_panel(PANEL_TYPE)
