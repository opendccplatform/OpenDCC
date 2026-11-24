# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
from pxr import Sdf
import opendcc.core as dcc_core
from . import _usd_layers
from opendcc.i18n import i18n
from opendcc.usd_editor.usd_layer_text.usd_layer_text import UsdLayerText


class UsdLayersPanel(QtWidgets.QWidget):
    context_menu_requested = QtCore.Signal(QtWidgets.QMenu, str, str)

    def __init__(self, parent=None, model=None, column_labels=None, visible_columns=None):
        super(UsdLayersPanel, self).__init__(parent)
        self.app = dcc_core.Application.instance()
        self._callbacks = []
        self._updating_selection = False
        self._model = model
        self._text_preview = None
        self.view = _usd_layers.UsdLayersView(self)
        if model is not None:
            self.view.set_layer_model(model)
        columns = _usd_layers.UsdLayersModel
        source = self.view.layer_model()
        labels = (
            column_labels
            if column_labels is not None
            else {
                int(columns.NameColumn): i18n("panels.usd_layers", "Name"),
                int(columns.EditTargetColumn): i18n("panels.usd_layers", "Edit Target"),
                int(columns.PathColumn): i18n("panels.usd_layers", "Identifier"),
                int(columns.ResolvedPathColumn): i18n("panels.usd_layers", "Resolved Path"),
                int(columns.MutedColumn): i18n("panels.usd_layers", "Muted"),
                int(columns.DirtyColumn): i18n("panels.usd_layers", "Dirty"),
                int(columns.VersionColumn): i18n("panels.usd_layers", "Version"),
            }
        )
        for column, label in labels.items():
            if column_labels is not None or source.headerData(column, QtCore.Qt.Horizontal) is None:
                source.setHeaderData(column, QtCore.Qt.Horizontal, label, QtCore.Qt.DisplayRole)
        for column, width in (
            (columns.NameColumn, 270),
            (columns.EditTargetColumn, 70),
            (columns.MutedColumn, 45),
            (columns.DirtyColumn, 45),
            (columns.VersionColumn, 45),
        ):
            self.view.setColumnWidth(int(column), width)
        if visible_columns is None:
            visible_columns = {
                int(columns.NameColumn),
                int(columns.EditTargetColumn),
                int(columns.MutedColumn),
                int(columns.DirtyColumn),
            }
            visible_columns.update(range(int(columns.ColumnCount), source.columnCount()))
        for column in range(source.columnCount()):
            self.view.setColumnHidden(column, column not in visible_columns)
        self.view.layers_selected.connect(self._selection_changed)
        self.view.layers_changed.connect(self._sync_selection)
        self.view.context_menu_requested.connect(self.context_menu_requested.emit)

        menu_bar = QtWidgets.QMenuBar(self)
        show_menu = menu_bar.addMenu("Show")
        for text, checked, callback in (
            ("References", True, self.view.set_show_references),
            ("Only Dirty Layers", False, self.view.set_show_only_dirty),
            ("Anonymous Layers", False, self.view.set_show_only_anonymous),
            ("Empty Layers", False, self.view.set_show_only_empty),
        ):
            action = QtWidgets.QWidgetAction(show_menu)
            checkbox = QtWidgets.QCheckBox(text, show_menu)
            checkbox.setChecked(checked)
            checkbox.toggled.connect(callback)
            action.setDefaultWidget(checkbox)
            show_menu.addAction(action)

        self.search = QtWidgets.QLineEdit(self)
        self.search.setPlaceholderText(i18n("panels.usd_layers", "Search..."))
        self.search.setClearButtonEnabled(True)
        self.search.textChanged.connect(self.view.set_filter_text)
        self.view.filter_text_cleared.connect(self.search.clear)
        self.search.hide()

        controls = QtWidgets.QWidget(menu_bar)
        toolbar = QtWidgets.QHBoxLayout(controls)
        toolbar.setContentsMargins(0, 0, 0, 0)
        toolbar.setSpacing(0)
        for icon, tooltip, callback in (
            (":/icons/refresh", "Refresh", self._refresh),
            (":/layer_icons/add_layer", "Create Sublayer", self.view.add_sublayer),
        ):
            button = QtWidgets.QPushButton(QtGui.QIcon(icon), "", controls)
            button.setIconSize(QtCore.QSize(20, 20))
            button.setFlat(True)
            button.setToolTip(tooltip)
            button.clicked.connect(callback)
            toolbar.addWidget(button)

        self._preview_toggle = QtWidgets.QToolButton(controls)
        self._preview_toggle.setIcon(QtGui.QIcon(":/icons/panel_usd_layer_text"))
        self._preview_toggle.setIconSize(QtCore.QSize(20, 20))
        self._preview_toggle.setAutoRaise(True)
        self._preview_toggle.setCheckable(True)
        preview_label = i18n("panels.usd_layers", "Layer Text")
        self._preview_toggle.setToolTip(preview_label)
        self._preview_toggle.setAccessibleName(preview_label)
        self._preview_toggle.toggled.connect(self._show_text_preview)
        toolbar.addWidget(self._preview_toggle)

        self._search_toggle = QtWidgets.QToolButton(controls)
        self._search_toggle.setIcon(QtGui.QIcon(":/icons/small_search"))
        self._search_toggle.setIconSize(QtCore.QSize(16, 16))
        self._search_toggle.setAutoRaise(True)
        self._search_toggle.setCheckable(True)
        self._search_toggle.setFocusPolicy(QtCore.Qt.NoFocus)
        search_label = i18n("panels.usd_layers", "Search")
        self._search_toggle.setToolTip(search_label)
        self._search_toggle.setAccessibleName(search_label)
        self._search_toggle.toggled.connect(self._toggle_search)
        toolbar.addWidget(self._search_toggle)
        menu_bar.setCornerWidget(controls, QtCore.Qt.TopRightCorner)

        self._splitter = QtWidgets.QSplitter(QtCore.Qt.Horizontal, self)
        self._splitter.setChildrenCollapsible(False)
        self._splitter.addWidget(self.view)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setMenuBar(menu_bar)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)
        layout.addWidget(self.search)
        layout.addWidget(self._splitter, 1)
        self._add_callbacks()
        self.destroyed.connect(lambda: self._remove_callbacks())

    def _show_text_preview(self, show):
        if not show:
            if self._text_preview is not None and not self._text_preview.close():
                blocker = QtCore.QSignalBlocker(self._preview_toggle)
                self._preview_toggle.setChecked(True)
                blocker.unblock()
            return

        if self._text_preview is None:
            self._text_preview = UsdLayerText(self._splitter, follow_session=False)
            self._splitter.addWidget(self._text_preview)
            width = self._splitter.width()
            self._splitter.setSizes([width * 3 // 5, width * 2 // 5])

        self._text_preview.show()
        self._update_text_preview(reload=True)

    def _update_text_preview(self, reload=False):
        if not self._preview_toggle.isChecked():
            return

        selection = self.view.selectionModel()
        rows = selection.selectedRows() if selection is not None else []
        identifier = (
            rows[0].data(_usd_layers.UsdLayersModel.LayerIdentifierRole) if len(rows) == 1 else None
        )
        self._text_preview.set_layer(
            Sdf.Layer.Find(identifier) if identifier else None, reload=reload
        )

    def _refresh(self):
        self.view.refresh()
        if self._preview_toggle.isChecked():
            self._text_preview.refresh()

    def _toggle_search(self, show):
        self.search.setVisible(show)
        if show:
            self.search.setFocus()
        else:
            self.search.clear()

    def _add_callbacks(self):
        if self._callbacks:
            return
        for event, callback in (
            (dcc_core.Application.EventType.CURRENT_STAGE_CHANGED, self.view.set_current_stage),
            (dcc_core.Application.EventType.LAYER_SELECTION_CHANGED, self._sync_selection),
        ):
            self._callbacks.append((event, self.app.register_event_callback(event, callback)))
        self.view.set_current_stage()

    def _remove_callbacks(self):
        for event, handle in self._callbacks:
            self.app.unregister_event_callback(event, handle)
        self._callbacks = []

    def _selection_changed(self, identifiers):
        if self._updating_selection:
            return
        self._updating_selection = True
        try:
            self.app.set_layer_selection({Sdf.Layer.Find(identifier) for identifier in identifiers})
        finally:
            self._updating_selection = False
        self._update_text_preview()

    def _sync_selection(self):
        if not self._updating_selection:
            self.view.select_layer_identifiers(
                [layer.identifier for layer in self.app.get_layer_selection()]
            )
            self._update_text_preview()

    def closeEvent(self, event):
        self._preview_toggle.setChecked(False)
        if self._preview_toggle.isChecked():
            event.ignore()
            return
        self._remove_callbacks()
        super(UsdLayersPanel, self).closeEvent(event)

    def showEvent(self, event):
        self._add_callbacks()
        super(UsdLayersPanel, self).showEvent(event)
