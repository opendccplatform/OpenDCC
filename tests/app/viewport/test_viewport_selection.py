# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

import OpenImageIO as oiio
from Qt import QtCore, QtWidgets
from pxr import Gf, Sdf, Usd, UsdGeom
import opendcc.core as core
from image_utils import ImageDiffingTestCase, compare_images
from viewport_utils import project_to_viewport


class TestViewportSelection(ImageDiffingTestCase):
    @classmethod
    def setUpClass(cls):
        cls.viewport_widget = core.ViewportWidget(core.Application.instance().get_main_window())
        if not isinstance(cls.viewport_widget, QtWidgets.QWidget):
            raise TypeError("ViewportWidget must be a Qt widget")
        cls.addClassCleanup(cls.viewport_widget.deleteLater)
        cls.addClassCleanup(cls.viewport_widget.close)
        cls.viewport_widget.setWindowFlags(QtCore.Qt.Window)
        cls.viewport_widget.show()
        cls.view = cls.viewport_widget.get_viewport_view()
        QtCore.QCoreApplication.processEvents()

    def setUp(self):
        app = core.Application.instance()
        self.app = app
        previous_mode = app.get_selection_mode()
        self.addCleanup(app.set_selection_mode, previous_mode)
        app.set_selection_mode(core.Application.SelectionMode.PRIMS)
        self.assertTrue(app.is_ui_available())
        self.assertGreater(QtCore.QThread.currentThread().loopLevel(), 0)
        window = app.get_main_window()
        self.assertTrue(window.isVisible())
        self.assertIsNotNone(self.view)
        self.assertIsInstance(self.view, core.ViewportView)
        self.assertEqual(self.view.get_scene_context_type(), "USD")
        self.view.get_widget().setFixedSize(400, 400)

        session = app.get_session()
        stage = Usd.Stage.CreateInMemory()
        self.stage = stage
        self.addCleanup(session.close_stage, stage)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        cube_fixture = Path(__file__).parent / "fixtures" / "cube.usda"
        for name, x in (("Left", -2), ("Right", 2)):
            cube = UsdGeom.Mesh.Define(stage, "/" + name)
            self.assertTrue(cube.GetPrim().GetReferences().AddReference(str(cube_fixture)))
            cube.AddTranslateOp().Set(Gf.Vec3d(x, 0, 0))
        camera = UsdGeom.Camera.Define(stage, "/Camera")
        camera.CreateProjectionAttr(UsdGeom.Tokens.orthographic)
        camera.CreateHorizontalApertureAttr(100)
        camera.CreateVerticalApertureAttr(100)
        camera.AddTranslateOp().Set(Gf.Vec3d(0, 0, 10))
        session.set_current_stage(stage)
        self.view.look_through(camera.GetPath())
        app.set_current_viewport_tool(None)
        app.get_settings().set_bool("viewport.grid.enable", False)
        app.get_settings().set_bool("viewport.show_camera", False)
        app.get_settings().set_double_array("viewport.background.color", [0.36, 0.36, 0.36])
        app.clear_prim_selection()
        self.addCleanup(app.clear_prim_selection)

    def set_draw_mode(self, name):
        action = self.viewport_widget.findChild(QtCore.QObject, name)
        self.assertIsNotNone(action, "Missing viewport action: " + name)
        action.setChecked(True)

    def capture_cubes(self, path):
        def cubes_visible(image):
            width, height = image.spec().width, image.spec().height
            background = image.getpixel(0, 0)[:3]
            return all(
                image.getpixel(x, height // 2)[:3] != background
                for x in (width // 4, 3 * width // 4)
            )

        # A quiet background can precede Hydra's first populated frame.
        return self.capture_viewport(self.view, path, cubes_visible)

    def test_selection_highlight_and_clear(self):
        app = self.app
        baseline = self.capture_cubes("unselected.png")
        _, _, width, height = self.view.get_viewport_dimensions()
        self.assertEqual((baseline.spec().width, baseline.spec().height), (width, height))
        self.assertGreater(width, 0)
        self.assertGreater(height, 0)
        for path, x in (("/Left", -2), ("/Right", 2)):
            picked = self.view.pick_single_prim(
                project_to_viewport(self.view, (x, 0, 0)),
                int(core.MergeFlags.FULL_SELECTION),
            )
            self.assertEqual(list(picked.get_fully_selected_paths()), [Sdf.Path(path)])

        left = oiio.ROI(0, width // 2, 0, height, 0, 1, 0, 3)
        right = oiio.ROI(width // 2, width, 0, height, 0, 1, 0, 3)
        app.set_prim_selection([Sdf.Path("/Left")])
        selected = self.capture_viewport(
            self.view,
            "selected.png",
            lambda image: compare_images(baseline, image, left).nfail > 100,
        )
        self.assertGreater(compare_images(baseline, selected, left).nfail, 100)
        self.assertEqual(compare_images(baseline, selected, right).nfail, 0)

        reference_path = Path(__file__).parent / "baseline" / "selected.png"
        self.assert_images_close(reference_path, "selected.png")

        app.clear_prim_selection()
        cleared = self.capture_viewport(
            self.view, "cleared.png", lambda image: compare_images(baseline, image).nfail == 0
        )
        self.assertEqual(compare_images(baseline, cleared).nfail, 0)

    def test_marquee_selection(self):
        baseline = self.capture_cubes("marquee_unselected.png")
        _, _, width, height = self.view.get_viewport_dimensions()
        mask = int(core.MergeFlags.FULL_SELECTION)
        for start, end, expected in (
            ((0, 0), (width // 2, height), {Sdf.Path("/Left")}),
            ((width // 2, height), (0, 0), {Sdf.Path("/Left")}),
            ((0, 0), (width, height), {Sdf.Path("/Left"), Sdf.Path("/Right")}),
            ((0, 0), (20, 20), set()),
        ):
            with self.subTest(expected=expected):
                selected = self.view.pick_multiple_prims(Gf.Vec2f(*start), Gf.Vec2f(*end), mask)
                self.assertEqual(set(selected.get_fully_selected_paths()), expected)

        selected = self.view.pick_multiple_prims(Gf.Vec2f(0, 0), Gf.Vec2f(width, height), mask)
        self.app.set_selection(selected)
        image = self.capture_viewport(
            self.view,
            "marquee_selected.png",
            lambda image: compare_images(baseline, image).nfail > 100,
        )
        for start, end in ((0, width // 2), (width // 2, width)):
            roi = oiio.ROI(start, end, 0, height, 0, 1, 0, 3)
            self.assertGreater(compare_images(baseline, image, roi).nfail, 100)
        self.app.clear_prim_selection()
        cleared = self.capture_viewport(
            self.view,
            "marquee_cleared.png",
            lambda image: compare_images(baseline, image).nfail == 0,
        )
        self.assertEqual(compare_images(baseline, cleared).nfail, 0)

    def test_component_selection(self):
        # Two adjacent quads give stable point, half-edge and face IDs.
        self.stage.RemovePrim("/Left")
        mesh = UsdGeom.Mesh.Define(self.stage, "/Left")
        fixture = Path(__file__).parent / "fixtures" / "two_quads.usda"
        self.assertTrue(mesh.GetPrim().GetReferences().AddReference(str(fixture)))
        path = mesh.GetPath()
        self.addCleanup(self.set_draw_mode, "viewport_set_shaded _smooth_draw_mode")
        for name, mode, mask, attribute, position, expected in (
            (
                "points",
                core.Application.SelectionMode.POINTS,
                core.MergeFlags.POINTS,
                "point_indices",
                (-3, 1, 1),
                [3],
            ),
            (
                "edges",
                core.Application.SelectionMode.EDGES,
                core.MergeFlags.EDGES,
                "edge_indices",
                (-2, 0, 1),
                [1, 7],
            ),
            (
                "faces",
                core.Application.SelectionMode.FACES,
                core.MergeFlags.ELEMENTS,
                "element_indices",
                (-2.5, 0, 1),
                [0],
            ),
        ):
            with self.subTest(component=name):
                draw_mode = {
                    "points": "points",
                    "edges": "wireframe on _surface",
                    "faces": "shaded _smooth",
                }[name]
                self.set_draw_mode("viewport_set_" + draw_mode + "_draw_mode")
                self.app.set_selection_mode(mode)
                baseline = self.capture_viewport(self.view, name + "_unselected.png")
                single = self.view.pick_single_prim(
                    project_to_viewport(self.view, position), int(mask)
                )
                self.assertEqual(set(single.get_selected_paths()), {path})
                self.assertEqual(list(getattr(single[path], attribute)), expected)
                self.assertFalse(single[path].fully_selected)
                x, y, z = position
                partial = self.view.pick_multiple_prims(
                    project_to_viewport(self.view, (x - 0.15, y + 0.15, z)),
                    project_to_viewport(self.view, (x + 0.15, y - 0.15, z)),
                    int(mask),
                )
                self.assertEqual(partial, single)
                empty = self.view.pick_multiple_prims(Gf.Vec2f(0, 0), Gf.Vec2f(20, 20), int(mask))
                self.assertTrue(empty.empty())
                marquee = self.view.pick_multiple_prims(
                    project_to_viewport(self.view, (-3.5, 1.5, 1)),
                    project_to_viewport(self.view, (-0.5, -1.5, 1)),
                    int(mask),
                )
                count = {"points": 6, "edges": 8, "faces": 2}[name]
                self.assertEqual(set(marquee.get_selected_paths()), {path})
                self.assertEqual(list(getattr(marquee[path], attribute)), list(range(count)))
                self.assertFalse(marquee[path].fully_selected)
                self.app.set_selection(single)
                selected = self.capture_viewport(
                    self.view,
                    name + "_selected.png",
                    lambda image: compare_images(baseline, image).nfail > 0,
                )
                _, _, width, height = self.view.get_viewport_dimensions()
                right = oiio.ROI(width // 2, width, 0, height, 0, 1, 0, 3)
                self.assertEqual(compare_images(baseline, selected, right).nfail, 0)
                reference_path = Path(__file__).parent / "baseline" / (name + "_selected.png")
                # A selected point covers too few pixels for the usual whole-image tolerance.
                tolerance = 1e-6 if name == "points" else 1e-4
                self.assert_images_close(reference_path, name + "_selected.png", tolerance)
                self.app.clear_prim_selection()
                cleared = self.capture_viewport(
                    self.view,
                    name + "_cleared.png",
                    lambda image: compare_images(baseline, image).nfail == 0,
                )
                self.assertEqual(compare_images(baseline, cleared).nfail, 0)

    def test_soft_selection(self):
        from opendcc.actions.tool_context_actions import set_tool_context

        self.stage.RemovePrim("/Left")
        self.stage.RemovePrim("/Right")
        mesh = UsdGeom.Mesh.Define(self.stage, "/Mesh")
        fixture = Path(__file__).parent / "fixtures" / "soft_selection_grid.usda"
        self.assertTrue(mesh.GetPrim().GetReferences().AddReference(str(fixture)))
        settings = self.app.get_settings()
        for name, value, kind in (
            ("soft_selection.falloff_radius", 2.2, "double"),
            ("soft_selection.enable_color", True, "bool"),
            ("soft_selection.falloff_curve", [0, 1, 1, 1, 0, 1], "double_array"),
            ("soft_selection.falloff_color", [0, 1, 0, 0, 1, 1, 1, 1, 0, 1], "double_array"),
        ):
            setter = getattr(settings, "set_" + kind)
            if settings.has(name):
                self.addCleanup(setter, name, getattr(settings, "get_" + kind)(name))
            else:
                self.addCleanup(settings.remove, name)
            setter(name, value)
        previous_enabled = self.app.is_soft_selection_enabled()
        self.addCleanup(self.app.enable_soft_selection, previous_enabled)
        self.app.enable_soft_selection(False)
        self.set_draw_mode("viewport_set_points_draw_mode")
        self.addCleanup(self.set_draw_mode, "viewport_set_shaded _smooth_draw_mode")
        self.app.set_prim_selection([mesh.GetPath()])
        self.app.set_selection_mode(core.Application.SelectionMode.POINTS)
        set_tool_context("select_tool")
        self.addCleanup(self.app.set_current_viewport_tool, None)
        baseline = self.capture_viewport(self.view, "soft_unselected.png")

        selection = self.view.pick_single_prim(
            project_to_viewport(self.view, (-2, 0, 1)), int(core.MergeFlags.POINTS)
        )
        self.assertEqual(list(selection[mesh.GetPath()].point_indices), [8])
        self.app.set_selection(selection)
        hard = self.capture_viewport(
            self.view, "soft_disabled.png", lambda image: compare_images(baseline, image).nfail > 0
        )
        self.app.enable_soft_selection(True)
        self.assertTrue(self.app.is_soft_selection_enabled())
        soft = self.capture_viewport(
            self.view, "soft_selected.png", lambda image: compare_images(hard, image).nfail > 0
        )
        reference_path = Path(__file__).parent / "baseline" / "soft_selected.png"
        self.assert_images_close(reference_path, "soft_selected.png", 1e-6)
        self.assertEqual(self.app.get_selection(), selection)
        _, _, width, height = self.view.get_viewport_dimensions()
        outside = oiio.ROI(3 * width // 4, width, 0, height, 0, 1, 0, 3)
        self.assertEqual(compare_images(hard, soft, outside).nfail, 0)
        settings.set_double("soft_selection.falloff_radius", 0.5)
        narrow = self.capture_viewport(
            self.view, "soft_narrow.png", lambda image: compare_images(soft, image).nfail > 0
        )
        self.assertEqual(compare_images(hard, narrow).nfail, 0)
        self.app.enable_soft_selection(False)
        disabled = self.capture_viewport(self.view, "soft_disabled_again.png")
        self.assertEqual(compare_images(hard, disabled).nfail, 0)
        self.app.enable_soft_selection(True)
        self.app.clear_prim_selection()
        cleared = self.capture_viewport(
            self.view, "soft_cleared.png", lambda image: compare_images(baseline, image).nfail == 0
        )
        self.assertEqual(compare_images(baseline, cleared).nfail, 0)
