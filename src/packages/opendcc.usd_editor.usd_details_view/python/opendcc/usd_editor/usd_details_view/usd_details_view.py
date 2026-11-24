# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Tomorrow-Open-Source-Technology-1.0
# Modifications by OpenDCC contributors.
#
# Copyright 2016 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#

from html import escape
from Qt import QtCore, QtGui, QtWidgets
import opendcc.core as dcc_core
from .composition_tree import CompositionTreeWidget
from pxr import Ar, Sdf, Tf, Usd, UsdGeom
from opendcc.usd_ui_utils import add_layer_copy_actions, reload_layers
from opendcc.color_theme import get_color_theme, ColorTheme
from opendcc.i18n import i18n
from opendcc.undo import UsdEditsUndoBlock


class VariantComboBox(QtWidgets.QComboBox):
    def __init__(self, parent, prim, variantSetName):
        QtWidgets.QComboBox.__init__(self, parent)
        self.prim = prim
        self.variantSetName = variantSetName

    def updateVariantSelection(self, index):
        variantSet = self.prim.GetVariantSet(self.variantSetName)
        currentVariantSelection = variantSet.GetVariantSelection()
        newVariantSelection = str(self.currentText())
        if currentVariantSelection != newVariantSelection:
            with UsdEditsUndoBlock():
                variantSet.SetVariantSelection(newVariantSelection)


def GetValueAndDisplayString(prop, time):
    """If `prop` is a timeSampled Sdf.AttributeSpec, compute a string specifying
    how many timeSamples it possesses.  Otherwise, compute the single default
    value, or targets for a relationship, or value at 'time' for a
    Usd.Attribute.  Return a tuple of a parameterless function that returns the
    resolved value at 'time', and the computed brief string for display.  We
    return a value-producing function rather than the value itself because for
    an Sdf.AttributeSpec with multiple timeSamples, the resolved value is
    *all* of the timeSamples, which can be expensive to compute, and is
    rarely needed.
    """

    def _ValAndStr(val):
        return (lambda: val, GetShortStringForValue(prop, val))

    if isinstance(prop, Usd.Relationship):
        return _ValAndStr(prop.GetTargets())
    elif isinstance(prop, Usd.Attribute):
        return _ValAndStr(prop.Get(time))
    elif isinstance(prop, Sdf.AttributeSpec):
        if time == Usd.TimeCode.Default():
            return _ValAndStr(prop.default)
        else:
            numTimeSamples = prop.layer.GetNumTimeSamplesForPath(prop.path)
            if numTimeSamples == 0:
                return _ValAndStr(prop.default)
            else:

                def _GetAllTimeSamples(attrSpec):
                    l = attrSpec.layer
                    p = attrSpec.path
                    ordinates = l.ListTimeSamplesForPath(p)
                    return [(o, l.QueryTimeSample(p, o)) for o in ordinates]

                if numTimeSamples == 1:
                    valStr = "1 time sample"
                else:
                    valStr = str(numTimeSamples) + " time samples"

                return (lambda prop=prop: _GetAllTimeSamples(prop), valStr)

    elif isinstance(prop, Sdf.RelationshipSpec):
        return _ValAndStr(prop.targetPathList)

    return (lambda: None, "unrecognized property type")


def GetShortStringForValue(prop, val):
    if isinstance(prop, Usd.Relationship):
        val = ", ".join(str(p) for p in val)
    elif isinstance(prop, Sdf.RelationshipSpec):
        return str(prop.targetPathList)

    # If there is no value opinion, we do not want to display anything,
    # since python 'None' has a different meaning than usda-authored None,
    # which is how we encode attribute value blocks (which evaluate to
    # Sdf.ValueBlock)
    if val is None:
        return ""

    valType = Sdf.GetValueTypeNameForValue(val)
    result = ""
    if valType.isArray and not isinstance(val, Sdf.ValueBlock):

        def arrayToStr(a):
            from itertools import chain

            elems = a if len(a) <= 6 else chain(a[:3], ["..."], a[-3:])
            return "[" + ", ".join(map(str, elems)) + "]"

        if val is not None and len(val):
            result = "%s[%d]: %s" % (valType.scalarType, len(val), arrayToStr(val))
        else:
            result = "%s[]" % valType.scalarType
    else:
        result = str(val)

    return result[:500]


