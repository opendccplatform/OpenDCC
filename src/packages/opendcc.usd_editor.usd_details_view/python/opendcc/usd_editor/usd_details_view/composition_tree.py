# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Tomorrow-Open-Source-Technology-1.0
# Modifications by OpenDCC contributors.
#
# Copyright 2016 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

from Qt import QtCore, QtGui, QtWidgets
from pxr import Usd, Tf
from opendcc.usd_ui_utils import add_layer_copy_actions, reload_layers
from opendcc.tree_ui_utils import HeaderVisibilityMenu
from opendcc.tree_ui_utils import add_branches
from opendcc.i18n import i18n

import opendcc.core as dcc_core


@add_branches(0)
class CompositionTreeWidget(QtWidgets.QTreeWidget):
    LAYER = i18n("panels.usd_details_view.composition_tree", "Layer")
    ARC_TYPE = i18n("panels.usd_details_view.composition_tree", "Arc type")
    ARC_PATH = i18n("panels.usd_details_view.composition_tree", "Arc path")
    HAS_SPEC = i18n("panels.usd_details_view.composition_tree", "Has spec")
    PATH = i18n("panels.usd_details_view.composition_tree", "Path")
    RES_PATH = i18n("panels.usd_details_view.composition_tree", "Resolved path")
    MUTED = i18n("panels.usd_details_view.composition_tree", "Muted")
    DIRTY = i18n("panels.usd_details_view.composition_tree", "Dirty")
    VERSION = i18n("panels.usd_details_view.composition_tree", "Version")

    columns = [LAYER, ARC_TYPE, ARC_PATH, HAS_SPEC, PATH, RES_PATH, MUTED, DIRTY, VERSION]

    def __init__(self, prim=None, stage=None, parent=None):
        QtWidgets.QTreeWidget.__init__(self, parent)
        self.setIconSize(QtCore.QSize(20, 20))
        self.muted_icon = QtGui.QIcon(":/icons/muted")
        self.unmuted_icon = QtGui.QIcon(":/icons/unmuted")
        self.checkmark_icon = QtGui.QIcon(":/icons/checkmark")
        self.layers_icon = QtGui.QIcon(":/icons/layers")
        self.reference_icon = QtGui.QIcon(":/icons/create_reference.png")
        self._listener_obj_chgd = None
        self._listener_edit_target_chgd = None
        self.destroyed.connect(lambda: self._revoke_notices())
        self.setAllColumnsShowFocus(True)

        self.setHeaderLabels(self.columns)

        self._stage = None
        self._prim = None
        self.app = dcc_core.Application.instance()

        self.itemDoubleClicked.connect(self.set_edit_target)

        self.update(prim, stage)
        self.apply_settings()

        self.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)

        self.itemSelectionChanged.connect(self.selection_changed)

    def _get_selected_layers(self):
        items = self.selectedItems()
        layers = set()
        for item in items:
            layers.add(item.layer)
        return layers

    def selection_changed(self):
        self.app.set_layer_selection(self._get_selected_layers())

    def layer_selected(self):
        selected_layers = self._get_selected_layers()
        layers = self.app.get_layer_selection()
        if selected_layers != layers:
            selection_blocker = QtCore.QSignalBlocker(self)
            iterator = QtWidgets.QTreeWidgetItemIterator(self)
            while iterator.value():
                item = iterator.value()
                item.setSelected(item.layer in layers)
                iterator += 1

    def apply_settings(self):
        self.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(self.open_context_menu)
        self.header().setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.header().customContextMenuRequested.connect(self.open_header_menu)
        self.hideColumn(self.columns.index(self.PATH))
        self.hideColumn(self.columns.index(self.RES_PATH))
        self.hideColumn(self.columns.index(self.VERSION))
        self.hideColumn(self.columns.index(self.MUTED))

    def _revoke_notices(self):
        if self._listener_obj_chgd:
            self._listener_obj_chgd.Revoke()
            self._listener_obj_chgd = None
        if self._listener_edit_target_chgd:
            self._listener_edit_target_chgd.Revoke()
            self._listener_edit_target_chgd = None

    def update(self, prim=None, stage=None):
        if stage != self._stage:
            self._revoke_notices()
            if stage:
                self._listener_obj_chgd = Tf.Notice.Register(
                    Usd.Notice.ObjectsChanged, self.on_object_changed, stage
                )
                self._listener_edit_target_chgd = Tf.Notice.Register(
                    Usd.Notice.StageEditTargetChanged, self.on_object_changed, stage
                )
        self._prim = prim
        self._stage = stage
        self._rebuild_tree()

    def on_object_changed(self, notice, stage):
        self._rebuild_tree()

    def _rebuild_tree(self):
        """Sets the contents of the composition tree view"""
        selection_blocker = QtCore.QSignalBlocker(self)
        self.clear()

        if not self._prim:
            return

        # Populate the treeview with items from the prim index.
        index = self._prim.GetPrimIndex()
        if index.IsValid():
            self._walk_nodes(self, index.rootNode)
        self.layer_selected()

    def _label_for_layer(self, layer):
        """For brevity, we display only the basename of layer paths."""
        return "~session~" if layer == self._stage.GetSessionLayer() else layer.GetDisplayName()

    def _walk_nodes(self, parent, node):
        """Create treeview items for all nodes in the composition index."""
        nodeItem = self._walk_sublayers(parent, node, node.layerStack.layerTree)
        for child in node.children:
            self._walk_nodes(nodeItem, child)

    # Create treeview items for all sublayers in the layer tree.
    def _walk_sublayers(self, parent, node, layerTree, sublayer=False):
        layer = layerTree.layer
        spec = layer.GetObjectAtPath(node.path)
        # fill up all columns for item
        item = QtWidgets.QTreeWidgetItem(
            parent,
            [
                self._label_for_layer(layer),
                "sublayer" if sublayer else node.arcType.displayName,
                str(node.GetPathAtIntroduction()),
                "yes" if bool(spec) else "no",
                layer.identifier,
                layer.realPath,
                "",
                "",
                str(layer.version),
            ],
        )
        # add some icons
        if node.arcType.displayName == "reference":
            item.setIcon(self.columns.index(self.LAYER), self.reference_icon)
        else:
            item.setIcon(self.columns.index(self.LAYER), self.layers_icon)

        if layer != self._stage.GetRootLayer():
            if self._stage.IsLayerMuted(layer.identifier):
                item.setIcon(self.columns.index(self.MUTED), self.muted_icon)
            else:
                item.setIcon(self.columns.index(self.MUTED), self.unmuted_icon)

        if layer.dirty:
            item.setIcon(self.columns.index(self.DIRTY), self.checkmark_icon)

        # attributes for selection:
        item.layer = layer
        item.spec = spec
        item.identifier = layer.identifier
        item.node = node

        # attributes for LayerStackContextMenu:
        if layer.realPath:
            item.layerPath = layer.realPath
        if spec:
            item.path = node.path

        item.setExpanded(True)
        item.setToolTip(0, layer.identifier)
        color = QtCore.Qt.darkGray
        if not spec or not node.CanContributeSpecs():
            for i in range(item.columnCount()):
                item.setForeground(i, color)

        font = item.font(0)
        font.setBold(True)
        if self._stage.GetEditTarget().GetLayer() == item.layer:
            for i in range(item.columnCount()):
                item.setFont(i, font)

        for subtree in layerTree.childTrees:
            self._walk_sublayers(item, node, subtree, True)
        return item

    def set_edit_target(self, item, column):
        edit_target = Usd.EditTarget(item.layer, item.node)
        self._stage.SetEditTarget(edit_target)

    def open_context_menu(self, position):
        menu = QtWidgets.QMenu(self)

        act_reload_layer = QtWidgets.QAction(
            i18n("panels.usd_details_view.composition_tree", "Reload layer"), menu
        )
        act_reload_layer.triggered.connect(self._reload_layer)

        menu.addAction(act_reload_layer)
        layer = self._get_selected_layer()
        if layer:
            add_layer_copy_actions(menu, layer)
        menu.exec_(self.viewport().mapToGlobal(position))

    def _get_selected_layer(self):
        items = self.selectedItems()
        if not items:
            return

        return items[0].layer

    def _reload_layer(self):
        layer = self._get_selected_layer()
        if layer:
            reload_layers([layer], self)

    def open_header_menu(self, position):
        menu = HeaderVisibilityMenu(self)
        menu.set_column_permanent(0, True)
        menu.exec_(self.header().mapToGlobal(position))
