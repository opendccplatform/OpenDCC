# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import unittest

import opendcc.core as core
from pxr import Sdf, Vt

COMPONENTS = (
    ("points", "point_indices", core.MergeFlags.POINTS),
    ("edges", "edge_indices", core.MergeFlags.EDGES),
    ("elements", "element_indices", core.MergeFlags.ELEMENTS),
    ("instances", "instance_indices", core.MergeFlags.INSTANCES),
)


class TestSelectionData(unittest.TestCase):
    def test_empty_and_full_selection(self):
        self.assertTrue(core.SelectionData().empty())
        selected = core.SelectionData(full=True)
        self.assertFalse(selected.empty())
        self.assertTrue(selected.fully_selected)

    def test_component_intervals(self):
        # Interval endpoints are inclusive, even when supplied in reverse order.
        data = core.SelectionData(points=[5, (3, 1), 2, 5])
        self.assertEqual(list(data.point_indices), [1, 2, 3, 5])
        self.assertFalse(data.fully_selected)

    def test_usd_arrays_and_properties(self):
        data = core.SelectionData(
            points=Vt.UIntArray([2, 0, 2]),
            edges=[1],
            elements=[3],
            instances=[4],
            properties=Vt.TokenArray(["visibility", "purpose", "visibility"]),
        )
        self.assertEqual(list(data.point_indices), [0, 2])
        self.assertEqual(list(data.edge_indices), [1])
        self.assertEqual(list(data.element_indices), [3])
        self.assertEqual(list(data.instance_indices), [4])
        self.assertEqual(set(data.properties), {"visibility", "purpose"})

    def test_vt_component_constructors(self):
        for component, attribute, _ in COMPONENTS:
            for array_type in (Vt.IntArray, Vt.UIntArray):
                with self.subTest(component=component, array_type=array_type):
                    values = array_type([5, 0, 2, 2])
                    data = core.SelectionData(**{component: values})
                    self.assertEqual(list(getattr(data, attribute)), [0, 2, 5])
                    self.assertIsInstance(getattr(data, attribute), Vt.UIntArray)
                    self.assertEqual(list(values), [5, 0, 2, 2])
                    values[0] = 9
                    self.assertEqual(list(getattr(data, attribute)), [0, 2, 5])

    def test_empty_vt_arrays(self):
        for component, _, _ in COMPONENTS:
            for array_type in (Vt.IntArray, Vt.UIntArray):
                with self.subTest(component=component, array_type=array_type):
                    self.assertTrue(core.SelectionData(**{component: array_type()}).empty())
        self.assertTrue(core.SelectionData(properties=Vt.TokenArray()).empty())

    def test_vt_properties_are_independent(self):
        values = Vt.TokenArray(["visibility", "purpose", "visibility"])
        data = core.SelectionData(properties=values)
        values[0] = "changed"
        self.assertEqual(set(data.properties), {"visibility", "purpose"})

    def test_returned_vt_arrays_are_snapshots(self):
        for component, attribute, _ in COMPONENTS:
            with self.subTest(component=component):
                data = core.SelectionData(**{component: Vt.UIntArray([0, 3])})
                returned = getattr(data, attribute)
                returned[0] = 9
                self.assertEqual(list(getattr(data, attribute)), [0, 3])

    def test_unsigned_vt_index_boundaries(self):
        values = Vt.UIntArray([2**32 - 1, 0, 2**31])
        data = core.SelectionData(points=values)
        self.assertEqual(list(data.point_indices), [0, 2**31, 2**32 - 1])


