# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import unittest
from unittest.mock import patch

from Qt import QtWidgets
from pxr import Sdf
import opendcc.core as core
from opendcc.usd_editor.usd_layer_text.usd_layer_text import UsdLayerText


class LayerTextTests(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.root = Sdf.Layer.CreateAnonymous("text.usda")
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.stack = self.app.get_undo_stack()
        self.stack.set_enabled(True)
        self.stack.clear()
        self.text = UsdLayerText()
        self.text.show()

    def tearDown(self):
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
        ):
            self.text.close()
        self.text.deleteLater()
        QtWidgets.QApplication.processEvents()
        self.stack.clear()
        self.session.close_all()

    def edit_text(self, text):
        self.text.editableCheck_action.setChecked(True)
        self.text.textArea.code_editor.setPlainText(text)

    def test_manual_reload_and_apply_undo(self):
        self.assertTrue(self.text.textArea.code_editor.isReadOnly())
        before = self.text.textArea.code_editor.toPlainText()
        self.stage.DefinePrim("/Reloaded", "Xform")
        QtWidgets.QApplication.processEvents()
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), before)
        self.text.refresh_action.trigger()
        self.assertIn('"Reloaded"', self.text.textArea.code_editor.toPlainText())
        self.edit_text('#usda 1.0\n\ndef Xform "Applied" {}\n')
        self.text.save_action.trigger()
        self.assertTrue(self.stage.GetPrimAtPath("/Applied"))
        self.stack.undo()
        self.assertFalse(self.stage.GetPrimAtPath("/Applied"))
        self.stack.redo()
        self.assertTrue(self.stage.GetPrimAtPath("/Applied"))
        with patch.object(QtWidgets.QMessageBox, "question") as prompt:
            self.text.refresh()
            prompt.assert_not_called()

    def test_cancel_and_discard_on_layer_change(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        edited = '#usda 1.0\n\ndef Xform "Draft" {}\n'
        self.edit_text(edited)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Cancel
        ) as prompt:
            self.app.set_layer_selection({child})
            prompt.assert_called_once()
        self.assertEqual(self.text._layer, self.root)
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), edited)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
        ):
            self.text.layer_selected()
        self.assertEqual(self.text._layer, child)
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), child.ExportToString())
        self.app.set_layer_selection({self.root})
        self.assertNotIn('"Draft"', self.text.textArea.code_editor.toPlainText())

    def test_cancel_and_discard_on_stage_and_edit_target_change(self):
        edited = '#usda 1.0\n\ndef Xform "Draft" {}\n'
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        self.edit_text(edited)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Cancel
        ):
            self.stage.SetEditTarget(child)
        self.assertEqual(self.text._layer, self.root)
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), edited)
        other = Sdf.Layer.CreateAnonymous("other.usda")
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
        ):
            self.session.open_stage(other)
        self.assertEqual(self.text._layer, other)
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), other.ExportToString())

    def test_reload_and_close_can_be_cancelled(self):
        edited = '#usda 1.0\n\ndef Xform "Draft" {}\n'
        self.edit_text(edited)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Cancel
        ):
            self.text.refresh_action.trigger()
            self.assertEqual(self.text.textArea.code_editor.toPlainText(), edited)
            self.assertFalse(self.text.close())
            self.assertIsNotNone(self.text.stage_changed_callback_id)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
        ):
            self.text.refresh_action.trigger()
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), self.root.ExportToString())
        self.edit_text(edited)
        with patch.object(
            QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
        ) as prompt:
            self.assertTrue(self.text.close())
            prompt.assert_called_once()
        self.assertEqual(self.text.textArea.code_editor.toPlainText(), self.root.ExportToString())
        self.assertIsNone(self.text.stage_changed_callback_id)
        self.text.show()
        self.assertIsNotNone(self.text.stage_changed_callback_id)
        other = Sdf.Layer.CreateAnonymous("reopened.usda")
        self.session.open_stage(other)
        self.assertEqual(self.text._layer, other)

    def test_invalid_text_and_locked_layer(self):
        before = self.root.ExportToString()
        self.edit_text("invalid USDA")
        with patch.object(QtWidgets.QMessageBox, "warning") as warning:
            self.text.save()
            warning.assert_called_once()
        self.assertEqual(self.root.ExportToString(), before)
        self.assertEqual(self.stack.get_size(), 0)
        self.root.SetPermissionToEdit(False)
        try:
            with patch.object(
                QtWidgets.QMessageBox, "question", return_value=QtWidgets.QMessageBox.Discard
            ):
                self.text.refresh()
            self.assertFalse(self.text.editableCheck_action.isEnabled())
            self.text.set_editable(True)
            self.assertTrue(self.text.textArea.code_editor.isReadOnly())
            self.assertFalse(self.text.save_action.isEnabled())
        finally:
            self.root.SetPermissionToEdit(True)
