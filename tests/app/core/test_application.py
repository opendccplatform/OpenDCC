# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import tempfile
import unittest

import opendcc.core as core
from pxr import Sdf, Usd


class TestApplication(unittest.TestCase):
    def test_instance_and_owned_services(self):
        app = core.Application.instance()
        other = core.Application.instance()
        self.assertIs(app, other)
        self.assertIs(app.get_session(), other.get_session())
        self.assertIs(app.get_settings(), other.get_settings())
        # Python borrows these objects; their lifetime belongs to the application.
        for cls in (core.Application, core.Session):
            with self.subTest(cls=cls):
                with self.assertRaises(TypeError):
                    cls()

    def test_headless_instance(self):
        app = core.Application.instance()
        self.assertFalse(app.is_ui_available())
        self.assertIsNone(app.get_main_window())
        self.assertIsNone(app.get_active_view())

    def test_time_and_callback_lifetime(self):
        app = core.Application.instance()
        previous_time = app.get_current_time()
        self.addCleanup(app.set_current_time, previous_time)
        times = []
        handle = app.register_event_callback(
            "current_time_changed", lambda: times.append(app.get_current_time())
        )
        try:
            app.set_current_time(12.5)
            self.assertEqual(core.Application.instance().get_current_time(), 12.5)
            app.set_current_time(24)
            self.assertEqual(times, [12.5, 24])
        finally:
            app.unregister_event_callback("current_time_changed", handle)
        app.set_current_time(30)
        self.assertEqual(times, [12.5, 24])