class TestSelectionList(unittest.TestCase):
    def setUp(self):
        self.mesh = Sdf.Path("/World/Mesh")
        self.other = Sdf.Path("/World/Other")

    def test_prim_paths_are_unique(self):
        selection = core.SelectionList([self.mesh, self.other, self.mesh])
        self.assertEqual(len(selection), 2)
        self.assertIn(self.mesh, selection)
        self.assertEqual(set(selection.get_fully_selected_paths()), {self.mesh, self.other})
        selection.remove_prims([self.other])
        self.assertEqual(list(selection.get_selected_paths()), [self.mesh])
        selection.clear()
        self.assertTrue(selection.empty())

    def test_mapping_and_equality(self):
        data = core.SelectionData(points=[1, 3])
        selection = core.SelectionList({self.mesh: data})
        self.assertEqual(selection[self.mesh], data)
        self.assertEqual(selection, core.SelectionList({self.mesh: data}))
        selection[self.other] = core.SelectionData(full=True)
        self.assertEqual(set(selection.get_selected_paths()), {self.mesh, self.other})
        self.assertEqual(list(selection.get_fully_selected_paths()), [self.other])

    def test_add_and_remove_components(self):
        for component, attribute in (
            ("points", "point_indices"),
            ("edges", "edge_indices"),
            ("elements", "element_indices"),
            ("instances", "instance_indices"),
        ):
            with self.subTest(component=component):
                selection = core.SelectionList()
                getattr(selection, "add_" + component)(self.mesh, Vt.IntArray([3, 1, 3]))
                self.assertEqual(list(getattr(selection[self.mesh], attribute)), [1, 3])
                getattr(selection, "remove_" + component)(self.mesh, Vt.IntArray([1]))
                self.assertEqual(list(getattr(selection[self.mesh], attribute)), [3])
                getattr(selection, "remove_" + component)(self.mesh, Vt.IntArray([3]))
                self.assertTrue(selection.empty())

    def test_merge_and_difference_mask(self):
        selection = core.SelectionList({self.mesh: core.SelectionData(points=[1], edges=[2])})
        incoming = core.SelectionList({self.mesh: core.SelectionData(points=[3], edges=[4])})
        selection.merge(incoming, core.MergeFlags.POINTS)
        self.assertEqual(list(selection[self.mesh].point_indices), [1, 3])
        self.assertEqual(list(selection[self.mesh].edge_indices), [2])
        selection.difference(incoming, core.MergeFlags.POINTS)
        self.assertEqual(list(selection[self.mesh].point_indices), [1])
        self.assertEqual(list(selection[self.mesh].edge_indices), [2])
        self.assertEqual(list(incoming[self.mesh].point_indices), [3])

    def test_vt_component_edits_preserve_other_paths_and_data(self):
        for component, attribute, _ in COMPONENTS:
            with self.subTest(component=component):
                selection = core.SelectionList([self.mesh, self.other])
                selection.add_properties(self.mesh, Vt.TokenArray(["visibility"]))
                values = Vt.IntArray([3, 0, 3])
                getattr(selection, "add_" + component)(self.mesh, values)
                getattr(selection, "add_" + component)(self.mesh, Vt.IntArray([2, 3]))
                self.assertEqual(list(getattr(selection[self.mesh], attribute)), [0, 2, 3])
                self.assertEqual(list(values), [3, 0, 3])
                getattr(selection, "remove_" + component)(self.mesh, Vt.IntArray([2, 2, 99]))
                self.assertEqual(list(getattr(selection[self.mesh], attribute)), [0, 3])
                # Removing the last component must leave whole-prim selection and properties intact.
                getattr(selection, "remove_" + component)(self.mesh, Vt.IntArray([0, 3]))
                self.assertTrue(selection[self.mesh].fully_selected)
                self.assertEqual(set(selection[self.mesh].properties), {"visibility"})
                self.assertTrue(selection[self.other].fully_selected)

    def test_empty_arrays_and_missing_paths_are_noops(self):
        selection = core.SelectionList([self.mesh])
        for component, _, _ in COMPONENTS:
            with self.subTest(component=component):
                getattr(selection, "add_" + component)(self.other, Vt.IntArray())
                getattr(selection, "remove_" + component)(self.mesh, Vt.IntArray())
                getattr(selection, "remove_" + component)(self.other, Vt.IntArray([1]))
                self.assertEqual(selection, core.SelectionList([self.mesh]))
        selection.add_properties(self.other, Vt.TokenArray())
        selection.remove_properties(self.other, Vt.TokenArray(["visibility"]))
        selection.remove_properties(self.mesh, Vt.TokenArray())
        self.assertEqual(selection, core.SelectionList([self.mesh]))
        self.assertTrue(selection.get_selection_data(self.other).empty())
        self.assertNotIn(self.other, selection)

    def test_property_add_remove_with_vt_tokens(self):
        selection = core.SelectionList()
        selection.add_properties(self.mesh, Vt.TokenArray(["visibility", "purpose", "visibility"]))
        selection.add_properties(self.mesh, Vt.TokenArray(["purpose", "extent"]))
        self.assertEqual(set(selection[self.mesh].properties), {"visibility", "purpose", "extent"})
        selection.remove_properties(self.mesh, Vt.TokenArray(["purpose", "purpose", "missing"]))
        self.assertEqual(set(selection[self.mesh].properties), {"visibility", "extent"})
        selection.remove_properties(self.mesh, Vt.TokenArray(["visibility", "extent"]))
        self.assertTrue(selection.empty())

    def test_property_removal_preserves_components(self):
        selection = core.SelectionList({self.mesh: core.SelectionData(points=Vt.UIntArray([2]))})
        selection.add_properties(self.mesh, Vt.TokenArray(["visibility"]))
        selection.remove_properties(self.mesh, Vt.TokenArray(["visibility"]))
        self.assertEqual(list(selection[self.mesh].point_indices), [2])
        self.assertEqual(set(selection[self.mesh].properties), set())

    def test_component_masks_with_vt_arrays(self):
        initial = core.SelectionData(
            points=Vt.UIntArray([0, 2]),
            edges=Vt.UIntArray([0, 2]),
            elements=Vt.UIntArray([0, 2]),
            instances=Vt.UIntArray([0, 2]),
            properties=Vt.TokenArray(["visibility"]),
        )
        incoming_data = core.SelectionData(
            full=True,
            points=Vt.UIntArray([2, 3]),
            edges=Vt.UIntArray([2, 3]),
            elements=Vt.UIntArray([2, 3]),
            instances=Vt.UIntArray([2, 3]),
            properties=Vt.TokenArray(["purpose"]),
        )
        for _, attribute, mask in COMPONENTS:
            with self.subTest(mask=mask):
                selection = core.SelectionList({self.mesh: initial})
                incoming = core.SelectionList({self.mesh: incoming_data, self.other: incoming_data})
                # The incoming whole-prim selection and properties are outside this mask.
                selection.merge(incoming, mask)
                self.assertFalse(selection[self.mesh].fully_selected)
                self.assertEqual(set(selection[self.mesh].properties), {"visibility"})
                for _, other_attribute, _ in COMPONENTS:
                    expected = [0, 2, 3] if attribute == other_attribute else [0, 2]
                    self.assertEqual(list(getattr(selection[self.mesh], other_attribute)), expected)
                self.assertEqual(list(getattr(selection[self.other], attribute)), [2, 3])
                # The other path has only this component, so subtracting it removes the path.
                selection.difference(incoming, mask)
                self.assertEqual(list(getattr(selection[self.mesh], attribute)), [0])
                self.assertNotIn(self.other, selection)
                self.assertEqual(incoming[self.mesh], incoming_data)

    def test_merge_difference_all_and_none(self):
        initial = core.SelectionData(
            points=Vt.UIntArray([0, 2]), properties=Vt.TokenArray(["visibility"])
        )
        data = core.SelectionData(
            full=True, points=Vt.UIntArray([2, 3]), properties=Vt.TokenArray(["purpose"])
        )
        selection = core.SelectionList({self.mesh: initial})
        incoming = core.SelectionList({self.mesh: data, self.other: data})
        selection.merge(incoming, core.MergeFlags.NONE)
        selection.difference(incoming, core.MergeFlags.NONE)
        self.assertEqual(selection, core.SelectionList({self.mesh: initial}))
        selection.merge(incoming)
        self.assertTrue(selection[self.mesh].fully_selected)
        self.assertEqual(list(selection[self.mesh].point_indices), [0, 2, 3])
        self.assertEqual(set(selection[self.mesh].properties), {"visibility", "purpose"})
        selection.difference(incoming)
        self.assertFalse(selection[self.mesh].fully_selected)
        self.assertEqual(list(selection[self.mesh].point_indices), [0])
        self.assertEqual(set(selection[self.mesh].properties), {"visibility"})
        self.assertNotIn(self.other, selection)
        selection.difference(core.SelectionList({self.mesh: initial}))
        self.assertTrue(selection.empty())

    def test_full_selection_mask_preserves_vt_data(self):
        data = core.SelectionData(
            points=Vt.UIntArray([2]), properties=Vt.TokenArray(["visibility"])
        )
        selection = core.SelectionList({self.mesh: data})
        incoming = core.SelectionList([self.mesh, self.other])
        selection.merge(incoming, core.MergeFlags.FULL_SELECTION)
        self.assertTrue(selection[self.mesh].fully_selected)
        selection.difference(incoming, core.MergeFlags.FULL_SELECTION)
        self.assertEqual(selection[self.mesh], data)
        self.assertNotIn(self.other, selection)

    def test_selected_paths_cache_updates_after_vt_edits(self):
        selection = core.SelectionList([self.mesh])
        # Populate the cache before adding or removing paths through component edits.
        self.assertEqual(list(selection.get_selected_paths()), [self.mesh])
        selection.add_points(self.other, Vt.IntArray([2]))
        self.assertEqual(set(selection.get_selected_paths()), {self.mesh, self.other})
        selection.remove_points(self.other, Vt.IntArray([2]))
        self.assertEqual(list(selection.get_selected_paths()), [self.mesh])
        selection.add_properties(self.other, Vt.TokenArray(["visibility"]))
        self.assertEqual(set(selection.get_selected_paths()), {self.mesh, self.other})
        selection.remove_properties(self.other, Vt.TokenArray(["visibility"]))
        self.assertEqual(list(selection.get_selected_paths()), [self.mesh])

    def test_path_data_replace_and_clear_with_vt_arrays(self):
        selection = core.SelectionList([self.mesh, self.other])
        data = core.SelectionData(
            points=Vt.UIntArray([2]), properties=Vt.TokenArray(["visibility"])
        )
        selection.set_selection_data(self.mesh, data)
        self.assertEqual(selection.get_selection_data(self.mesh), data)
        replacement = core.SelectionData(edges=Vt.UIntArray([3]))
        selection[self.mesh] = replacement
        self.assertEqual(selection[self.mesh], replacement)
        selection.set_selection_data(self.mesh, core.SelectionData(points=Vt.UIntArray()))
        self.assertNotIn(self.mesh, selection)
        self.assertEqual(list(selection.get_selected_paths()), [self.other])


