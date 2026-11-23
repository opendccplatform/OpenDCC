# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import tempfile
import unittest

from pxr import Gf, Sdf, Usd, UsdGeom
import opendcc.core as core
from opendcc import cmds


class TestCommonCommands(unittest.TestCase):
    UNDO_REDO_CYCLES = 2
    # Allow rounding when USD composes and decomposes transforms.
    TRANSFORM_TOLERANCE = 1e-5

    def setUp(self):
        self.app = core.Application.instance()
        self.assertTrue(self.app.is_ui_available())
        self.session = self.app.get_session()
        self.stack = self.app.get_undo_stack()
        enabled, limit = self.stack.is_enabled(), self.stack.get_undo_limit()
        self.addCleanup(self.stack.set_enabled, enabled)
        self.addCleanup(self.stack.set_undo_limit, limit)
        self.stack.set_enabled(True)
        # Keep every edit available for undo; zero means unlimited history.
        self.stack.set_undo_limit(0)
        self.app.set_current_viewport_tool(None)
        self.stage = Usd.Stage.CreateInMemory()
        self.session.set_current_stage(self.stage)
        self.addCleanup(self.session.close_stage, self.stage)
        self.addCleanup(self.app.clear_prim_selection)
        self.addCleanup(self.stack.clear)
        UsdGeom.SetStageUpAxis(self.stage, UsdGeom.Tokens.y)
        self.stage.DefinePrim("/World", "Xform")
        item = self.stage.DefinePrim("/World/Item", "Xform")
        item.CreateAttribute("value", Sdf.ValueTypeNames.Double).Set(7)
        self.stage.DefinePrim("/World/Item/Child", "Xform")
        self.stage.DefinePrim("/World/Target", "Xform")
        item.CreateRelationship("target").SetTargets([Sdf.Path("/World/Target")])
        self.app.set_prim_selection([Sdf.Path("/World/Item")])
        self.stack.clear()

    def snapshot_layers(self):
        return tuple(layer.ExportToString() for layer in self.stage.GetLayerStack())

    def snapshot_state(self):
        return self.snapshot_layers(), self.app.get_selection()

    def isolate_clipboard(self):
        directory = tempfile.TemporaryDirectory(prefix="common-command-clipboard-")
        self.addCleanup(directory.cleanup)
        clipboard = self.app.get_usd_clipboard()
        # The application starts with this default path; keep tests off its file.
        self.addCleanup(
            clipboard.set_clipboard_path, str(Path(tempfile.gettempdir()) / "OpenDCCClipboard.usd")
        )
        clipboard.set_clipboard_path(str(Path(directory.name) / "clipboard.usda"))
        clipboard.clear_clipboard()
        return clipboard

    def test_cut_prims_copies_subtree_and_restores_authored_contents(self):
        clipboard = self.isolate_clipboard()
        self.app.set_prim_selection([Sdf.Path("/World/Target")])
        before = self.snapshot_state()
        self.assertTrue(cmds.cut_prims([Sdf.Path("/World/Item")]).is_successful())
        self.assertFalse(self.stage.GetPrimAtPath("/World/Item"))
        copied = clipboard.get_clipboard_stage().GetPrimAtPath("/Item")
        self.assertTrue(copied.GetChild("Child"))
        self.assertEqual(copied.GetAttribute("value").Get(), 7)
        self.assert_undo_redo_restores_state(before)

    def test_copy_and_paste_prims_preserve_subtree_and_undo_authored_contents(self):
        self.isolate_clipboard()
        before_copy = self.snapshot_state()
        self.assertTrue(cmds.copy_prims([Sdf.Path("/World/Item")]).is_successful())
        self.assertEqual(self.snapshot_state(), before_copy)
        self.assertEqual(self.stack.get_size(), 1)
        self.stack.clear()
        layers_before_paste = self.snapshot_layers()
        self.assertTrue(cmds.paste_prims(Sdf.Path("/World/Target")).is_successful())
        pasted = self.stage.GetPrimAtPath("/World/Target/Item")
        self.assertTrue(pasted.GetChild("Child"))
        self.assertEqual(pasted.GetAttribute("value").Get(), 7)
        self.assertEqual(pasted.GetRelationship("target").GetTargets(), [Sdf.Path("/World/Target")])
        self.assertEqual(self.app.get_prim_selection(), [pasted.GetPath()])
        layers_after_paste = self.snapshot_layers()
        self.assertNotEqual(layers_before_paste, layers_after_paste)
        self.assertEqual(self.stack.get_size(), 1)
        for _ in range(self.UNDO_REDO_CYCLES):
            self.stack.undo()
            self.assertEqual(self.snapshot_layers(), layers_before_paste)
            self.assertFalse(self.stack.can_undo())
            self.stack.redo()
            self.assertEqual(self.snapshot_layers(), layers_after_paste)
            self.assertFalse(self.stack.can_redo())
            self.assertEqual(self.stack.get_size(), 1)

    def test_copy_and_paste_attribute_values_metadata_and_time_samples(self):
        self.isolate_clipboard()
        source = self.stage.GetPrimAtPath("/World/Item").GetAttribute("value")
        source.Set(11, 1)
        source.Set(19, 2)
        source.SetMetadata("documentation", "Copied attribute")
        destination = self.stage.GetPrimAtPath("/World/Target").CreateAttribute(
            "value", Sdf.ValueTypeNames.Double
        )
        destination.Set(-1)
        before_copy = self.snapshot_state()
        self.stack.clear()
        self.assertTrue(cmds.copy_attr(source.GetPath()).is_successful())
        self.assertEqual(self.snapshot_state(), before_copy)
        self.assertEqual(self.stack.get_size(), 1)
        for option in ("paste_value", "paste_metadata", "paste_time_samples"):
            with self.subTest(option=option):
                self.stack.clear()
                before = self.snapshot_state()
                self.assertTrue(
                    cmds.paste_attr(destination.GetPath(), **{option: True}).is_successful()
                )
                if option == "paste_value":
                    self.assertEqual(destination.Get(), 7)
                elif option == "paste_metadata":
                    self.assertEqual(destination.GetMetadata("documentation"), "Copied attribute")
                else:
                    self.assertEqual(destination.GetTimeSamples(), [1, 2])
                    self.assertEqual(destination.Get(1), 11)
                    self.assertEqual(destination.Get(2), 19)
                self.assert_undo_redo_restores_state(before)
                self.stack.undo()

    def assert_undo_redo_restores_state(self, before):
        after = self.snapshot_state()
        self.assertNotEqual(before, after)
        self.assertEqual(self.stack.get_size(), 1)
        for _ in range(self.UNDO_REDO_CYCLES):
            self.stack.undo()
            self.assertEqual(self.snapshot_state(), before)
            self.assertFalse(self.stack.can_undo())
            self.stack.redo()
            self.assertEqual(self.snapshot_state(), after)
            self.assertFalse(self.stack.can_redo())
            self.assertEqual(self.stack.get_size(), 1)

    def test_create_prim_returns_path_and_restores_selection(self):
        before = self.snapshot_state()
        result = cmds.create_prim("Created", "Xform", parent=Sdf.Path("/World"))
        self.assertTrue(result.is_successful())
        path = result.get_result()
        self.assertEqual(path, Sdf.Path("/World/Created"))
        self.assertEqual(self.stage.GetPrimAtPath(path).GetTypeName(), "Xform")
        self.assertEqual(self.app.get_prim_selection(), [path])
        self.assert_undo_redo_restores_state(before)

    def test_create_collision_uses_unique_name_without_changing_selection(self):
        before = self.snapshot_state()
        _, selection_before_create = before
        result = cmds.create_prim(
            "Item", "Xform", parent=Sdf.Path("/World"), change_selection=False
        )
        self.assertTrue(result.is_successful())
        path = result.get_result()
        self.assertNotEqual(path, Sdf.Path("/World/Item"))
        self.assertTrue(self.stage.GetPrimAtPath(path))
        self.assertEqual(self.app.get_selection(), selection_before_create)
        self.assert_undo_redo_restores_state(before)

    def test_duplicate_preserves_subtree_attributes_and_external_targets(self):
        before = self.snapshot_state()
        result = cmds.duplicate_prim(paths=[Sdf.Path("/World/Item")])
        self.assertTrue(result.is_successful())
        paths = result.get_result()
        self.assertEqual(len(paths), 1)
        self.assertNotEqual(paths[0], Sdf.Path("/World/Item"))
        duplicate = self.stage.GetPrimAtPath(paths[0])
        self.assertEqual(duplicate.GetAttribute("value").Get(), 7)
        self.assertTrue(duplicate.GetChild("Child"))
        self.assertEqual(
            duplicate.GetRelationship("target").GetTargets(), [Sdf.Path("/World/Target")]
        )
        self.assert_undo_redo_restores_state(before)

    def test_remove_unselected_prims_restores_subtree_and_preserves_selection(self):
        self.app.set_prim_selection([Sdf.Path("/World/Target")])
        before = self.snapshot_state()
        result = cmds.remove_prims(Sdf.Path("/World/Item"))
        self.assertTrue(result.is_successful())
        self.assertFalse(self.stage.GetPrimAtPath("/World/Item"))
        self.assertFalse(self.stage.GetPrimAtPath("/World/Item/Child"))
        self.assertTrue(self.stage.GetPrimAtPath("/World/Target"))
        self.assert_undo_redo_restores_state(before)

    def test_rename_updates_relationships_and_selected_descendants(self):
        self.app.set_prim_selection([Sdf.Path("/World/Item/Child")])
        observer = self.stage.GetPrimAtPath("/World/Target").CreateRelationship("item")
        observer.SetTargets([Sdf.Path("/World/Item/Child")])
        before = self.snapshot_state()
        result = cmds.rename_prim("Renamed", path=Sdf.Path("/World/Item"))
        self.assertTrue(result.is_successful())
        self.assertEqual(result.get_result(), Sdf.Path("/World/Renamed"))
        self.assertFalse(self.stage.GetPrimAtPath("/World/Item"))
        self.assertTrue(self.stage.GetPrimAtPath("/World/Renamed/Child"))
        self.assertEqual(observer.GetTargets(), [Sdf.Path("/World/Renamed/Child")])
        self.assertEqual(self.app.get_prim_selection(), [Sdf.Path("/World/Renamed/Child")])
        self.assert_undo_redo_restores_state(before)

    def test_parent_preserves_world_transform(self):
        UsdGeom.Xformable(self.stage.GetPrimAtPath("/World")).AddTranslateOp().Set(
            Gf.Vec3d(3, 0, 0)
        )
        destination = UsdGeom.Xform.Define(self.stage, "/Destination")
        destination.AddTranslateOp().Set(Gf.Vec3d(-5, 0, 0))
        item = self.stage.GetPrimAtPath("/World/Item")
        transform = UsdGeom.XformCache().GetLocalToWorldTransform(item)
        before = self.snapshot_state()
        result = cmds.parent_prim(
            Sdf.Path("/Destination"), paths=[item.GetPath()], preserve_transform=True
        )
        self.assertTrue(result.is_successful())
        new_path = Sdf.Path("/Destination/Item")
        self.assertEqual(result.get_result(), [new_path])
        self.assertEqual(
            UsdGeom.XformCache().GetLocalToWorldTransform(self.stage.GetPrimAtPath(new_path)),
            transform,
        )
        self.assert_undo_redo_restores_state(before)

    def test_group_creates_parent_and_restores_original_hierarchy(self):
        before = self.snapshot_state()
        result = cmds.group_prim(paths=[Sdf.Path("/World/Item"), Sdf.Path("/World/Target")])
        self.assertTrue(result.is_successful())
        selection = self.app.get_prim_selection()
        self.assertEqual(len(selection), 1)
        group = self.stage.GetPrimAtPath(selection[0])
        self.assertEqual(group.GetTypeName(), "Xform")
        self.assertEqual({child.GetName() for child in group.GetChildren()}, {"Item", "Target"})
        self.assert_undo_redo_restores_state(before)

    def test_failed_read_only_rename_cannot_be_applied_by_undo(self):
        before = self.snapshot_state()
        layer = self.stage.GetRootLayer()
        layer.SetPermissionToEdit(False)
        try:
            result = cmds.rename_prim("Renamed", path=Sdf.Path("/World/Item"))
            self.assertFalse(result.is_successful())
            self.assertEqual(self.stack.get_size(), 0)
            self.assertEqual(self.snapshot_state(), before)
        finally:
            layer.SetPermissionToEdit(True)
        self.stack.undo()
        self.assertEqual(self.snapshot_state(), before)
        self.assertTrue(self.stage.GetPrimAtPath("/World/Item"))
        self.assertFalse(self.stage.GetPrimAtPath("/World/Renamed"))

    def test_selection_replace_add_remove_clear_are_undoable(self):
        for options, expected in (
            ({}, [Sdf.Path("/World/Target")]),
            ({"add": True}, [Sdf.Path("/World/Item"), Sdf.Path("/World/Target")]),
            ({"remove": True}, [Sdf.Path("/World/Item")]),
            ({"clear": True}, []),
        ):
            with self.subTest(options=options):
                self.app.set_prim_selection([Sdf.Path("/World/Item")])
                self.stack.clear()
                before = self.snapshot_state()
                result = cmds.select(core.SelectionList([Sdf.Path("/World/Target")]), **options)
                self.assertTrue(result.is_successful())
                self.assertEqual(set(self.app.get_prim_selection()), set(expected))
                if self.snapshot_state() != before:
                    self.assert_undo_redo_restores_state(before)

    def test_move_restores_authored_transform(self):
        prim = UsdGeom.Xform.Define(self.stage, "/Move").GetPrim()
        self.stack.clear()
        before = self.snapshot_state()
        translation = Gf.Vec3d(1, 2, 3)
        result = cmds.move(translation, objects=core.SelectionList([prim.GetPath()]))
        self.assertTrue(result.is_successful())
        matrix = UsdGeom.XformCache().GetLocalToWorldTransform(prim)
        actual_translation = matrix.ExtractTranslation()
        self.assertTrue(
            Gf.IsClose(actual_translation, translation, self.TRANSFORM_TOLERANCE),
            (actual_translation, translation),
        )
        self.assert_undo_redo_restores_state(before)

    def test_rotate_restores_authored_transform(self):
        prim = UsdGeom.Xform.Define(self.stage, "/Rotate").GetPrim()
        self.stack.clear()
        before = self.snapshot_state()
        quarter_turn_about_z = Gf.Rotation(Gf.Vec3d(0, 0, 1), 90)
        result = cmds.rotate(quarter_turn_about_z, objects=core.SelectionList([prim.GetPath()]))
        self.assertTrue(result.is_successful())
        matrix = UsdGeom.XformCache().GetLocalToWorldTransform(prim)
        rotated_x_axis = matrix.TransformDir(Gf.Vec3d(1, 0, 0))
        expected_y_axis = Gf.Vec3d(0, 1, 0)
        self.assertTrue(
            Gf.IsClose(rotated_x_axis, expected_y_axis, self.TRANSFORM_TOLERANCE),
            (rotated_x_axis, expected_y_axis),
        )
        self.assert_undo_redo_restores_state(before)

    def test_scale_restores_authored_transform(self):
        prim = UsdGeom.Xform.Define(self.stage, "/Scale").GetPrim()
        self.stack.clear()
        before = self.snapshot_state()
        scale = Gf.Vec3d(2, 3, 4)
        result = cmds.scale(scale, objects=core.SelectionList([prim.GetPath()]))
        self.assertTrue(result.is_successful())
        matrix = UsdGeom.XformCache().GetLocalToWorldTransform(prim)
        actual_scale = Gf.Transform(matrix).GetScale()
        self.assertTrue(
            Gf.IsClose(actual_scale, scale, self.TRANSFORM_TOLERANCE), (actual_scale, scale)
        )
        self.assert_undo_redo_restores_state(before)

    def test_invalid_arguments_preserve_scene_selection_and_history(self):
        for scenario, execute in (
            ("invalid prim name", lambda: cmds.create_prim("invalid name", "Xform")),
            (
                "missing create parent",
                lambda: cmds.create_prim("Created", "Xform", parent=Sdf.Path("/Missing")),
            ),
            ("missing delete prim", lambda: cmds.remove_prims(Sdf.Path("/Missing"))),
            ("missing rename prim", lambda: cmds.rename_prim("Renamed", path=Sdf.Path("/Missing"))),
            (
                "missing reparent destination",
                lambda: cmds.parent_prim(Sdf.Path("/Missing"), paths=[Sdf.Path("/World/Item")]),
            ),
        ):
            with self.subTest(scenario=scenario):
                before = self.snapshot_state()
                self.assertFalse(execute().is_successful())
                self.assertEqual(self.snapshot_state(), before)
                self.assertEqual(self.stack.get_size(), 0)
        with self.assertRaises(TypeError):
            cmds.create_prim("MissingTypeArgument")
        self.assertEqual(self.stack.get_size(), 0)

    def test_export_selection_writes_only_requested_subtree(self):
        directory = tempfile.TemporaryDirectory(prefix="common-command-export-")
        self.addCleanup(directory.cleanup)
        filename = Path(directory.name) / "selected.usda"
        before = self.snapshot_state()
        result = cmds.export_selection(
            str(filename), paths=[Sdf.Path("/World/Item")], export_connections=False
        )
        self.assertTrue(result.is_successful())
        exported = Usd.Stage.Open(str(filename))
        self.assertTrue(exported)
        self.assertTrue(exported.GetPrimAtPath("/World/Item/Child"))
        self.assertFalse(exported.GetPrimAtPath("/World/Target"))
        self.assertEqual(self.snapshot_state(), before)
        self.assertEqual(self.stack.get_size(), 0)
