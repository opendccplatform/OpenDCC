# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import os
from Qt import QtCore, QtGui, QtWidgets
import opendcc.core as dcc_core
from pxr import Tf
from opendcc.ui.code_editor.code_editor import CodeEditor
from opendcc.i18n import i18n
from opendcc import cmds


class UsdLayerText(QtWidgets.QWidget):
    def __init__(self, parent=None, *, follow_session=True):
        QtWidgets.QWidget.__init__(self, parent=parent)
        self.app = dcc_core.Application.instance()
        self._follow_session = follow_session
        self._layer = None
        self._loaded_text = ""

        self.stage_changed_callback_id = None
        self.edit_target_changed_callback_id = None
        self.layer_selection_changed_callback_id = None

        self.textArea = CodeEditor(
            self, lang="usd", highlight_line=True, highlight_selected=False, line_numbers=True
        )

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        toolbar_layout = QtWidgets.QHBoxLayout()
        toolbar_layout.setContentsMargins(0, 0, 0, 0)
        toolbar_layout.setSpacing(4)

        toolbar = QtWidgets.QToolBar(self)
        toolbar.setIconSize(QtCore.QSize(20, 20))
        self.refresh_action = QtWidgets.QAction(
            QtGui.QIcon(":/icons/refresh"), i18n("panels.usd_layer_text", "Reload"), self
        )
        self.refresh_action.triggered.connect(self.refresh)
        toolbar.addAction(self.refresh_action)
        self.editableCheck_action = QtWidgets.QAction(
            QtGui.QIcon(":/icons/unlock"), i18n("panels.usd_layer_text", "Unlock for Editing"), self
        )
        self.editableCheck_action.setCheckable(True)
        self.editableCheck_action.setChecked(False)
        self.editableCheck_action.toggled.connect(self.set_editable)
        toolbar.addAction(self.editableCheck_action)
        self.save_action = QtWidgets.QAction(
            QtGui.QIcon(":/icons/save"), i18n("panels.usd_layer_text", "Apply"), self
        )
        self.save_action.triggered.connect(self.save)
        toolbar.addAction(self.save_action)

        self.layer_name = QtWidgets.QLineEdit()
        self.layer_name.setPlaceholderText(i18n("panels.usd_layer_text", "None"))
        self.layer_name.setReadOnly(True)
        self.layer_name.addAction(
            QtGui.QIcon(":/icons/usd_small"), QtWidgets.QLineEdit.LeadingPosition
        )

        self.show_text_button = QtWidgets.QPushButton(i18n("panels.usd_layer_text", "Show Text"))
        self.show_text_button.clicked.connect(self.show_text)
        self.show_text_button.hide()

        toolbar_layout.addWidget(toolbar)
        toolbar_layout.addWidget(self.show_text_button)
        toolbar_layout.addStretch()

        layout.addLayout(toolbar_layout)
        layout.addWidget(self.layer_name)
        layout.addSpacing(1)
        layout.addWidget(self.textArea)

        self._refresh()
        self.add_callbacks()
        self.destroyed.connect(lambda: self.remove_callbacks())

    def closeEvent(self, event):
        if not self._confirm_discard():
            event.ignore()
            return
        self._refresh()
        self.remove_callbacks()
        event.accept()

    def showEvent(self, event):
        if self.stage_changed_callback_id is None:
            self.add_callbacks()
        super(UsdLayerText, self).showEvent(event)

    def _confirm_discard(self):
        if not self._layer or self.textArea.code_editor.toPlainText() == self._loaded_text:
            return True
        return (
            QtWidgets.QMessageBox.question(
                self,
                i18n("panels.usd_layer_text", "Unapplied Layer Text"),
                i18n("panels.usd_layer_text", "Discard unapplied changes to the displayed layer?"),
                QtWidgets.QMessageBox.Discard | QtWidgets.QMessageBox.Cancel,
                QtWidgets.QMessageBox.Cancel,
            )
            == QtWidgets.QMessageBox.Discard
        )

    def show_text(self):
        if self._layer:
            self.textArea.code_editor.setPlainText(self._layer.ExportToString())
        self._loaded_text = self.textArea.code_editor.toPlainText()
        self.show_text_button.hide()
        self.refresh_action.setEnabled(True)
        self.editableCheck_action.setEnabled(bool(self._layer and self._layer.permissionToEdit))
        self.save_action.setEnabled(False)

    def set_stage(self, stage):
        self.set_layer(stage.GetEditTarget().GetLayer() if stage else None)

    def set_layer(self, layer, *, reload=False):
        if (self._layer == layer and not reload) or not self._confirm_discard():
            return
        self._layer = layer
        self._refresh()

    def update_stage(self):
        stage = self.app.get_session().get_current_stage()
        self.set_stage(stage)

    def add_callbacks(self):
        if not self._follow_session or self.stage_changed_callback_id is not None:
            return
        self.stage_changed_callback_id = self.app.register_event_callback(
            dcc_core.Application.EventType.CURRENT_STAGE_CHANGED, self.update_stage
        )
        self.edit_target_changed_callback_id = self.app.register_event_callback(
            dcc_core.Application.EventType.EDIT_TARGET_CHANGED, self.update_stage
        )
        self.layer_selection_changed_callback_id = self.app.register_event_callback(
            dcc_core.Application.EventType.LAYER_SELECTION_CHANGED, self.layer_selected
        )

        self.update_stage()

    def remove_callbacks(self):
        if self.stage_changed_callback_id:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.CURRENT_STAGE_CHANGED, self.stage_changed_callback_id
            )
            self.stage_changed_callback_id = None
        if self.edit_target_changed_callback_id:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.EDIT_TARGET_CHANGED,
                self.edit_target_changed_callback_id,
            )
            self.edit_target_changed_callback_id = None
        if self.layer_selection_changed_callback_id:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.LAYER_SELECTION_CHANGED,
                self.layer_selection_changed_callback_id,
            )
            self.layer_selection_changed_callback_id = None

    def layer_selected(self):
        selected_layers = list(self.app.get_layer_selection())
        if len(selected_layers) == 1:
            self.set_layer(selected_layers[0])

    def set_editable(self, editable):
        if editable and self._layer and self._layer.permissionToEdit:
            self.textArea.code_editor.setUndoRedoEnabled(True)
            self.textArea.code_editor.setReadOnly(False)
            self.save_action.setEnabled(True)
        else:
            self.textArea.code_editor.setUndoRedoEnabled(False)
            self.textArea.code_editor.setReadOnly(True)
            self.save_action.setEnabled(False)

    def refresh(self):
        self.set_layer(self._layer, reload=True)

    def _refresh(self):
        self.editableCheck_action.setChecked(False)
        self.set_editable(False)
        self.show_text_button.hide()

        self.refresh_action.setEnabled(True)
        self.editableCheck_action.setEnabled(bool(self._layer and self._layer.permissionToEdit))
        self.save_action.setEnabled(False)

        if self._layer:
            too_big = False
            if self._layer.anonymous:
                path = self._layer.GetDisplayName()
            else:
                path = self._layer.realPath
                if os.path.isfile(path):
                    size = os.path.getsize(path)
                    size_kb = float(size) / 1024
                    if size_kb > 50:
                        too_big = True

            self.layer_name.setText(path)

            if not too_big:
                self.textArea.code_editor.setPlainText(self._layer.ExportToString())
            else:
                self.textArea.code_editor.setPlainText(
                    i18n(
                        "panels.usd_layer_text",
                        "This file is too big.\nPlease click the 'Show Text' button to display it anyway.",
                    )
                )
                self.show_text_button.show()
                self.refresh_action.setEnabled(False)
                self.editableCheck_action.setEnabled(False)
                self.save_action.setEnabled(False)

            self.setEnabled(True)
        else:
            self.textArea.code_editor.clear()
            self.layer_name.setText("None")
            self.setEnabled(False)

        self._loaded_text = self.textArea.code_editor.toPlainText()

    def save(self):
        if self._layer:
            try:
                if not cmds.apply_layer_text(
                    self._layer.identifier, self.textArea.code_editor.toPlainText()
                ).is_successful():
                    raise ValueError("Layer contents could not be applied.")
                self._loaded_text = self.textArea.code_editor.toPlainText()
            except (Tf.ErrorException, ValueError, RuntimeError) as e:
                warn_msg = str(e)
                QtWidgets.QMessageBox.warning(
                    self,
                    i18n("panels.usd_layer_text", "Layer Syntax Error"),
                    "Failed to apply modified layer contents:\n\n{0}".format(warn_msg),
                )