class TestApplicationSelection(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        previous_mode = self.app.get_selection_mode()
        previous_selection = self.app.get_selection()
        # Cleanups run in reverse order: clear, restore the mode, then restore the selection.
        self.addCleanup(self.app.set_selection, previous_selection)
        self.addCleanup(self.app.set_selection_mode, previous_mode)
        self.addCleanup(self.app.clear_prim_selection)
        self.app.clear_prim_selection()
        self.app.set_selection_mode(core.Application.SelectionMode.PRIMS)
        self.mesh = Sdf.Path("/World/Mesh")

    def test_prim_selection_replace_and_clear(self):
        self.app.set_prim_selection([self.mesh])
        self.assertEqual(list(self.app.get_prim_selection()), [self.mesh])
        self.assertTrue(self.app.get_selection()[self.mesh].fully_selected)
        self.app.set_prim_selection([Sdf.Path("/World/Other")])
        self.assertNotIn(self.mesh, self.app.get_selection())
        self.app.clear_prim_selection()
        self.assertTrue(self.app.get_selection().empty())

    def test_selection_snapshot_is_independent(self):
        selection = core.SelectionList({self.mesh: core.SelectionData(points=[1, 2])})
        self.app.set_selection(selection)
        selection.clear()
        snapshot = self.app.get_selection()
        self.assertEqual(list(snapshot[self.mesh].point_indices), [1, 2])
        snapshot.add_points(self.mesh, Vt.IntArray([3]))
        self.assertEqual(list(self.app.get_selection()[self.mesh].point_indices), [1, 2])

    def test_prim_selection_switches_component_mode(self):
        self.app.set_selection_mode(core.Application.SelectionMode.POINTS)
        self.app.set_prim_selection([self.mesh])
        self.assertEqual(self.app.get_selection_mode(), core.Application.SelectionMode.PRIMS)
        self.assertEqual(list(self.app.get_prim_selection()), [self.mesh])
        self.assertTrue(self.app.get_selection()[self.mesh].fully_selected)

    def test_selection_changed_notification(self):
        notifications = []
        handle = self.app.register_event_callback(
            "selection_changed", lambda: notifications.append(True)
        )
        self.addCleanup(self.app.unregister_event_callback, "selection_changed", handle)
        self.app.set_prim_selection([self.mesh])
        self.app.clear_prim_selection()
        self.assertEqual(len(notifications), 2)
