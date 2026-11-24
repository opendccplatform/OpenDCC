# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import tempfile
import unittest
from pathlib import Path

from pxr import Sdf, Tf, Usd
from opendcc import cmds
import opendcc.core as core


class LayerCommandsTests(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.root = Sdf.Layer.CreateAnonymous("layers_test.usda")
        self.session.open_stage(self.root)
        self.stage = self.session.get_current_stage()
        self.stack = self.app.get_undo_stack()
        self.stack.set_enabled(True)
        self.stack.clear()
        self.app.set_layer_selection(set())

    def tearDown(self):
        self.stack.clear()
        self.session.close_all()

    def run_command(self, name, *args, **kwargs):
        result = getattr(cmds, name)(*args, **kwargs)
        self.assertTrue(result.is_successful(), name)
        return result

    def assert_failed(self, name, *args, **kwargs):
        paths = list(self.root.subLayerPaths)
        size = self.stack.get_size()
        try:
            result = getattr(cmds, name)(*args, **kwargs)
            self.assertFalse(result.is_successful())
        except Tf.ErrorException:
            pass
        self.assertEqual(list(self.root.subLayerPaths), paths)
        self.assertEqual(self.stack.get_size(), size)

    def test_invalid_edits_leave_layer_and_undo_unchanged(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.run_command("add_sublayer", self.root.identifier, child.identifier)
        self.assert_failed("add_sublayer", self.root.identifier, child.identifier)
        self.assert_failed("add_sublayer", self.root.identifier, child.identifier, index=-1)
        self.assert_failed("add_sublayer", self.root.identifier, child.identifier, index=3)
        self.assert_failed("move_sublayer", self.root.identifier, child.identifier, -1)
        self.assert_failed("move_sublayer", self.root.identifier, child.identifier, 1)
        self.assert_failed("move_sublayer", self.root.identifier, "missing.usda", 0)
        self.assert_failed("remove_sublayer", self.root.identifier, "missing.usda")
        self.root.SetPermissionToEdit(False)
        try:
            self.assert_failed("remove_sublayer", self.root.identifier, child.identifier)
            self.assert_failed("move_sublayer", self.root.identifier, child.identifier, 0)
            with tempfile.TemporaryDirectory() as directory:
                path = str(Path(directory) / "must_not_exist.usda")
                self.assert_failed("add_sublayer", self.root.identifier, path)
                self.assertFalse(Path(path).exists())
        finally:
            self.root.SetPermissionToEdit(True)

    def test_relative_duplicate_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Sdf.Layer.CreateNew(str(Path(directory) / "root.usda"))
            child = Sdf.Layer.CreateNew(str(Path(directory) / "child.usda"))
            self.session.open_stage(root)
            self.run_command("add_sublayer", root.identifier, "child.usda")
            size = self.stack.get_size()
            self.assertFalse(cmds.add_sublayer(root.identifier, child.identifier).is_successful())
            self.assertEqual(list(root.subLayerPaths), ["child.usda"])
            self.assertEqual(self.stack.get_size(), size)

    def test_commands_execute(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        second = Sdf.Layer.CreateAnonymous("second.usda")
        self.run_command("add_sublayer", self.root.identifier, first.identifier)
        self.run_command("add_sublayer", self.root.identifier, second.identifier, index=1)
        self.run_command("move_sublayer", self.root.identifier, first.identifier, 1)
        self.assertEqual(list(self.root.subLayerPaths), [second.identifier, first.identifier])
        self.run_command(
            "apply_layer_text", first.identifier, '#usda 1.0\n\ndef Xform "Edited" {}\n'
        )
        self.assertTrue(self.stage.GetPrimAtPath("/Edited"))
        self.run_command("set_edit_target", second.identifier)
        self.assertEqual(self.stage.GetEditTarget().GetLayer(), second)
        self.run_command("mute_layer", first.identifier, mute=True)
        self.assertTrue(self.stage.IsLayerMuted(first.identifier))
        self.run_command("mute_layer", first.identifier, mute=False)
        self.run_command("remove_sublayer", self.root.identifier, first.identifier)
        self.assertEqual(list(self.root.subLayerPaths), [second.identifier])

    def test_anonymous_creation_and_replay(self):
        def script(parent):
            first_id = self.run_command(
                "add_sublayer", parent.identifier, "first.usda", anonymous=True
            ).get_result()
            second_id = self.run_command(
                "add_sublayer", parent.identifier, "second.usda", anonymous=True
            ).get_result()
            self.run_command("apply_layer_text", first_id, '#usda 1.0\n\ndef Xform "Replayed" {}\n')
            self.run_command("move_sublayer", parent.identifier, first_id, 0)
            self.run_command("set_edit_target", first_id)
            self.run_command("mute_layer", second_id, mute=True)
            self.run_command("remove_sublayer", parent.identifier, second_id)
            return (
                Sdf.Layer.Find(first_id).ExportToString(),
                len(parent.subLayerPaths),
                self.session.get_current_stage().GetEditTarget().GetLayer()
                == Sdf.Layer.Find(first_id),
            )

        first = script(self.root)
        other = Sdf.Layer.CreateAnonymous("replay_root.usda")
        self.session.open_stage(other)
        self.assertEqual(script(other), first)

    def test_sublayer_and_text_undo_without_ui_delegates(self):
        first = self.run_command(
            "add_sublayer", self.root.identifier, "first.usda", anonymous=True
        ).get_result()
        self.stack.undo()
        self.assertEqual(list(self.root.subLayerPaths), [])
        self.stack.redo()
        self.assertEqual(list(self.root.subLayerPaths), [first])
        second = self.run_command(
            "add_sublayer", self.root.identifier, "second.usda", anonymous=True
        ).get_result()
        before = list(self.root.subLayerPaths)
        self.run_command("move_sublayer", self.root.identifier, first, 0)
        self.stack.undo()
        self.assertEqual(list(self.root.subLayerPaths), before)
        self.stack.redo()
        self.assertEqual(list(self.root.subLayerPaths), [first, second])
        self.run_command("remove_sublayer", self.root.identifier, second)
        self.stack.undo()
        self.assertIn(second, self.root.subLayerPaths)
        self.stack.redo()
        self.assertNotIn(second, self.root.subLayerPaths)
        before_text = self.root.ExportToString()
        self.run_command(
            "apply_layer_text", self.root.identifier, '#usda 1.0\n\ndef Xform "Undo" {}\n'
        )
        self.stack.undo()
        self.assertEqual(self.root.ExportToString(), before_text)
        self.stack.redo()
        self.assertTrue(self.stage.GetPrimAtPath("/Undo"))

    def test_file_saves_do_not_enter_the_undo_stack(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "saved.usda")
            self.root.ImportFromString('#usda 1.0\n\ndef Xform "Saved" {}\n')
            size = self.stack.get_size()
            identifier = self.run_command(
                "save_layer", self.root.identifier, path=path
            ).get_result()
            self.assertEqual(self.stack.get_size(), size)
            saved = Sdf.Layer.FindOrOpen(identifier)
            self.assertTrue(saved.GetPrimAtPath("/Saved"))
            self.run_command("save_layer", saved.identifier)
            self.assertEqual(self.stack.get_size(), size)

    def test_retarget_rejects_locked_parents_and_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            child = Sdf.Layer.CreateAnonymous("child.usda")
            child.ImportFromString('#usda 1.0\n\ndef Xform "Keep" {}\n')
            self.root.subLayerPaths = [child.identifier]
            path = str(Path(directory) / "saved.usda")
            target = self.run_command("save_layer", child.identifier, path=path).get_result()
            self.root.SetPermissionToEdit(False)
            try:
                self.assert_failed("retarget_layer", child.identifier, target)
            finally:
                self.root.SetPermissionToEdit(True)
            self.root.subLayerPaths = [child.identifier, target]
            self.assert_failed("retarget_layer", child.identifier, target)

    def test_layer_text_undo_retains_detached_anonymous_descendants(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        nested = Sdf.Layer.CreateAnonymous("nested.usda")
        nested.ImportFromString('#usda 1.0\n\ndef Xform "Keep" {}\n')
        child.subLayerPaths = [nested.identifier]
        self.root.subLayerPaths = [child.identifier]
        nested_id = nested.identifier
        self.stack.clear()
        self.run_command(
            "apply_layer_text", self.root.identifier, '#usda 1.0\n\ndef Xform "Replace" {}\n'
        )
        del child, nested
        self.assertTrue(Sdf.Layer.Find(nested_id))
        self.stack.undo()
        self.assertTrue(self.stage.GetPrimAtPath("/Keep"))
        self.stack.redo()
        self.assertTrue(self.stage.GetPrimAtPath("/Replace"))

    def test_invalid_existing_sublayer_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.usda"
            path.write_text("invalid layer contents")
            self.assert_failed("add_sublayer", self.root.identifier, str(path))
            self.assertEqual(path.read_text(), "invalid layer contents")

    def test_move_between_parents_preserves_resolved_layer_and_offset(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Sdf.Layer.CreateNew(str(Path(directory) / "source.usda"))
            child = Sdf.Layer.CreateNew(str(Path(directory) / "child.usda"))
            absolute_id = child.identifier
            target_dir = Path(directory) / "target"
            target_dir.mkdir()
            target = Sdf.Layer.CreateNew(str(target_dir / "target.usda"))
            self.root.subLayerPaths = [source.identifier, target.identifier]
            source.subLayerPaths = ["child.usda"]
            offset = Sdf.LayerOffset(8, 2)
            source.subLayerOffsets[0] = offset
            self.stack.clear()

            self.run_command(
                "move_sublayer",
                source.identifier,
                absolute_id,
                0,
                new_parent_identifier=target.identifier,
            )
            self.assertEqual(list(source.subLayerPaths), [])
            self.assertEqual(list(target.subLayerPaths), [absolute_id])
            self.assertEqual(target.subLayerOffsets[0], offset)
            self.stack.undo()
            self.assertEqual(list(source.subLayerPaths), ["child.usda"])
            self.assertEqual(source.subLayerOffsets[0], offset)
            self.assertEqual(list(target.subLayerPaths), [])
            self.stack.redo()
            self.assertEqual(list(target.subLayerPaths), [absolute_id])
            self.assertEqual(target.subLayerOffsets[0], offset)

    def test_move_between_parents_rejects_cycles_and_locked_destinations(self):
        first = Sdf.Layer.CreateAnonymous("first.usda")
        nested = Sdf.Layer.CreateAnonymous("nested.usda")
        target = Sdf.Layer.CreateAnonymous("target.usda")
        first.subLayerPaths = [nested.identifier]
        self.root.subLayerPaths = [first.identifier, target.identifier]
        before = list(self.root.subLayerPaths)
        self.assert_failed(
            "move_sublayer",
            self.root.identifier,
            first.identifier,
            0,
            new_parent_identifier=nested.identifier,
        )
        target.SetPermissionToEdit(False)
        self.assert_failed(
            "move_sublayer",
            self.root.identifier,
            first.identifier,
            0,
            new_parent_identifier=target.identifier,
        )
        self.assertEqual(list(self.root.subLayerPaths), before)
        self.assertEqual(list(first.subLayerPaths), [nested.identifier])
        self.assertEqual(list(nested.subLayerPaths), [])
        self.assertEqual(list(target.subLayerPaths), [])

    def test_move_to_current_index_can_be_undone(self):
        child = Sdf.Layer.CreateAnonymous("unchanged.usda")
        self.root.subLayerPaths = [child.identifier]
        self.root.subLayerOffsets[0] = Sdf.LayerOffset(7, 2)
        before = self.root.ExportToString()
        self.stack.clear()
        self.run_command("move_sublayer", self.root.identifier, child.identifier, 0)
        self.stack.undo()
        self.assertEqual(self.root.ExportToString(), before)
        self.stack.redo()
        self.assertEqual(self.root.ExportToString(), before)

    def test_file_parent_can_be_opened_by_identifier(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "parent.usda")
            parent = Sdf.Layer.CreateNew(path)
            del parent
            self.assertFalse(Sdf.Layer.Find(path))
            child = Sdf.Layer.CreateAnonymous("child.usda")
            self.run_command("add_sublayer", path, child.identifier)
            parent = Sdf.Layer.FindOrOpen(path)
            self.assertTrue(parent)
            self.assertEqual(list(parent.subLayerPaths), [child.identifier])
            self.stack.undo()
            self.assertEqual(list(parent.subLayerPaths), [])
            self.stack.redo()
            self.assertEqual(list(parent.subLayerPaths), [child.identifier])
            self.stack.clear()
            parent.Save()
            del parent
            self.assertFalse(Sdf.Layer.Find(path))
            self.run_command("move_sublayer", path, child.identifier, 0)
            self.stack.undo()
            self.stack.clear()
            self.run_command("remove_sublayer", path, child.identifier)
            parent = Sdf.Layer.Find(path)
            self.assertEqual(list(parent.subLayerPaths), [])
            self.stack.undo()
            self.assertEqual(list(parent.subLayerPaths), [child.identifier])

    def test_edit_target_preserves_sublayer_time_mapping(self):
        child = Sdf.Layer.CreateAnonymous("offset.usda")
        self.root.subLayerPaths = [child.identifier]
        self.root.subLayerOffsets[0] = Sdf.LayerOffset(7, 2)
        previous = self.stage.GetEditTarget()
        expected = self.stage.GetEditTargetForLocalLayer(child)
        self.stack.clear()
        self.run_command("set_edit_target", child.identifier)
        self.assertEqual(self.stage.GetEditTarget(), expected)
        self.stack.undo()
        self.assertEqual(self.stage.GetEditTarget(), previous)
        self.stack.redo()
        self.assertEqual(self.stage.GetEditTarget(), expected)

    def test_mute_undo_retains_anonymous_descendants(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        nested = Sdf.Layer.CreateAnonymous("nested.usda")
        nested.ImportFromString('#usda 1.0\n\ndef Xform "Keep" {}\n')
        child.subLayerPaths = [nested.identifier]
        self.root.subLayerPaths = [child.identifier]
        nested_id = nested.identifier
        self.stack.clear()
        self.run_command("mute_layer", child.identifier, mute=True)
        del child, nested
        self.assertTrue(Sdf.Layer.Find(nested_id))
        self.stack.undo()
        self.assertTrue(self.stage.GetPrimAtPath("/Keep"))
        self.stack.redo()
        self.assertFalse(self.stage.GetPrimAtPath("/Keep"))
        self.stack.undo()
        self.assertTrue(self.stage.GetPrimAtPath("/Keep"))
        self.assert_failed("mute_layer", self.stage.GetSessionLayer().identifier)

    def test_layer_text_redo_retains_new_anonymous_descendants(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        nested = Sdf.Layer.CreateAnonymous("nested.usda")
        nested.ImportFromString('#usda 1.0\n\ndef Xform "Added" {}\n')
        child.subLayerPaths = [nested.identifier]
        nested_id = nested.identifier
        text = "#usda 1.0\n( subLayers = [@" + child.identifier + "@] )\n"
        self.run_command("apply_layer_text", self.root.identifier, text)
        self.stack.undo()
        del child, nested
        self.assertTrue(Sdf.Layer.Find(nested_id))
        self.stack.redo()
        self.assertTrue(self.stage.GetPrimAtPath("/Added"))

    def test_retarget_cannot_replace_layer_with_its_parent(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        before = self.root.ExportToString()
        self.assert_failed("retarget_layer", child.identifier, self.root.identifier)
        self.assertEqual(self.root.ExportToString(), before)

    def test_muting_stage_without_session_layer(self):
        child = Sdf.Layer.CreateAnonymous("child.usda")
        self.root.subLayerPaths = [child.identifier]
        with Usd.StageCacheContext(Usd.BlockStageCaches):
            self.stage = Usd.Stage.Open(rootLayer=self.root, sessionLayer=None)
        self.assertIsNone(self.stage.GetSessionLayer())
        self.session.set_current_stage(self.stage)
        self.run_command("mute_layer", child.identifier, mute=True)
        self.assertTrue(self.stage.IsLayerMuted(child.identifier))
        self.stack.undo()
        self.assertFalse(self.stage.IsLayerMuted(child.identifier))