class LayerInfo(object):
    def __init__(
        self, identifier, realPath, offset, stage, timeCodesPerSecond=None, isMuted=False, depth=0
    ):
        self._identifier = identifier
        self._realPath = realPath
        self._offset = offset
        self._stage = stage
        self._timeCodesPerSecond = timeCodesPerSecond
        self._isMuted = isMuted
        self._depth = depth

    @classmethod
    def FromLayer(cls, layer, stage, offset, depth=0):
        return cls(
            layer.identifier,
            layer.realPath,
            offset,
            stage,
            timeCodesPerSecond=layer.timeCodesPerSecond,
            depth=depth,
        )

    @classmethod
    def FromMutedLayerIdentifier(cls, identifier, parentLayer, stage, depth=0):
        realPath = ""
        try:
            resolver = Ar.GetResolver()
            realPath = resolver.Resolve(identifier).GetPathString()
        except Exception as e:
            Tf.Warn("Failed to resolve identifier {}: {}".format(identifier, e))
            realPath = "unknown"

        return cls(identifier, realPath, Sdf.LayerOffset(), stage, isMuted=True, depth=depth)

    def GetIdentifier(self):
        return self._identifier

    def GetRealPath(self):
        return self._realPath

    def IsMuted(self):
        return self._isMuted

    def GetOffset(self):
        return self._offset

    def GetOffsetString(self):
        if self._offset == None:
            return "-"
        if self._offset.IsIdentity():
            return ""
        else:
            return "{} , {}".format(self._offset.offset, self._offset.scale)

    def GetOffsetTooltipString(self):
        if self._offset == None:
            return "-"
        if self._offset.IsIdentity():
            return ""
        toolTips = [
            "<b>Layer Offset</b> (from stage root)",
            "<b>offset:</b> {}".format(self._offset.offset),
            "<b>scale:</b> {}".format(self._offset.scale),
        ]
        # Display info about automatic time scaling if the layer's tcps is known
        # and doesn't match the stage's tcps.
        if self._timeCodesPerSecond:
            stageTcps = self._stage.GetTimeCodesPerSecond()
            if self._timeCodesPerSecond != stageTcps:
                toolTips.append(
                    "Includes timeCodesPerSecond auto-scaling: "
                    "{} (stage) / {} (layer)".format(stageTcps, self._timeCodesPerSecond)
                )
        return "<br>".join(toolTips)

    def GetToolTipString(self):
        return "<b>identifier:</b> @%s@ <br> <b>resolved path:</b> %s" % (
            escape(self.GetIdentifier()),
            escape(self.GetRealPath()),
        )

    def GetHierarchicalDisplayString(self):
        return "    " * self._depth + Sdf.Layer.GetDisplayNameFromIdentifier(self._identifier)


def _AddLayerTree(stage, layerTree, depth=0, ancestors=frozenset()):
    ancestors = ancestors | {layerTree.layer.identifier}

    layers = [LayerInfo.FromLayer(layerTree.layer, stage, layerTree.offset, depth)]

    # The layer tree from the layer stack has all of the fully composed layer
    # offsets, but will not have any of the muted layers. The sublayer paths of
    # layer will still contain any muted layers but will not have the composed
    # layer offsets that the layer tree provides. So in order to show the muted
    # layers in the correct sublayer position, we go through the sublayer paths
    # parsing either the muted layer or a layer stack tree subtree.
    #
    # The layer tree will also not have any entries for layers that failed to
    # load. We need to handle this case as well.
    childTrees = layerTree.childTrees
    subLayerPaths = layerTree.layer.subLayerPaths
    childTreeIter = iter(layerTree.childTrees)
    numMissingLayers = 0
    for subLayerPath in subLayerPaths:
        anchoredSubLayerPath = Sdf.ComputeAssetPathRelativeToLayer(layerTree.layer, subLayerPath)
        if anchoredSubLayerPath in ancestors:
            numMissingLayers += 1
            continue
        if stage.IsLayerMuted(anchoredSubLayerPath):
            # The sublayer path is muted so add muted layer by path. We don't
            # recurse on sublayers for muted layers.
            layers.append(
                LayerInfo.FromMutedLayerIdentifier(
                    anchoredSubLayerPath, layerTree.layer, stage, depth=depth + 1
                )
            )
            numMissingLayers = numMissingLayers + 1
        elif not Sdf.Layer.Find(anchoredSubLayerPath):
            # Otherwise, the sublayer failed to load for some other reason.
            # Just skip it to maintain current behavior; in the future, we
            # may want to show an entry with some annotation to tell the
            # user about this sublayer.
            numMissingLayers = numMissingLayers + 1
        else:
            # Otherwise we expect the unmuted sublayer to be the next child
            # tree in the layer stack tree so we recursively add it.
            childTree = next(childTreeIter, None)
            if childTree is None:
                continue
            layers.extend(_AddLayerTree(stage, childTree, depth=depth + 1, ancestors=ancestors))

    # Since we're relying on the correspondence between the unmuted sublayer
    # paths and the child layer stack trees, report an error if the total number
    # of muted layers and child trees don't match up so we can track if it
    # becomes an issue.
    if numMissingLayers + len(childTrees) != len(subLayerPaths):
        Tf.Warn(
            "CODING ERROR: Encountered an unexpected number of muted "
            "sublayers of layer {}. The root layer stack may be "
            "incorrect in the layer stack view".format(layerTree.layer.identifier)
        )

    return layers


