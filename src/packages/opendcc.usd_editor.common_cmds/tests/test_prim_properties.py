# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from pxr import Sdf, Usd, UsdGeom
from opendcc import cmds
import unittest
from Qt import QtCore
import opendcc.core as core


def flush():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


class TestPrimProperties(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.undo = self.app.get_undo_stack()
        self.previous = self.session.get_current_stage()
        self.stage = Usd.Stage.CreateInMemory()
        for path in ("/Root", "/Root/Child", "/Root/Payload", "/Other"):
            UsdGeom.Xform.Define(self.stage, path)
        variants = self.stage.GetPrimAtPath("/Root").GetVariantSets().AddVariantSet("look")
        variants.AddVariant("red")
        variants.AddVariant("blue")
        variants.SetVariantSelection("red")
        self.payload_stage = Usd.Stage.CreateInMemory()
        UsdGeom.Xform.Define(self.payload_stage, "/Asset/Inside")
        self.stage.GetPrimAtPath("/Root/Payload").GetPayloads().AddPayload(
            self.payload_stage.GetRootLayer().identifier, Sdf.Path("/Asset")
        )
        self.session.set_current_stage(self.stage)
        self.undo.clear()
        self.addCleanup(self.restore_stage)

    def restore_stage(self):
        self.undo.clear()
        if self.previous:
            self.session.set_current_stage(self.previous)
        self.close_stage(self.stage)
        flush()

    def close_stage(self, stage):
        if stage:
            self.session.close_stage(stage)

    def checked(self, command, *args, **kwargs):
        result = command(*args, **kwargs)
        self.assertTrue(result.is_successful(), command.__name__)
        self.assertTrue(self.undo.can_undo())
        flush()

    def round_trip(self, command, *args, **kwargs):
        target = kwargs.get("stage", self.stage)
        before = target.GetRootLayer().ExportToString()
        rules_before = target.GetLoadRules()
        self.checked(command, *args, **kwargs)
        after = target.GetRootLayer().ExportToString()
        rules_after = target.GetLoadRules()
        self.undo.undo()
        flush()
        self.assertEqual(target.GetRootLayer().ExportToString(), before)
        self.assertEqual(target.GetLoadRules(), rules_before)
        self.undo.redo()
        flush()
        self.assertEqual(target.GetRootLayer().ExportToString(), after)
        self.assertEqual(target.GetLoadRules(), rules_after)

    def test_authored_edits_and_payload_rules_round_trip(self):
        for command, args in (
            (cmds.set_prim_visibility, ([Sdf.Path("/Root")], False)),
            (cmds.set_prim_visibility, ([Sdf.Path("/Root")], True)),
            (cmds.set_prim_active, ([Sdf.Path("/Root")], False)),
            (cmds.set_prim_active, ([Sdf.Path("/Root")], True)),
            (cmds.set_prim_type, (Sdf.Path("/Root"), "Scope")),
            (cmds.set_prim_type, (Sdf.Path("/Root"), "")),
            (cmds.set_prim_kind, (Sdf.Path("/Root"), "component")),
            (cmds.set_prim_kind, (Sdf.Path("/Root"), "")),
            (cmds.set_prim_draw_mode, (Sdf.Path("/Root"), "bounds")),
            (cmds.set_variant_selection, (Sdf.Path("/Root"), "look", "blue")),
            (cmds.set_variant_selection, (Sdf.Path("/Root"), "look", "")),
            (cmds.set_payload_loaded, ([Sdf.Path("/Root/Payload")], False)),
            (cmds.set_payload_loaded, ([Sdf.Path("/Root/Payload")], True)),
        ):
            with self.subTest(command=command.__name__, args=args):
                self.round_trip(command, *args)
        root = self.stage.GetPrimAtPath("/Root")
        self.assertFalse(root.HasAuthoredTypeName())
        self.assertFalse(root.HasAuthoredMetadata("kind"))
        self.assertEqual(root.GetVariantSets().GetVariantSelection("look"), "")

    def test_invalid_targets_fail_without_authoring_or_undo(self):
        for command, args in (
            (cmds.set_prim_active, ([Sdf.Path("/Root"), Sdf.Path("/Missing")], False)),
            (cmds.set_prim_visibility, ([], False)),
            (cmds.set_prim_kind, (Sdf.Path("/"), "component")),
            (cmds.set_prim_type, (Sdf.Path("relative"), "Xform")),
            (cmds.set_prim_kind, (Sdf.Path("/Root.visibility"), "component")),
            (cmds.set_variant_selection, (Sdf.Path("/Root"), "look", "missing")),
            (cmds.set_variant_selection, (Sdf.Path("/Root"), "missing", "red")),
            (cmds.set_payload_loaded, ([Sdf.Path("/Other")], False)),
            (cmds.set_prim_draw_mode, (Sdf.Path("/Other"), "invalid")),
        ):
            with self.subTest(command=command.__name__, args=args):
                before = self.stage.GetRootLayer().ExportToString()
                rules = self.stage.GetLoadRules()
                count = self.undo.get_size()
                self.assertFalse(command(*args).is_successful())
                self.assertEqual(self.stage.GetRootLayer().ExportToString(), before)
                self.assertEqual(self.stage.GetLoadRules(), rules)
                self.assertEqual(self.undo.get_size(), count)

    def test_read_only_target_fails(self):
        self.stage.GetRootLayer().SetPermissionToEdit(False)
        self.addCleanup(self.stage.GetRootLayer().SetPermissionToEdit, True)
        self.assertFalse(cmds.set_prim_kind(Sdf.Path("/Other"), "component").is_successful())
        self.assertFalse(self.undo.can_undo())

    def test_explicit_stage_undo_does_not_edit_current_stage(self):
        second = Usd.Stage.CreateInMemory()
        UsdGeom.Xform.Define(second, "/Second")
        self.addCleanup(self.session.close_stage, second)
        self.session.set_current_stage(second)
        self.round_trip(cmds.set_prim_kind, Sdf.Path("/Other"), "assembly", stage=self.stage)
        self.assertEqual(self.stage.GetPrimAtPath("/Other").GetMetadata("kind"), "assembly")
        self.assertFalse(second.GetPrimAtPath("/Other"))
        self.round_trip(
            cmds.set_payload_loaded, [Sdf.Path("/Root/Payload")], False, stage=self.stage
        )

    def test_no_stage_fails(self):
        self.session.close_all()
        self.assertFalse(cmds.set_prim_kind(Sdf.Path("/Missing"), "component").is_successful())

    def test_visibility_edits_animated_target_or_ancestor_at_current_time(self):
        previous_time = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous_time)
        self.app.set_current_time(2)
        root = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root"))
        visibility = root.GetVisibilityAttr()
        visibility.Set("inherited", Usd.TimeCode(1))
        visibility.Set("invisible", Usd.TimeCode(2))
        self.round_trip(cmds.set_prim_visibility, [Sdf.Path("/Root")], True)
        self.assertEqual(root.ComputeVisibility(Usd.TimeCode(2)), "inherited")
        self.assertEqual(visibility.Get(Usd.TimeCode(1)), "inherited")
        self.undo.undo()
        child = UsdGeom.Imageable(self.stage.GetPrimAtPath("/Root/Child"))
        self.round_trip(cmds.set_prim_visibility, [child.GetPath()], True)
        self.assertEqual(child.ComputeVisibility(Usd.TimeCode(2)), "inherited")
        self.assertEqual(visibility.Get(Usd.TimeCode(1)), "inherited")

    def test_overlapping_activation_targets_restore_children(self):
        self.round_trip(cmds.set_prim_active, [Sdf.Path("/Root"), Sdf.Path("/Root/Child")], False)
        self.undo.undo()
        flush()
        self.assertTrue(self.stage.GetPrimAtPath("/Root/Child"))
