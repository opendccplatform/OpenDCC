# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import unittest

import opendcc.core as core
from pxr import Sdf, Usd


class TestApplicationCallbacks(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        self.addCleanup(self.session.close_all)

    def listen(self, event, callback):
        handle = self.app.register_event_callback(event, callback)
        self.assertIsInstance(handle, core.Application.CallbackHandle)
        self.addCleanup(self.app.unregister_event_callback, event, handle)
        return handle

    def test_all_events_accept_enum_and_string_registration(self):
        for name, event in core.Application.EventType.__members__.items():
            with self.subTest(event=name):
                handle = self.app.register_event_callback(event, lambda: None)
                self.assertIsInstance(handle, core.Application.CallbackHandle)
                self.app.unregister_event_callback(name.lower(), handle)
                handle = self.app.register_event_callback(name.lower(), lambda: None)
                self.assertIsInstance(handle, core.Application.CallbackHandle)
                self.app.unregister_event_callback(event, handle)

    def test_enum_callback_can_be_removed_by_name(self):
        previous = self.app.get_current_time()
        self.addCleanup(self.app.set_current_time, previous)
        times = []
        event = core.Application.EventType.CURRENT_TIME_CHANGED
        handle = self.listen(event, lambda: times.append(self.app.get_current_time()))
        self.app.set_current_time(12)
        self.app.unregister_event_callback("current_time_changed", handle)
        self.app.set_current_time(24)
        self.assertEqual(times, [12])

    def test_unknown_event_names_are_rejected(self):
        handle = self.listen("current_time_changed", lambda: None)
        with self.assertRaisesRegex(ValueError, "Unknown application event"):
            self.app.register_event_callback("not_an_event", lambda: None)
        with self.assertRaisesRegex(ValueError, "Unknown application event"):
            self.app.unregister_event_callback("not_an_event", handle)

    def test_selection_mode_callbacks_observe_new_mode(self):
        previous = self.app.get_selection_mode()
        self.addCleanup(self.app.set_selection_mode, previous)
        modes, selections = [], []
        self.listen("selection_mode_changed", lambda: modes.append(self.app.get_selection_mode()))
        self.listen("selection_changed", lambda: selections.append(self.app.get_selection()))
        self.app.set_selection_mode(core.Application.SelectionMode.POINTS)
        self.app.set_selection_mode(core.Application.SelectionMode.PRIMS)
        self.assertEqual(
            modes, [core.Application.SelectionMode.POINTS, core.Application.SelectionMode.PRIMS]
        )
        self.assertEqual(len(selections), 2)

    def test_layer_selection_only_notifies_on_change(self):
        previous = self.app.get_layer_selection()
        self.addCleanup(self.app.set_layer_selection, previous)
        layer = Sdf.Layer.CreateAnonymous()
        changes = []
        self.listen(
            "layer_selection_changed", lambda: changes.append(self.app.get_layer_selection())
        )
        self.app.set_layer_selection({layer})
        self.app.set_layer_selection({layer})
        self.app.set_layer_selection(set())
        self.assertEqual(changes, [{layer}, set()])

    def test_scene_context_only_notifies_on_change(self):
        previous = self.app.get_active_view_scene_context()
        self.addCleanup(self.app.set_active_view_scene_context, previous)
        changes = []
        self.listen(
            "active_view_scene_context_changed",
            lambda: changes.append(self.app.get_active_view_scene_context()),
        )
        self.app.set_active_view_scene_context("callback_test")
        self.app.set_active_view_scene_context("callback_test")
        self.app.set_active_view_scene_context(previous)
        self.assertEqual(changes, ["callback_test", previous])

    def test_stage_lifecycle_callbacks(self):
        lists, stages, closing, targets = [], [], [], []
        self.listen(
            "session_stage_list_changed", lambda: lists.append(self.session.get_stage_list())
        )
        self.listen(
            "current_stage_changed", lambda: stages.append(self.session.get_current_stage())
        )
        self.listen(
            "before_current_stage_closed", lambda: closing.append(self.session.get_current_stage())
        )
        self.listen("edit_target_changed", lambda: targets.append(self.session.get_current_stage()))
        stage = Usd.Stage.CreateInMemory()
        self.session.set_current_stage(stage)
        self.assertEqual(lists, [[stage]])
        self.assertEqual(stages, [stage])
        self.assertEqual(targets, [stage])
        self.session.close_stage(stage)
        # The before-close callback must still be able to inspect the closing stage.
        self.assertEqual(closing, [stage])
        self.assertEqual(lists[-1], [])
        self.assertIsNone(stages[-1])

    def test_edit_target_watchers_follow_current_stage(self):
        stage = Usd.Stage.CreateInMemory()
        layer = Sdf.Layer.CreateAnonymous()
        stage.GetRootLayer().subLayerPaths.append(layer.identifier)
        self.session.set_current_stage(stage)
        targets = []
        self.listen("edit_target_changed", lambda: targets.append(stage.GetEditTarget().GetLayer()))
        stage.SetEditTarget(layer)
        self.assertEqual(targets, [layer])
        self.session.set_current_stage(Usd.Stage.CreateInMemory())
        targets.clear()
        layer.Clear()
        stage.SetEditTarget(stage.GetRootLayer())
        self.assertEqual(targets, [])