def GetRootLayerStackInfo(stage):
    primIndex = stage.GetPseudoRoot().GetPrimIndex()
    layerStack = primIndex.rootNode.layerStack

    with Ar.ResolverContextBinder(stage.GetPathResolverContext()):
        layers = []
        if layerStack.sessionLayerTree:
            layers.extend(_AddLayerTree(stage, layerStack.sessionLayerTree))
        layers.extend(_AddLayerTree(stage, layerStack.layerTree))
        return layers


class LayerStackViewColumnIndex:
    LAYER, OFFSET, PATH, VALUE = range(4)


class UsdDetailsView(QtWidgets.QWidget):
    def __init__(self, parent=None):
        QtWidgets.QWidget.__init__(self, parent=parent)

        self.layers_icon = QtGui.QIcon(":/icons/layers")

        self._layout = QtWidgets.QVBoxLayout(self)
        self._layout.setContentsMargins(0, 0, 0, 0)

        self._tabs = QtWidgets.QTabWidget()

        style = """
QTabWidget::pane {
    background: palette(light);
    border-top-color: palette(light);
}

QTabBar::tab:selected, QTabBar::tab:hover {
    background: palette(light);
    color: palette(foreground);
}

QTabBar::tab:!selected {
    background: TAB_BACKGROUND;
}

QToolBar[att~="main"] {
    background: palette(light);
    margin-top: 1px;
    margin-bottom: 1px;
}

QTabBar::tab {
    color: TAB_COLOR;
    background: TAB_BACKGROUND;

    padding-left: 12px;
    padding-right: 12px;
    padding-top: 4px;
    padding-bottom: 5px;

    border-radius: 0px;
    border-left: 0px;

    border: 1px solid;
    border-width: 0px 1px 0px 0px;
    border-color: palette(base) palette(light) palette(light) palette(base);
}

QTabBar::tab:left {
    padding-left: 4px;
    padding-right: 5px;
    padding-top: 12px;
    padding-bottom: 12px;
    border-width: 0px 0px 1px 0px;
    border-color: palette(light) palette(base) palette(light) palette(light);
}

QTabBar::tab:last {
    border: 0px;
}
"""
        if get_color_theme() == ColorTheme.LIGHT:
            style = style.replace("TAB_BACKGROUND", "#d6d6d6")
            style = style.replace("TAB_COLOR", "#3b3b3b")
        else:
            style = style.replace("TAB_BACKGROUND", "rgb(55, 55, 55)")
            style = style.replace("TAB_COLOR", "palette(dark)")

        self._tabs.setStyleSheet(style)

        self._metadata_view = QtWidgets.QTableWidget()
        self._metadata_view.setFrameShape(QtWidgets.QFrame.NoFrame)
        self._metadata_view.setFrameShadow(QtWidgets.QFrame.Plain)
        self._metadata_view.setLineWidth(0)
        self._metadata_view.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self._metadata_view.setAlternatingRowColors(True)
        self._metadata_view.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        self._metadata_view.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self._metadata_view.setShowGrid(False)
        self._metadata_view.setGridStyle(QtCore.Qt.SolidLine)
        self._metadata_view.setColumnCount(2)
        self._metadata_view.setObjectName("metadataView")
        self._metadata_view.setRowCount(0)
        self._metadata_view.setHorizontalHeaderItem(0, QtWidgets.QTableWidgetItem())
        self._metadata_view.setHorizontalHeaderItem(1, QtWidgets.QTableWidgetItem())
        self._metadata_view.horizontalHeader().setCascadingSectionResizes(True)
        self._metadata_view.horizontalHeader().setDefaultSectionSize(220)
        self._metadata_view.horizontalHeader().setMinimumSectionSize(30)
        self._metadata_view.horizontalHeader().setSortIndicatorShown(False)
        self._metadata_view.horizontalHeader().setStretchLastSection(True)
        self._metadata_view.horizontalHeader().setHighlightSections(False)
        self._metadata_view.verticalHeader().setVisible(False)
        self._metadata_view.verticalHeader().setDefaultSectionSize(30)
        self._metadata_view.verticalHeader().setMinimumSectionSize(20)
        self._metadata_view.verticalHeader().setStretchLastSection(False)
        self._metadata_view.horizontalHeaderItem(0).setText(
            i18n("panels.usd_details_view", "Field Name")
        )
        self._metadata_view.horizontalHeaderItem(1).setText(
            i18n("panels.usd_details_view", "Value")
        )

        self._layer_stack = QtWidgets.QTableWidget()
        self._layer_stack.setFrameShape(QtWidgets.QFrame.NoFrame)
        self._layer_stack.setFrameShadow(QtWidgets.QFrame.Plain)
        self._layer_stack.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        self._layer_stack.setAlternatingRowColors(True)
        self._layer_stack.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        self._layer_stack.setSelectionBehavior(QtWidgets.QAbstractItemView.SelectRows)
        self._layer_stack.setShowGrid(False)
        self._layer_stack.setGridStyle(QtCore.Qt.SolidLine)
        self._layer_stack.setColumnCount(4)
        self._layer_stack.setRowCount(0)
        self._layer_stack.setHorizontalHeaderLabels(
            [
                i18n("panels.usd_details_view", text)
                for text in ("Layer", "Layer Offset", "Path", "Value")
            ]
        )
        self._layer_stack.horizontalHeader().setStretchLastSection(True)
        self._layer_stack.verticalHeader().setVisible(False)
        self._layer_stack.verticalHeader().setDefaultSectionSize(30)
        self._layer_stack.setIconSize(QtCore.QSize(20, 20))

        self._layer_stack.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self._layer_stack.customContextMenuRequested.connect(self.open_context_menu)
        self._layer_stack.itemSelectionChanged.connect(self._selection_changed)

        self._layer_stack.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)

        self._composition = CompositionTreeWidget()

        self._tabs.addTab(self._metadata_view, i18n("panels.usd_details_view", "Meta Data"))
        self._tabs.addTab(self._layer_stack, i18n("panels.usd_details_view", "Layer Stack"))
        self._tabs.addTab(self._composition, i18n("panels.usd_details_view", "Composition"))

        self._layout.addWidget(self._tabs)

        self.selection_callback_id = None
        self.stage_changed_callback_id = None
        self.layer_selection_changed_callback_id = None
        self.time_changed_callback_id = None
        self._stage = None
        self._listener_obj_changed = None
        self._refresh_timer = QtCore.QTimer(self)
        self._refresh_timer.setSingleShot(True)
        self._refresh_timer.timeout.connect(self.update_data)

        self.app = dcc_core.Application.instance()
        self.destroyed.connect(lambda: self._release_callbacks())
        self.register_callbacks()

    def _get_selected_layers(self):
        selected_items = self._layer_stack.selectedItems()
        layers = set()
        for item in selected_items:
            layer = Sdf.Layer.Find(item.identifier)
            if layer:
                layers.add(layer)
        return layers

    def _set_layer_selection(self):
        selected_layers = self._get_selected_layers()
        layers = self.app.get_layer_selection()
        if selected_layers != layers:
            selection_blocker = QtCore.QSignalBlocker(self._layer_stack)
            self._layer_stack.clearSelection()
            for row in range(self._layer_stack.rowCount()):
                item = self._layer_stack.item(row, 0)
                if item:
                    layer = Sdf.Layer.Find(item.identifier)
                    if layer in layers:
                        self._layer_stack.selectionModel().select(
                            self._layer_stack.model().index(row, 0),
                            QtCore.QItemSelectionModel.Select | QtCore.QItemSelectionModel.Rows,
                        )

    def _selection_changed(self):
        self.app.set_layer_selection(self._get_selected_layers())

    def layer_selected(self):
        self._set_layer_selection()
        self._composition.layer_selected()

    def register_callbacks(self):
        self.selection_callback_id = self.app.register_event_callback(
            "selection_changed", self.update_data
        )
        self.stage_changed_callback_id = self.app.register_event_callback(
            "current_stage_changed", self.update_data
        )
        self.layer_selection_changed_callback_id = self.app.register_event_callback(
            "layer_selection_changed", self.layer_selected
        )
        self.time_changed_callback_id = self.app.register_event_callback(
            "current_time_changed", self.update_data
        )

        # make sure editor starts in correct state
        self.update_data()

    def _release_callbacks(self):
        # Qt may already have deleted child widgets when destroyed invokes this.
        if self.selection_callback_id is not None:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.SELECTION_CHANGED, self.selection_callback_id
            )
            self.selection_callback_id = None
        if self.stage_changed_callback_id is not None:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.CURRENT_STAGE_CHANGED, self.stage_changed_callback_id
            )
            self.stage_changed_callback_id = None
        if self.layer_selection_changed_callback_id is not None:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.LAYER_SELECTION_CHANGED,
                self.layer_selection_changed_callback_id,
            )
            self.layer_selection_changed_callback_id = None
        if self.time_changed_callback_id is not None:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.CURRENT_TIME_CHANGED, self.time_changed_callback_id
            )
            self.time_changed_callback_id = None
        if self._listener_obj_changed:
            self._listener_obj_changed.Revoke()
            self._listener_obj_changed = None
        self._stage = None

    def unregister_callbacks(self):
        self._release_callbacks()
        self._refresh_timer.stop()
        self._composition.update()

    def showEvent(self, event):
        if self.selection_callback_id is None:
            self.register_callbacks()
        super().showEvent(event)

    def closeEvent(self, event):
        self.unregister_callbacks()
        event.accept()

    def update_data(self):
        session = self.app.get_session()
        stage = session.get_current_stage()
        if stage != self._stage:
            if self._listener_obj_changed:
                self._listener_obj_changed.Revoke()
                self._listener_obj_changed = None
            self._stage = stage
            if stage:
                self._listener_obj_changed = Tf.Notice.Register(
                    Usd.Notice.ObjectsChanged, self._on_objects_changed, stage
                )
        if not stage:
            self.update_meta_data()
            self.update_layer_stack()
            self._composition.update()
            return

        selected_prim_path = self.app.get_prim_selection()

        prims = []
        for prim_path in selected_prim_path:
            prim = stage.GetPrimAtPath(prim_path)
            if prim.IsValid():
                prims.append(prim)

        prim = None
        if prims:
            prim = prims[0]

        self._composition.update(prim, stage)

        obj = prim  # because it can be a prim or a property

        if obj:
            selection = self.app.get_selection()
            selection_data = selection.get_selection_data(prim.GetPath())
            if selection_data.properties:
                attr_name = sorted(selection_data.properties)[-1]
                obj = prim.GetProperty(attr_name)
                if not obj.IsValid():
                    # Check if it is an inherited primvar.
                    inherited_primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(
                        attr_name
                    )
                    if inherited_primvar:
                        obj = inherited_primvar.GetAttr()

        self.update_meta_data(obj, stage)
        self.update_layer_stack(obj, stage)
        self._set_layer_selection()

    def _on_objects_changed(self, notice, stage):
        self._refresh_timer.start(0)

    def update_layer_stack(self, obj=None, stage=None):
        """Sets the contents of the layer stack viewer"""

        tableWidget = self._layer_stack
        selection_blocker = QtCore.QSignalBlocker(tableWidget)

        def createLayerStackViewItem(displayString, layerInfo, spec=None, toolTip=""):
            """Creates a table view item for the layer stack view widget"""
            item = QtWidgets.QTableWidgetItem(displayString)
            item.setToolTip(self._limitToolTipSize(toolTip))
            if layerInfo.IsMuted():
                mutedLayerColor = QtGui.QColor(151, 151, 151)
                item.setForeground(QtGui.QBrush(mutedLayerColor))

            # Set layer and stage info for the context menu. The non-pseudoroot
            # layer stack views also provide a spec path for the context
            item.stage = stage
            item.layerPath = layerInfo.GetRealPath()
            item.identifier = layerInfo.GetIdentifier()
            if spec is not None:
                item.path = spec.path.pathString

            return item

        def addLayerItem(rowNum, layerInfo):
            layerItem = createLayerStackViewItem(
                layerInfo.GetHierarchicalDisplayString(),
                layerInfo,
                toolTip=layerInfo.GetToolTipString(),
            )
            tableWidget.setItem(rowNum, LayerStackViewColumnIndex.LAYER, layerItem)

        def addOffsetItem(rowNum, layerInfo):
            offsetItem = createLayerStackViewItem(
                layerInfo.GetOffsetString(), layerInfo, toolTip=layerInfo.GetOffsetTooltipString()
            )
            tableWidget.setItem(rowNum, LayerStackViewColumnIndex.OFFSET, offsetItem)

        def addSpecPathItem(rowNum, layerInfo, spec):
            pathItem = createLayerStackViewItem(
                spec.path.pathString, layerInfo, spec=spec, toolTip=spec.path.pathString
            )
            tableWidget.setItem(rowNum, LayerStackViewColumnIndex.PATH, pathItem)

        def addMetadataItem(rowNum, layerInfo, spec):
            metadataKeys = spec.GetMetaDataInfoKeys()
            metadataDict = {}
            for mykey in metadataKeys:
                if spec.HasInfo(mykey):
                    metadataDict[mykey] = spec.GetInfo(mykey)
            valStr, ttStr = self._formatMetadataValueView(metadataDict)

            valueItem = createLayerStackViewItem(valStr, layerInfo, spec=spec, toolTip=ttStr)
            tableWidget.setItem(rowNum, LayerStackViewColumnIndex.VALUE, valueItem)

        def addSpecValueItem(rowNum, layerInfo, spec):
            _, valStr = GetValueAndDisplayString(spec, Usd.TimeCode(self.app.get_current_time()))
            valueItem = createLayerStackViewItem(valStr, layerInfo, spec=spec, toolTip=valStr)
            sampleBased = spec.layer.GetNumTimeSamplesForPath(spec.path) > 0
            valueItemColor = (
                QtGui.QColor(177, 207, 153) if sampleBased else QtGui.QColor(135, 206, 250)
            )
            valueItem.setForeground(valueItemColor)
            tableWidget.setItem(rowNum, LayerStackViewColumnIndex.VALUE, valueItem)

        # Clear table widget
        tableWidget.clearContents()
        tableWidget.setRowCount(0)

        if not stage:
            return
        if obj is None:
            obj = stage.GetPseudoRoot()

        path = obj.GetPath()
        isPseudoRoot = path == Sdf.Path.absoluteRootPath
        isProperty = path.IsPropertyPath()

        layers = None
        specsAndLayerOffsets = None
        # valueColumnHeader = "Value"
        if isPseudoRoot:
            # For the pseudoRoot, get the layers from the root layer stack
            layers = GetRootLayerStackInfo(stage)
        else:
            # Otherwise we get the specs (and layer offsets) from the prim or
            # property stack. Note that the layer offsets are the cumulative
            # offsets of the spec's layer relative to the root of the stage.
            if isProperty:
                specsAndLayerOffsets = obj.GetPropertyStackWithLayerOffsets(
                    Usd.TimeCode(self.app.get_current_time())
                )
            else:
                specsAndLayerOffsets = obj.GetPrimStackWithLayerOffsets()
            # We get the layer info from each prim or property spec and its
            # computed layer offset
            layers = [
                LayerInfo.FromLayer(spec.layer, stage, offset)
                for spec, offset in specsAndLayerOffsets
            ]

        tableWidget.setRowCount(len(layers))
        # While adding layers we'll determine if we should hide the offset
        # column. By default we always display it for the pseudoRoot. We'll
        # only display it for prims and properties if at least one spec has a
        # non-identity layer offset.
        hideOffsetColumn = not isPseudoRoot
        hideSpecColumns = isPseudoRoot
        for i, layer in enumerate(layers):
            if not layer.GetOffset().IsIdentity():
                hideOffsetColumn = False

            # Always add the layer and offset items.
            addLayerItem(i, layer)
            addOffsetItem(i, layer)

            # Add the items for the spec columns if needed.
            if not hideSpecColumns:
                spec = specsAndLayerOffsets[i][0]
                addSpecPathItem(i, layer, spec)
                # The value column for prims shows metadata instead of property
                # values.
                if isProperty:
                    addSpecValueItem(i, layer, spec)
                else:
                    addMetadataItem(i, layer, spec)

        # Set the hidden state of the dynamically visible columns.
        tableWidget.setColumnHidden(LayerStackViewColumnIndex.OFFSET, hideOffsetColumn)
        tableWidget.setColumnHidden(LayerStackViewColumnIndex.PATH, hideSpecColumns)
        tableWidget.setColumnHidden(LayerStackViewColumnIndex.VALUE, hideSpecColumns)

        # Some final formatting for the spec columns if shown.
        if not hideSpecColumns:
            # The value column's header adjusts for whether we're showing prim,
            # attribute, or relationship layer stack.
            valueColumnHeader = (
                "Value"
                if isinstance(obj, Usd.Attribute)
                else "Target Paths" if isinstance(obj, Usd.Relationship) else "Metadata"
            )
            tableWidget.horizontalHeaderItem(LayerStackViewColumnIndex.VALUE).setText(
                valueColumnHeader
            )

            # Resize the value column to its contents so that display updates
            # appropriately when switching between selected properties and
            # prims.
            tableWidget.resizeColumnToContents(LayerStackViewColumnIndex.VALUE)

    def update_meta_data(self, obj=None, stage=None):
        """Sets the contents of the metadata viewer"""

        # XXX: this method gets called multiple times on selection, it
        # would be nice to clean that up and ensure we only update as needed.

        tableWidget = self._metadata_view

        # Setup table widget
        tableWidget.clearContents()
        tableWidget.setRowCount(0)

        if not stage or not obj:
            return

        m = obj.GetAllMetadata()

        # We have to explicitly add in metadata related to composition arcs
        # and value clips here, since GetAllMetadata prunes them out.
        #
        # XXX: Would be nice to have some official facility to query
        # this.
        compKeys = [  # composition related metadata (inherits handled below)
            "references",
            "specializes",
            "payload",
            "subLayers",
        ]

        for k in compKeys:
            v = obj.GetMetadata(k)
            if not v is None:
                m[k] = v

        m["clips"] = obj.GetMetadata("clips") or {}

        # Variant selections that don't have a defined variant set will be
        # displayed as well to aid debugging. Collect them separately from
        # the variant sets.
        variantSets = {}
        setlessVariantSelections = {}
        if isinstance(obj, Usd.Prim):
            # Get the inherits via API instead of the "inheritPaths" metadata
            # which can be incomplete.
            inheritPaths = obj.GetInherits().GetAllDirectInherits()
            if inheritPaths:
                m["inherits"] = inheritPaths

            # Get all variant selections as setless and remove the ones we find
            # sets for.
            setlessVariantSelections = obj.GetVariantSets().GetAllVariantSelections()

            variantSetNames = obj.GetVariantSets().GetNames()
            for variantSetName in variantSetNames:
                variantSet = obj.GetVariantSet(variantSetName)
                variantNames = variantSet.GetVariantNames()
                variantSelection = variantSet.GetVariantSelection()
                combo = VariantComboBox(None, obj, variantSetName)
                # First index is always empty to indicate no (or invalid)
                # variant selection.
                combo.addItem("")
                for variantName in variantNames:
                    combo.addItem(variantName)
                indexToSelect = combo.findText(variantSelection)
                combo.setCurrentIndex(indexToSelect)
                variantSets[variantSetName] = combo
                # Remove found variant set from setless.
                setlessVariantSelections.pop(variantSetName, None)

        rowIndex = 0

        # Although most metadata should be presented alphabetically,the most
        # user-facing items should be placed at the beginning of the  metadata
        # list, these consist of [object type], [path], variant sets, active,
        # assetInfo, and kind.
        def populateMetadataTable(key, val, rowIndex):
            tableWidget.insertRow(rowIndex)
            attrName = QtWidgets.QTableWidgetItem(str(key))
            tableWidget.setItem(rowIndex, 0, attrName)

            valStr, ttStr = self._formatMetadataValueView(val)
            attrVal = QtWidgets.QTableWidgetItem(valStr)
            attrVal.setToolTip(ttStr)

            tableWidget.setItem(rowIndex, 1, attrVal)

        def populateMetadataTableVariant(key, val, rowIndex):
            tableWidget.insertRow(rowIndex)
            attrName = QtWidgets.QTableWidgetItem(str(key + " variant"))
            tableWidget.setItem(rowIndex, 0, attrName)
            tableWidget.setCellWidget(rowIndex, 1, val)

        sortedKeys = sorted(m.keys())
        reorderedKeys = ["kind", "assetInfo", "active"]

        for key in reorderedKeys:
            if key in sortedKeys:
                sortedKeys.remove(key)
                sortedKeys.insert(0, key)

        object_type = (
            "Attribute"
            if type(obj) is Usd.Attribute
            else (
                "Prim"
                if type(obj) is Usd.Prim
                else "Relationship" if type(obj) is Usd.Relationship else "Unknown"
            )
        )
        populateMetadataTable("[object type]", object_type, rowIndex)
        rowIndex += 1

        # Represent applied API schemas
        if type(obj) is Usd.Prim:
            populateMetadataTable("[applied API schemas]", str(obj.GetAppliedSchemas()), rowIndex)
            rowIndex += 1

        populateMetadataTable("[path]", str(obj.GetPath()), rowIndex)
        rowIndex += 1

        for variantSetName, combo in variantSets.items():
            populateMetadataTableVariant(variantSetName, combo, rowIndex)
            combo.currentIndexChanged.connect(
                lambda i, combo=combo: combo.updateVariantSelection(i)
            )
            rowIndex += 1

        # Add all the setless variant selections directly after the variant
        # combo boxes
        for variantSetName, variantSelection in setlessVariantSelections.items():
            valStr, ttStr = self._formatMetadataValueView(variantSelection)
            # Italicized label to stand out when debugging a scene.
            label = QtWidgets.QLabel("<i>" + escape(valStr) + "</i>")
            label.setIndent(3)
            label.setToolTip(ttStr)
            populateMetadataTableVariant(variantSetName, label, rowIndex)

            rowIndex += 1

        for key in sortedKeys:
            if key == "clips":
                for clip, metadataGroup in m[key].items():
                    attrName = QtWidgets.QTableWidgetItem(str("clips:" + clip))
                    for i, metadata in enumerate(metadataGroup.keys()):
                        tableWidget.insertRow(rowIndex)
                        if i == 0:
                            tableWidget.setItem(rowIndex, 0, attrName)

                        dataPair = (metadata, metadataGroup[metadata])
                        valStr, ttStr = self._formatMetadataValueView(dataPair)
                        attrVal = QtWidgets.QTableWidgetItem(valStr)
                        attrVal.setToolTip(ttStr)
                        tableWidget.setItem(rowIndex, 1, attrVal)
                        rowIndex += 1
            elif key == "customData":
                populateMetadataTable(key, obj.GetCustomData(), rowIndex)
                rowIndex += 1
            else:
                populateMetadataTable(key, m[key], rowIndex)
                rowIndex += 1

        tableWidget.resizeColumnToContents(0)

    def _findIndentPos(self, s):
        for index, char in enumerate(s):
            if char != " ":
                return index

        return len(s) - 1

    def _maxToolTipWidth(self):
        return 90

    def _maxToolTipHeight(self):
        return 32

    def _trimWidth(self, s, isList=False):
        # We special-case the display offset because list
        # items will have </li> tags embedded in them.
        offset = 10 if isList else 5

        if len(s) >= self._maxToolTipWidth():
            # For strings, well do special ellipsis behavior
            # which displays the last 5 chars with an ellipsis
            # in between. For other values, we simply display a
            # trailing ellipsis to indicate more data.
            if s[0] == "'" and s[-1] == "'":
                return s[: self._maxToolTipWidth() - offset] + "..." + s[len(s) - offset :]
            else:
                return s[: self._maxToolTipWidth()] + "..."
        return s

    def _limitToolTipSize(self, s, isList=False):
        ttStr = ""

        lines = s.split("<br>")
        for index, line in enumerate(lines):
            if index + 1 > self._maxToolTipHeight():
                break
            ttStr += self._trimWidth(line, isList)
            if not isList and index != len(lines) - 1:
                ttStr += "<br>"

        if len(lines) > self._maxToolTipHeight():
            ellipsis = " " * self._findIndentPos(line) + "..."
            if isList:
                ellipsis = "<li>" + ellipsis + "</li>"
            else:
                ellipsis += "<br>"

            ttStr += ellipsis
            ttStr += self._trimWidth(lines[len(lines) - 2], isList)

        return ttStr

    def _addRichTextIndicators(self, s):
        # - We'll need to use html-style spaces to ensure they are respected
        # in the toolTip which uses richtext formatting.
        # - We wrap the tooltip as a paragraph to ensure &nbsp; 's are
        # respected by Qt's rendering engine.
        return "<p>" + s.replace(" ", "&nbsp;") + "</p>"

    def _limitValueDisplaySize(self, s):
        maxValueChars = 300
        return s[:maxValueChars]

    def _cleanStr(self, s, repl):
        replaced = str(s).replace("\n", repl)
        return " ".join(replaced.split())

    def _formatMetadataValueView(self, val):
        from pprint import pformat

        valStr = self._cleanStr(val, " ")
        ttStr = ""
        isList = False

        # For iterable things, like VtArrays and lists, we want to print
        # a nice numbered list.
        if isinstance(val, list) or getattr(val, "_isVtArray", False):
            isList = True

            # We manually supply the index for our list elements
            # because Qt's richtext processor starts the <ol> numbering at 1.
            for index, value in enumerate(val):
                trimmed = escape(self._cleanStr(value, " "))
                ttStr += "<li>" + str(index) + ":  " + trimmed + "</li><br>"

        elif isinstance(val, dict):
            # We stringify all dict elements so they display more nicely.
            val = {k: str(v) for k, v in val.items()}

            # We'll need to strip the quotes generated by the str' operation above
            stripQuotes = lambda s: s.replace("'", "").replace('"', "")

            valStr = stripQuotes(self._cleanStr(val, " "))

            formattedDict = pformat(val)
            formattedDictLines = formattedDict.split("\n")
            for index, line in enumerate(formattedDictLines):
                ttStr += escape(stripQuotes(line)) + (
                    "" if index == len(formattedDictLines) - 1 else "<br>"
                )
        else:
            ttStr = self._cleanStr(escape(str(val)), "<br>")

        valStr = self._limitValueDisplaySize(valStr)
        ttStr = self._addRichTextIndicators(self._limitToolTipSize(ttStr, isList))
        return valStr, ttStr

    def open_context_menu(self, position):
        menu = QtWidgets.QMenu(self)

        act_reload_layer = QtWidgets.QAction(i18n("panels.usd_details_view", "Reload layer"), menu)
        act_reload_layer.triggered.connect(self._reload_layer)

        menu.addAction(act_reload_layer)
        layer = self._get_selected_layer()
        if layer:
            add_layer_copy_actions(menu, layer)
        menu.exec_(self._layer_stack.viewport().mapToGlobal(position))

    def _get_selected_layer(self):
        row = self._layer_stack.currentRow()
        item = self._layer_stack.item(row, 0)
        if not item:
            return
        layer = Sdf.Layer.Find(item.identifier)
        return layer

    def _reload_layer(self):
        layer = self._get_selected_layer()
        if layer:
            reload_layers([layer], self)
