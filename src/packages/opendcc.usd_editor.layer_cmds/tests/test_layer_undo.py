# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import tempfile
from pathlib import Path

from pxr import Sdf, Tf
from opendcc import cmds
import test_layer_commands


class LayerUndoTests(test_layer_commands.LayerCommandsTests):
    def test_commands_and_undo(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        self.run_command("add_sublayer", self.root.identifier, first.identifier)
        self.stack.undo()
        self.assertEqual(list(self.root.subLayerPaths), [])
        self.stack.redo()
        self.assertEqual(list(self.root.subLayerPaths), [first.identifier])
        self.run_command("add_sublayer", self.root.identifier, second.identifier, index=1)
        self.root.subLayerOffsets[0] = Sdf.LayerOffset(12, 2)
        self.stack.clear()
        self.run_command("move_sublayer", self.root.identifier, first.identifier, 1)
        self.assertEqual(list(self.root.subLayerPaths), [second.identifier, first.identifier])
        self.assertEqual(self.root.subLayerOffsets[1], Sdf.LayerOffset(12, 2))
        self.stack.undo()
        self.assertEqual(list(self.root.subLayerPaths), [first.identifier, second.identifier])
        self.assertEqual(self.root.subLayerOffsets[0], Sdf.LayerOffset(12, 2))
        self.stack.redo()
        self.run_command("remove_sublayer", self.root.identifier, first.identifier)
        self.assertEqual(list(self.root.subLayerPaths), [second.identifier])
        self.stack.undo()
        self.assertIn(first.identifier, self.root.subLayerPaths)
        self.run_command("set_edit_target", second.identifier)
        self.assertEqual(self.stage.GetEditTarget().GetLayer(), second)
        self.stack.undo()
        self.assertEqual(self.stage.GetEditTarget().GetLayer(), self.root)
        self.run_command("mute_layer", second.identifier, mute=True)
        self.assertTrue(self.stage.IsLayerMuted(second.identifier))
        self.stack.undo()
        self.assertFalse(self.stage.IsLayerMuted(second.identifier))
        self.stack.redo()
        self.assertTrue(self.stage.IsLayerMuted(second.identifier))
        self.assertFalse(cmds.mute_layer(self.root.identifier).is_successful())

    def test_relative_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Sdf.Layer.CreateNew(str(Path(directory) / "root.usda"))
            child = Sdf.Layer.CreateNew(str(Path(directory) / "child.usda"))
            self.session.open_stage(root)
            self.run_command("add_sublayer", root.identifier, "child.usda")
            self.assertEqual(list(root.subLayerPaths), ["child.usda"])
            self.run_command("remove_sublayer", root.identifier, child.identifier)
            self.assertEqual(list(root.subLayerPaths), [])
            self.stack.undo()
            self.assertEqual(list(root.subLayerPaths), ["child.usda"])

    def test_text_validation_and_undo(self):
        before = self.root.ExportToString()
        text = '#usda 1.0\n\ndef Xform "Edited" {}\n'
        self.run_command("apply_layer_text", self.root.identifier, text)
        self.assertTrue(self.stage.GetPrimAtPath("/Edited"))
        self.stack.undo()
        self.assertEqual(self.root.ExportToString(), before)
        self.stack.redo()
        self.assertTrue(self.stage.GetPrimAtPath("/Edited"))
        before = self.root.ExportToString()
        size = self.stack.get_size()
        try:
            result = cmds.apply_layer_text(self.root.identifier, "invalid USDA")
            self.assertFalse(result.is_successful())
        except Tf.ErrorException:
            pass
        self.assertEqual(self.root.ExportToString(), before)
        self.assertEqual(self.stack.get_size(), size)

    def test_removed_anonymous_descendants_survive_undo(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        nested = Sdf.Layer.CreateAnonymous("nested.usda")
        nested.ImportFromString('#usda 1.0\n\ndef Xform "Keep" {}\n')
        child.subLayerPaths = [nested.identifier]
        self.root.subLayerPaths = [child.identifier]
        child_id, nested_id = child.identifier, nested.identifier
        self.stack.clear()
        self.run_command("remove_sublayer", self.root.identifier, child_id)
        del child, nested
        self.assertTrue(Sdf.Layer.Find(nested_id))
        self.stack.undo()
        self.assertEqual(list(self.root.subLayerPaths), [child_id])
        self.assertTrue(self.stage.GetPrimAtPath("/Keep"))
        self.stack.redo()
        self.assertEqual(list(self.root.subLayerPaths), [])
        self.stack.undo()
        self.assertTrue(self.stage.GetPrimAtPath("/Keep"))

    def test_saved_layer_retargeting_undo_preserves_offsets_and_file(self):
        with tempfile.TemporaryDirectory() as directory:
            child = Sdf.Layer.CreateAnonymous("child.usda")
            child.ImportFromString('#usda 1.0\n\ndef Xform "Saved" {}\n')
            self.root.subLayerPaths = [child.identifier]
            self.root.subLayerOffsets[0] = Sdf.LayerOffset(7, 2)
            old_target = self.stage.GetEditTargetForLocalLayer(child)
            self.stage.SetEditTarget(old_target)
            self.stack.clear()
            path = str(Path(directory) / "saved.usda")
            identifier = self.run_command("save_layer", child.identifier, path=path).get_result()
            self.run_command("retarget_layer", child.identifier, identifier)
            self.assertEqual(list(self.root.subLayerPaths), [identifier])
            self.assertEqual(self.root.subLayerOffsets[0], Sdf.LayerOffset(7, 2))
            self.assertEqual(self.stack.get_size(), 1)
            new_target = self.stage.GetEditTarget()
            self.assertEqual(new_target.GetLayer().identifier, identifier)
            self.assertEqual(new_target.GetMapFunction(), old_target.GetMapFunction())
            self.stack.undo()
            self.assertEqual(self.stage.GetEditTarget(), old_target)
            self.assertEqual(list(self.root.subLayerPaths), [child.identifier])
            self.assertTrue(Path(path).exists())
            self.assertIn('"Saved"', Path(path).read_text())
            self.stack.redo()
            self.assertEqual(list(self.root.subLayerPaths), [identifier])
            self.assertTrue(self.stage.GetPrimAtPath("/Saved"))
            self.assertEqual(self.stage.GetEditTarget(), new_target)

    def test_reload_undo_redo_uses_saved_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reload.usda"
            layer = Sdf.Layer.CreateNew(str(path))
            layer.ImportFromString('#usda 1.0\n\ndef Xform "Disk" {}\n')
            layer.Save()
            self.session.open_stage(layer)
            layer.ImportFromString('#usda 1.0\n\ndef Xform "Dirty" {}\n')
            before = layer.ExportToString()
            self.stack.clear()
            self.run_command("reload_layer", layer.identifier)
            after = layer.ExportToString()
            self.assertTrue(layer.GetPrimAtPath("/Disk"))
            self.assertFalse(layer.dirty)
            self.assertEqual(self.stack.get_size(), 1)
            self.stack.undo()
            self.assertEqual(layer.ExportToString(), before)
            self.assertTrue(layer.dirty)
            path.write_text('#usda 1.0\n\ndef Xform "External" {}\n')
            self.stack.redo()
            self.assertEqual(layer.ExportToString(), after)
            self.assertFalse(layer.dirty)
            self.assertIn('"External"', path.read_text())