class TestSession(unittest.TestCase):
    def setUp(self):
        self.app = core.Application.instance()
        self.session = self.app.get_session()
        # This suite owns the stages in its headless application process.
        self.assertEqual(self.session.get_stage_list(), [])
        self.addCleanup(self.session.close_all)

    def test_empty_session(self):
        self.assertIsNone(self.session.get_current_stage())
        self.assertFalse(self.session.get_current_stage_id().IsValid())
        self.assertFalse(self.session.close_stage(Usd.StageCache.Id()))
        self.session.close_all()
        self.assertEqual(self.session.get_stage_list(), [])

    def test_stage_and_id_round_trip(self):
        first = Usd.Stage.CreateInMemory()
        second = Usd.Stage.CreateInMemory()
        self.session.set_current_stage(first)
        first_id = self.session.get_stage_id(first)
        self.assertTrue(first_id.IsValid())
        self.assertEqual(self.session.get_current_stage_id(), first_id)
        self.assertEqual(self.session.get_current_stage(), first)
        self.assertTrue(self.session.get_stage_cache().Contains(first))
        self.session.set_current_stage(second)
        self.session.set_current_stage(first_id)
        self.assertEqual(self.session.get_current_stage(), first)
        self.assertEqual(set(self.session.get_stage_list()), {first, second})

    def test_open_layer_and_file(self):
        layer = Sdf.Layer.CreateAnonymous()
        Sdf.CreatePrimInLayer(layer, "/World")
        stage = self.session.open_stage(layer)
        self.assertEqual(stage.GetRootLayer(), layer)
        self.assertTrue(stage.GetPrimAtPath("/World"))
        self.assertEqual(self.session.get_current_stage(), stage)
        with tempfile.TemporaryDirectory(prefix="session-test-") as directory:
            path = Path(directory) / "stage with spaces.usda"
            self.assertTrue(layer.Export(str(path)))
            opened = self.session.open_stage(str(path))
            self.assertTrue(opened.GetPrimAtPath("/World"))
            self.assertEqual(self.session.get_current_stage(), opened)
            self.assertTrue(self.session.close_stage(opened))

    def test_failed_open_preserves_current_stage(self):
        stage = Usd.Stage.CreateInMemory()
        self.session.set_current_stage(stage)
        with tempfile.TemporaryDirectory(prefix="session-test-") as directory:
            missing = Path(directory) / "missing.usda"
            self.assertIsNone(self.session.open_stage(str(missing)))
        self.assertEqual(self.session.get_current_stage(), stage)
        self.assertEqual(self.session.get_stage_list(), [stage])

    def test_adopts_stage_opened_outside_session(self):
        with tempfile.TemporaryDirectory(prefix="session-test-") as directory:
            path = Path(directory) / "external.usda"
            layer = Sdf.Layer.CreateNew(str(path))
            Sdf.CreatePrimInLayer(layer, "/World")
            layer.Save()
            stage = Usd.Stage.Open(str(path))
            external_cache = Usd.StageCache()
            external_cache.Insert(stage)
            stage.SetEditTarget(stage.GetSessionLayer())
            stage.DefinePrim("/SessionOnly")
            self.assertFalse(self.session.get_stage_cache().Contains(stage))
            self.assertEqual(self.session.get_stage_list(), [])

            self.session.set_current_stage(stage)
            adopted = self.session.get_current_stage()
            self.assertEqual(adopted, stage)
            self.assertEqual(adopted.GetEditTarget().GetLayer(), stage.GetSessionLayer())
            self.assertTrue(adopted.GetPrimAtPath("/SessionOnly"))
            self.assertTrue(self.session.get_stage_id(stage).IsValid())
            self.session.set_current_stage(stage)
            self.assertEqual(self.session.get_stage_list(), [stage])
            self.assertTrue(self.session.close_stage(stage))
            self.assertFalse(self.session.get_stage_cache().Contains(stage))
            # Closing a session entry must not invalidate another owner's USD stage.
            self.assertTrue(stage.GetPrimAtPath("/SessionOnly"))

    def test_adopts_stage_already_in_shared_cache_by_id(self):
        with Usd.StageCacheContext(self.session.get_stage_cache()):
            stage = Usd.Stage.Open(Sdf.Layer.CreateAnonymous())
        cache = self.session.get_stage_cache()
        self.assertTrue(cache.Contains(stage))
        self.assertEqual(self.session.get_stage_list(), [])
        stage_id = cache.GetId(stage)
        self.session.set_current_stage(stage_id)
        self.assertEqual(self.session.get_current_stage(), stage)
        self.session.set_current_stage(stage)
        self.assertEqual(self.session.get_stage_list(), [stage])

    def test_closing_current_stage_selects_remaining_stage(self):
        first = Usd.Stage.CreateInMemory()
        second = Usd.Stage.CreateInMemory()
        self.session.set_current_stage(first)
        self.session.set_current_stage(second)
        second_id = self.session.get_stage_id(second)
        self.assertTrue(self.session.close_stage(second_id))
        self.assertEqual(self.session.get_current_stage(), first)
        self.assertEqual(self.session.get_stage_list(), [first])
        self.assertIsNone(self.session.get_stage_cache().Find(second_id))
        self.assertFalse(self.session.close_stage(second_id))
        self.assertTrue(self.session.close_stage(first))
        self.assertIsNone(self.session.get_current_stage())
        self.assertFalse(self.session.get_current_stage_id().IsValid())

    def test_closing_inactive_stage_preserves_current_stage(self):
        first, second, current = [Usd.Stage.CreateInMemory() for _ in range(3)]
        for stage in (first, second, current):
            self.session.set_current_stage(stage)
        self.app.set_prim_selection([Sdf.Path("/World")])
        selection = self.app.get_selection()
        self.assertTrue(self.session.close_stage(second))
        self.assertEqual(self.session.get_current_stage(), current)
        self.assertEqual(self.app.get_selection(), selection)
        self.assertEqual(set(self.session.get_stage_list()), {first, current})

    def test_stage_switch_clears_selection_and_notifies(self):
        changes = []
        handle = self.app.register_event_callback(
            "current_stage_changed", lambda: changes.append(self.session.get_current_stage())
        )
        self.addCleanup(self.app.unregister_event_callback, "current_stage_changed", handle)
        first = Usd.Stage.CreateInMemory()
        second = Usd.Stage.CreateInMemory()
        self.session.set_current_stage(first)
        self.app.set_prim_selection([Sdf.Path("/World")])
        self.session.set_current_stage(second)
        self.assertTrue(self.app.get_selection().empty())
        self.assertEqual(changes, [first, second])

    def test_close_all_removes_cached_stages(self):
        stages = [Usd.Stage.CreateInMemory(), Usd.Stage.CreateInMemory()]
        for stage in stages:
            self.session.set_current_stage(stage)
        self.session.close_all()
        self.assertEqual(self.session.get_stage_list(), [])
        self.assertIsNone(self.session.get_current_stage())
        self.assertFalse(self.session.get_current_stage_id().IsValid())
        for stage in stages:
            self.assertFalse(self.session.get_stage_cache().Contains(stage))
