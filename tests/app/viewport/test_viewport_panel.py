# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import json
from pathlib import Path
import tempfile

from Qt import QtCore, QtGui, QtWidgets
from pxr import Gf, Sdf, Usd, UsdGeom
import opendcc.core as core
from image_utils import ImageDiffingTestCase, compare_images
from viewport_utils import project_to_viewport
from opendcc.actions.tool_context_actions import set_tool_context


class TestViewportPanel(ImageDiffingTestCase):
    def setUp(self):
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.app = core.Application.instance()
        self.app.set_current_viewport_tool(None)
        self.app.clear_prim_selection()
        self.stage = Usd.Stage.CreateInMemory()
        cube = UsdGeom.Mesh.Define(self.stage, "/Cube")
        fixture = Path(__file__).parent / "fixtures" / "cube.usda"
        cube.GetPrim().GetReferences().AddReference(str(fixture))
        self.app.get_session().set_current_stage(self.stage)
        self.addCleanup(self.app.get_session().close_stage, self.stage)
        self.app.get_settings().set_bool("viewport.grid.enable", False)
        self.app.get_settings().set_bool("viewport.show_camera", False)
        self.app.get_settings().set_double_array("viewport.background.color", [0.36] * 3)
        self.panel = self.create_panel()

    def create_panel(self):
        panel = core.ViewportPanel()

        def cleanup():
            try:
                panel.parentWidget()
            except RuntimeError:
                return
            panel.close()
            panel.deleteLater()

        self.addCleanup(cleanup)
        panel.resize(1000, 800)
        panel.show()
        QtCore.QCoreApplication.processEvents()
        return panel

    def quad(self):
        self.panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
        QtCore.QCoreApplication.processEvents()
        return [self.panel.get_pane(i).get_viewport_view() for i in range(4)]

    def capture_cube(self, view, name):
        def visible(image):
            width, height = image.spec().width, image.spec().height
            return image.getpixel(width // 2, height // 2)[:3] != image.getpixel(0, 0)[:3]

        return self.capture_viewport(view, name + ".png", visible)

    def test_single_default_and_independent_orthographic_views(self):
        self.assertEqual(self.panel.get_layout_mode(), core.ViewportPanel.LayoutMode.SINGLE)
        self.assertIsNone(self.panel.get_pane(0))
        self.assertEqual(
            self.panel.get_pane(1).get_viewport_view().get_view_preset(),
            core.ViewportViewPreset.PERSPECTIVE,
        )
        views = self.quad()
        for index, (preset, label) in enumerate(
            (("TOP", "top"), ("PERSPECTIVE", "persp"), ("FRONT", "front"), ("SIDE", "side"))
        ):
            with self.subTest(preset=preset):
                view = views[index]
                self.assertEqual(view.get_view_preset(), getattr(core.ViewportViewPreset, preset))
                self.assertIn(
                    f"{label} (Def Cam)",
                    [
                        combo.currentText()
                        for combo in self.panel.get_pane(index).findChildren(QtWidgets.QComboBox)
                    ],
                )
                self.assertEqual(
                    view.get_camera().projection,
                    Gf.Camera.Perspective if index == 1 else Gf.Camera.Orthographic,
                )
                self.capture_cube(view, preset.lower())
                picked = view.pick_single_prim(
                    project_to_viewport(view, (0, 0, 0)), int(core.MergeFlags.FULL_SELECTION)
                )
                self.assertEqual(list(picked.get_fully_selected_paths()), [Sdf.Path("/Cube")])
        before = [view.get_camera() for view in views]
        camera = views[0].get_navigation_camera()
        camera.horizontalAperture *= 2
        camera.verticalAperture *= 2
        views[0].set_navigation_camera(camera)
        self.assertEqual(views[0].get_camera().horizontalAperture, camera.horizontalAperture)
        self.assertEqual([view.get_camera() for view in views[1:]], before[1:])

    def test_camera_menu_and_overlay_switch_only_their_pane(self):
        views = self.quad()
        pane = self.panel.get_pane(2)
        local = views[2].get_camera()
        other_cameras = [views[index].get_camera() for index in (0, 1, 3)]

        camera = UsdGeom.Camera.Define(self.stage, "/ShotCamera")
        camera.AddTranslateOp().Set(Gf.Vec3d(0, 0, 30))
        QtCore.QCoreApplication.processEvents()

        view_action = next(a for a in pane.get_menubar().actions() if a.text() == "View")
        view_menu = view_action.menu()
        camera_action = next(a for a in view_menu.actions() if a.text() == "Camera")
        camera_menu = camera_action.menu()
        sections = [a.text() for a in camera_menu.actions() if a.isSeparator()]
        self.assertEqual(sections, ["Def Cams", "USD Cameras"])
        actions = {a.text(): a for a in camera_menu.actions() if a.isCheckable()}
        self.assertEqual(set(actions), {"persp", "top", "front", "side", "ShotCamera"})
        self.assertTrue(actions["front"].isChecked())
        self.assertEqual(actions["side"].toolTip(), "In-memory navigation view")
        self.assertEqual(actions["ShotCamera"].toolTip(), "USD scene camera")
        overlay = next(
            combo
            for combo in pane.findChildren(QtWidgets.QComboBox)
            if combo.findText("front (Def Cam)") >= 0
        )

        actions["ShotCamera"].trigger()
        self.assertEqual(views[2].get_camera().transform, camera.GetCamera().transform)
        self.assertEqual(overlay.currentText(), "ShotCamera")
        actions["front"].trigger()
        self.assertEqual(views[2].get_camera().transform, local.transform)
        self.assertEqual(overlay.currentText(), "front (Def Cam)")

        overlay.setCurrentIndex(overlay.findText("side (Def Cam)"))
        self.assertEqual(views[2].get_view_preset(), core.ViewportViewPreset.SIDE)
        self.assertTrue(actions["side"].isChecked())
        actions["persp"].trigger()
        self.assertEqual(views[2].get_camera().projection, Gf.Camera.Perspective)
        self.assertEqual(overlay.currentText(), "persp (Def Cam)")
        self.assertEqual([views[index].get_camera() for index in (0, 1, 3)], other_cameras)
        self.assertEqual(
            [prim.GetPath() for prim in self.stage.Traverse() if prim.IsA(UsdGeom.Camera)],
            [camera.GetPath()],
        )

    def test_first_split_from_orthographic_uses_perspective_lens(self):
        view = self.panel.get_pane(1).get_viewport_view()
        view.set_view_preset(core.ViewportViewPreset.TOP)
        before = view.get_navigation_camera()
        self.assertEqual(before.projection, Gf.Camera.Orthographic)
        expected_fov = core.DefCamSettings.instance().fov
        self.quad()
        camera = view.get_camera()
        self.assertEqual(camera.projection, Gf.Camera.Perspective)
        self.assertEqual(camera.transform, before.transform)
        self.assertAlmostEqual(
            camera.GetFieldOfView(Gf.Camera.FOVHorizontal), expected_fov, places=4
        )

    def test_renderer_restarts_from_each_pane_menu(self):
        previous_context = QtGui.QOpenGLContext()
        self.assertTrue(previous_context.create())
        previous_surface = QtGui.QOffscreenSurface()
        previous_surface.setFormat(previous_context.format())
        previous_surface.create()
        self.assertTrue(previous_surface.isValid())

        for index, view in enumerate(self.quad()):
            with self.subTest(pane=index):
                before = "restart_before_{}".format(index)
                self.capture_cube(view, before)
                pane = self.panel.get_pane(index)
                menubar = pane.get_menubar()
                menu_actions = menubar.actions()
                menu_action = next(action for action in menu_actions if action.text() == "View")
                menu = menu_action.menu()
                restart = next(
                    action for action in menu.actions() if action.text() == "Restart Render"
                )
                context = QtGui.QOpenGLContext.currentContext()
                if context:
                    context.doneCurrent()
                self.assertIsNone(QtGui.QOpenGLContext.currentContext())
                restart.trigger()
                self.assertIsNone(QtGui.QOpenGLContext.currentContext())

                self.assertTrue(previous_context.makeCurrent(previous_surface))
                try:
                    restart.trigger()
                    self.assertIs(QtGui.QOpenGLContext.currentContext(), previous_context)
                    self.assertEqual(previous_context.surface(), previous_surface)
                finally:
                    previous_context.doneCurrent()

                after = "restart_after_{}".format(index)
                self.capture_cube(view, after)
                self.assert_images_close(before + ".png", after + ".png")

    def test_non_usd_context_collapses_and_disables_split_layouts(self):
        views = self.quad()
        for index, view in enumerate(views):
            self.capture_cube(view, "context_before_" + str(index))
        pane = self.panel.get_pane(2)
        menubar = pane.get_menubar()
        menu_actions = menubar.actions()
        context_action = next(action for action in menu_actions if action.text() == "Context")
        context = context_action.menu()
        action = next((action for action in context.actions() if action.text() == "HydraOp"), None)
        if action is None:
            self.skipTest("HydraOp context is not installed")
        action.trigger()
        QtCore.QCoreApplication.processEvents()
        self.assertEqual(self.panel.get_layout_mode(), core.ViewportPanel.LayoutMode.SINGLE)
        self.assertEqual(self.panel.get_active_pane(), 2)
        self.assertEqual(views[2].get_scene_context_type(), "HydraOp")
        layout = next(
            menu for menu in pane.findChildren(QtWidgets.QMenu) if menu.title() == "Layout"
        )
        for action in layout.actions()[1:4]:
            self.assertFalse(action.isEnabled())
        next(action for action in context.actions() if action.text() == "USD").trigger()
        self.panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
        QtCore.QCoreApplication.processEvents()
        self.capture_cube(views[2], "context_returned")

    def test_lighting_mode_changes_only_in_the_target_pane(self):
        views = self.quad()
        baseline = [
            self.capture_cube(view, "default_light_{}".format(i)) for i, view in enumerate(views)
        ]
        actions = self.panel.get_pane(2).get_toolbar().actions()
        next(action for action in actions if action.text() == "Use All Lights").trigger()
        self.capture_viewport(
            views[2], "scene_light.png", lambda image: compare_images(baseline[2], image).nfail > 0
        )
        for index in (0, 1, 3):
            self.capture_cube(views[index], "neighbor_light_{}".format(index))
            self.assert_images_close(
                "default_light_{}.png".format(index), "neighbor_light_{}.png".format(index)
            )
        next(action for action in actions if action.text() == "Use Default Lighting").trigger()
        self.capture_cube(views[2], "restored_camera_light")
        self.assert_images_close("default_light_2.png", "restored_camera_light.png")

    def test_splitters_and_resize_keep_columns_and_camera_aspect(self):
        views = self.quad()
        rows = [
            splitter
            for splitter in self.panel.findChildren(QtWidgets.QSplitter)
            if splitter.orientation() == QtCore.Qt.Horizontal
        ]
        self.assertEqual(len(rows), 2)
        rows[0].moveSplitter(300, 1)
        QtCore.QCoreApplication.processEvents()
        self.assertEqual(rows[0].sizes(), rows[1].sizes())
        sizes = rows[0].sizes()
        self.panel.maximize_pane(1)
        self.panel.restore_panes()
        QtCore.QCoreApplication.processEvents()
        self.assertEqual(rows[0].sizes(), sizes)
        self.panel.resize(1200, 900)
        QtCore.QCoreApplication.processEvents()
        for view in views:
            _, _, width, height = view.get_viewport_dimensions()
            camera = view.get_camera()
            self.assertAlmostEqual(
                camera.horizontalAperture / camera.verticalAperture, width / height, places=5
            )
            self.capture_cube(view, "resized_" + str(views.index(view)))

    def test_undoable_stage_edits_are_visible_from_every_active_pane(self):
        views = self.quad()
        stack = self.app.get_undo_stack()
        self.addCleanup(stack.clear)
        cube = UsdGeom.Xformable(self.stage.GetPrimAtPath("/Cube"))
        for index, view in enumerate(views):
            self.panel.set_active_pane(index)
            with core.UsdEditsUndoBlock():
                cube.AddTranslateOp().Set(Gf.Vec3d(2, 0, 0))
            self.capture_viewport(view, "edited_{}.png".format(index))
            for other in views:
                picked = other.pick_single_prim(
                    project_to_viewport(other, (2, 0, 0)), int(core.MergeFlags.FULL_SELECTION)
                )
                self.assertEqual(list(picked.get_fully_selected_paths()), [Sdf.Path("/Cube")])
            stack.undo()
            self.assertFalse(cube.GetOrderedXformOps())
            stack.redo()
            self.assertEqual(cube.GetOrderedXformOps()[0].Get(), Gf.Vec3d(2, 0, 0))
            stack.undo()

    def test_scene_camera_follow_restores_local_navigation_without_authoring(self):
        views = self.quad()
        scene_camera = UsdGeom.Camera.Define(self.stage, "/Camera")
        scene_camera.AddTranslateOp().Set(Gf.Vec3d(0, 0, 30))
        local = views[2].get_navigation_camera()
        views[2].look_through(scene_camera.GetPath())
        state = self.panel.save_state()
        other = self.create_panel()
        self.assertTrue(other.restore_state(state))
        restored = other.get_pane(2).get_viewport_view()
        self.assertEqual(restored.get_camera().transform, scene_camera.GetCamera().transform)
        restored.look_through(Sdf.Path())
        self.assertEqual(restored.get_camera().transform, local.transform)
        self.assertEqual(
            scene_camera.GetCamera().transform, Gf.Matrix4d().SetTranslate(Gf.Vec3d(0, 0, 30))
        )

    def test_global_camera_preferences_do_not_change_quad_projections(self):
        views = self.quad()
        before = [view.get_camera() for view in views]
        defaults = core.DefCamSettings.instance()
        previous = defaults.is_perspective
        try:
            defaults.is_perspective = False
            defaults.is_perspective = True
            self.assertEqual([view.get_camera() for view in views], before)
        finally:
            defaults.is_perspective = previous

    def test_default_camera_preferences_edit_named_views_across_panels(self):
        from opendcc.preferences.def_cam_page import DefCamPage

        views = self.quad()
        other = self.create_panel()
        other.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
        QtCore.QCoreApplication.processEvents()
        other_top = other.get_pane(0).get_viewport_view()
        other_persp = other.get_pane(1).get_viewport_view()
        top = other_top.get_navigation_camera()
        top.horizontalAperture *= 1.5
        other_top.set_navigation_camera(top)
        previous_layout = self.panel.save_state()
        before = [view.get_navigation_camera() for view in views]
        other_top_pose = other_top.get_camera().transform

        page = DefCamPage()
        self.addCleanup(page.deleteLater)
        self.addCleanup(page.restore_previous_settings)
        settings = self.app.get_settings()
        self.addCleanup(settings.remove, "def_cam.named.persp")
        self.assertEqual(
            [page._camera_picker.itemText(index) for index in range(page._camera_picker.count())],
            ["persp", "top", "front", "side"],
        )

        camera = UsdGeom.Camera.Define(self.stage, "/ShotCamera")
        other_persp.look_through(camera.GetPath())
        stage_camera = camera.GetCamera()
        page._fields["fov"].set_value(55)
        self.assertFalse(settings.has("def_cam.named.persp"))
        self.assertAlmostEqual(
            views[1].get_camera().GetFieldOfView(Gf.Camera.FOVHorizontal), 55, places=4
        )
        self.assertEqual(camera.GetCamera(), stage_camera)
        other_persp.look_through(Sdf.Path())
        self.assertAlmostEqual(
            other_persp.get_camera().GetFieldOfView(Gf.Camera.FOVHorizontal), 55, places=4
        )
        page.save_settings()
        self.assertTrue(settings.has("def_cam.named.persp"))
        self.assertFalse(settings.has("session.def_cam.named.persp"))

        self.panel.set_active_pane(3)
        page._camera_picker.setCurrentIndex(1)
        page._fields["size"].set_value(32)
        page._fields["near"].set_value(-4)
        top = views[0].get_camera()
        self.assertAlmostEqual(top.horizontalAperture * Gf.Camera.APERTURE_UNIT, 32, places=4)
        self.assertAlmostEqual(
            other_top.get_camera().horizontalAperture * Gf.Camera.APERTURE_UNIT, 48, places=4
        )
        self.assertEqual(other_top.get_camera().transform, other_top_pose)
        self.assertEqual(top.clippingRange.GetMin(), -4)
        self.assertEqual(top.projection, Gf.Camera.Orthographic)
        self.assertEqual([views[i].get_navigation_camera() for i in (2, 3)], before[2:])

        self.assertTrue(self.panel.restore_state(previous_layout))
        self.assertAlmostEqual(
            views[0].get_camera().horizontalAperture * Gf.Camera.APERTURE_UNIT, 32, places=4
        )
        self.assertAlmostEqual(
            views[1].get_camera().GetFieldOfView(Gf.Camera.FOVHorizontal), 55, places=4
        )

        page.restore_previous_settings()
        self.assertAlmostEqual(
            views[1].get_camera().GetFieldOfView(Gf.Camera.FOVHorizontal), 55, places=4
        )
        self.assertEqual(views[0].get_navigation_camera().clippingRange, before[0].clippingRange)
        self.assertAlmostEqual(
            views[0].get_navigation_camera().horizontalAperture,
            before[0].horizontalAperture,
            places=4,
        )

    def test_grid_planes_render_in_all_orthographic_views(self):
        views = self.quad()
        baseline = [self.capture_cube(view, "grid_off_" + str(i)) for i, view in enumerate(views)]
        self.app.get_settings().set_bool("viewport.grid.enable", True)
        for index in (0, 2, 3):
            self.capture_viewport(
                views[index],
                "grid_on_{}.png".format(index),
                lambda image, index=index: compare_images(baseline[index], image).nfail > 100,
            )

    def test_activation_maximize_and_restore_preserve_views(self):
        views = self.quad()
        for index, view in enumerate(views):
            self.capture_cube(view, "activate_" + str(index))
            QtWidgets.QApplication.sendEvent(
                view.get_widget(), QtGui.QFocusEvent(QtCore.QEvent.FocusIn)
            )
            self.assertEqual(self.panel.get_active_pane(), index)
            self.assertIs(self.app.get_active_view(), view)
        cameras = [view.get_camera() for view in views]
        dimensions = [view.get_viewport_dimensions() for view in views]
        for index in range(4):
            self.panel.maximize_pane(index)
            QtCore.QCoreApplication.processEvents()
            self.assertTrue(self.panel.is_maximized())
            self.assertIs(self.app.get_active_view(), views[index])
            self.assertEqual(
                [view.get_widget().isVisible() for view in views], [i == index for i in range(4)]
            )
            self.panel.restore_panes()
            QtCore.QCoreApplication.processEvents()
            self.assertEqual(
                [view.get_camera().transform for view in views],
                [camera.transform for camera in cameras],
            )
            self.assertEqual([view.get_viewport_dimensions() for view in views], dimensions)
        self.panel.set_layout_mode(core.ViewportPanel.LayoutMode.SINGLE)
        self.panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
        for index, view in enumerate(views):
            self.assertIs(self.panel.get_pane(index).get_viewport_view(), view)
            self.assertEqual(view.get_camera().transform, cameras[index].transform)

    def test_selection_highlights_all_panes_and_clears(self):
        views = self.quad()
        baselines = [self.capture_cube(view, "baseline_" + str(i)) for i, view in enumerate(views)]
        self.app.set_prim_selection([Sdf.Path("/Cube")])
        for index, view in enumerate(views):
            self.capture_viewport(
                view,
                "selected_{}.png".format(index),
                lambda image, index=index: compare_images(baselines[index], image).nfail > 20,
            )
        self.app.clear_prim_selection()
        for index, view in enumerate(views):
            self.capture_viewport(
                view,
                "cleared_{}.png".format(index),
                lambda image, index=index: compare_images(baselines[index], image).nfail == 0,
            )

    def test_z_up_axis_and_orthographic_tumble_lock(self):
        UsdGeom.SetStageUpAxis(self.stage, UsdGeom.Tokens.z)
        views = self.quad()
        direction = views[0].get_camera().frustum.ComputeViewDirection()
        self.assertTrue(Gf.IsClose(direction, Gf.Vec3d(0, 0, -1), 1e-6))
        for index in (0, 2, 3):
            widget = views[index].get_widget()
            before = views[index].get_camera().transform
            for event_type, position, button, buttons in (
                (
                    QtCore.QEvent.MouseButtonPress,
                    (100, 100),
                    QtCore.Qt.LeftButton,
                    QtCore.Qt.LeftButton,
                ),
                (QtCore.QEvent.MouseMove, (125, 120), QtCore.Qt.NoButton, QtCore.Qt.LeftButton),
                (
                    QtCore.QEvent.MouseButtonRelease,
                    (125, 120),
                    QtCore.Qt.LeftButton,
                    QtCore.Qt.NoButton,
                ),
            ):
                event = QtGui.QMouseEvent(
                    event_type, QtCore.QPointF(*position), button, buttons, QtCore.Qt.AltModifier
                )
                QtWidgets.QApplication.sendEvent(widget, event)
            self.assertEqual(views[index].get_camera().transform, before)

    def test_state_roundtrip_and_invalid_or_missing_camera(self):
        views = self.quad()
        self.panel.maximize_pane(2)
        camera = views[2].get_navigation_camera()
        camera.transform = Gf.Matrix4d(camera.transform).SetTranslateOnly(Gf.Vec3d(1, 2, 20))
        views[2].set_navigation_camera(camera)
        state = self.panel.save_state()
        restored = self.create_panel()
        self.assertTrue(restored.restore_state(state))
        QtCore.QCoreApplication.processEvents()
        self.assertTrue(restored.is_maximized())
        self.assertEqual(restored.get_active_pane(), 2)
        restored_camera = restored.get_pane(2).get_viewport_view().get_camera()
        self.assertEqual(restored_camera.transform, camera.transform)
        self.assertEqual(restored_camera.projection, camera.projection)
        self.assertEqual(restored_camera.horizontalAperture, camera.horizontalAperture)
        for bad in (b"", b"{}", b'{"version":99}', b"not json"):
            self.assertFalse(restored.restore_state(QtCore.QByteArray(bad)))
        data = json.loads(bytes(state))
        data["panes"][2]["follow_path"] = "/MissingCamera"
        self.assertTrue(restored.restore_state(QtCore.QByteArray(json.dumps(data).encode())))
        self.assertEqual(
            restored.get_pane(2).get_viewport_view().get_view_preset(),
            core.ViewportViewPreset.FRONT,
        )

    def test_orthographic_clipping_range_roundtrips(self):
        view = self.quad()[2]
        camera = view.get_navigation_camera()
        camera.clippingRange = Gf.Range1f(-100, 1000)
        view.set_navigation_camera(camera)
        restored = self.create_panel()
        self.assertTrue(restored.restore_state(self.panel.save_state()))
        actual = restored.get_pane(2).get_viewport_view().get_navigation_camera()
        self.assertEqual(actual.clippingRange, camera.clippingRange)

    def test_invalid_state_does_not_change_layout_or_cameras(self):
        self.quad()
        self.panel.maximize_pane(2)
        state = bytes(self.panel.save_state())
        mutations = (
            ("horizontal_aperture", 1e100),
            ("horizontal_offset", 1e100),
            ("vertical_offset", "bad"),
            ("focal_length", 1e-100),
            ("transform", [0] * 16),
            ("perspective", "true"),
            ("f_stop", -1),
            ("clipping_planes", [[0, 1, 0, 1e100]]),
            ("clipping_planes", [[0, 0, 0, 1]]),
            ("clipping_planes", "bad"),
        )
        for key, value in mutations:
            with self.subTest(key=key):
                data = json.loads(state)
                data["panes"][-1]["camera"][key] = value
                self.assertFalse(
                    self.panel.restore_state(QtCore.QByteArray(json.dumps(data).encode()))
                )
                self.assertEqual(bytes(self.panel.save_state()), state)
        for key, value in (("active", 1.5), ("mode", 1.5), ("maximized", "yes")):
            with self.subTest(key=key):
                data = json.loads(state)
                data[key] = value
                self.assertFalse(
                    self.panel.restore_state(QtCore.QByteArray(json.dumps(data).encode()))
                )
                self.assertEqual(bytes(self.panel.save_state()), state)

    def test_layout_menu_actions_track_mode(self):
        def layout_actions(index):
            pane = self.panel.get_pane(index)
            menu = next(
                menu for menu in pane.findChildren(QtWidgets.QMenu) if menu.title() == "Layout"
            )
            return [menu.actions()[i] for i in (0, 1, 4)]

        single, quad, maximize = layout_actions(1)
        self.assertTrue(single.isChecked())
        self.assertFalse(quad.isChecked())
        self.assertFalse(maximize.isEnabled())
        quad.trigger()
        QtCore.QCoreApplication.processEvents()
        for index in range(4):
            single, quad, maximize = layout_actions(index)
            self.assertFalse(single.isChecked())
            self.assertTrue(quad.isChecked())
            self.assertTrue(maximize.isEnabled())
        layout_actions(2)[2].trigger()
        self.assertTrue(self.panel.is_maximized())
        self.assertEqual(self.panel.get_active_pane(), 2)
        layout_actions(2)[0].trigger()
        self.assertEqual(self.panel.get_layout_mode(), core.ViewportPanel.LayoutMode.SINGLE)
        self.assertFalse(self.panel.is_maximized())
        self.assertTrue(layout_actions(2)[0].isChecked())

    def test_camera_locator_visibility_is_local(self):
        views = self.quad()
        self.app.get_settings().set_bool("viewport.show_camera", True)
        camera = UsdGeom.Camera.Define(self.stage, "/Camera")
        camera.AddTranslateOp().Set(Gf.Vec3d(4, 0, 0))
        self.app.clear_prim_selection()
        self.capture_viewport(views[1], "locator_before.png")
        views[2].look_through(camera.GetPath())
        self.capture_viewport(views[2], "locator_follow.png")
        self.capture_viewport(views[1], "locator_after.png")
        self.assert_images_close("locator_before.png", "locator_after.png")

    def test_return_to_navigation_after_followed_camera_resize(self):
        views = self.quad()
        camera = UsdGeom.Camera.Define(self.stage, "/Camera")
        camera.AddTranslateOp().Set(Gf.Vec3d(0, 0, 30))
        view = views[2]
        view.look_through(camera.GetPath())
        self.panel.maximize_pane(2)
        QtCore.QCoreApplication.processEvents()
        view.look_through(Sdf.Path())
        _, _, width, height = view.get_viewport_dimensions()
        local = view.get_camera()
        self.assertAlmostEqual(
            local.horizontalAperture / local.verticalAperture, width / height, places=5
        )

    def test_camera_snapshot_roundtrips_clipping_planes(self):
        self.quad()
        view = self.panel.get_pane(2).get_viewport_view()
        camera = view.get_navigation_camera()
        camera.fStop = 2.8
        camera.clippingPlanes = [Gf.Vec4f(0, 1, 0, 0)]
        view.set_navigation_camera(camera)
        other = self.create_panel()
        self.assertTrue(other.restore_state(self.panel.save_state()))
        restored = other.get_pane(2).get_viewport_view().get_navigation_camera()
        self.assertEqual(restored.clippingPlanes, camera.clippingPlanes)
        self.assertEqual(restored.fStop, camera.fStop)
        legacy = json.loads(bytes(self.panel.save_state()))
        for pane in legacy["panes"]:
            del pane["camera"]["f_stop"]
            del pane["camera"]["clipping_planes"]
        self.assertTrue(other.restore_state(QtCore.QByteArray(json.dumps(legacy).encode())))

    def test_isolate_selection_is_independent_in_each_pane(self):
        views = self.quad()
        other = UsdGeom.Cube.Define(self.stage, "/Other")
        position = (4, 3, 2)
        other.AddTranslateOp().Set(Gf.Vec3d(*position))
        self.addCleanup(self.app.get_undo_stack().clear)
        for index in range(4):
            self.panel.set_active_pane(index)
            self.app.set_prim_selection([Sdf.Path("/Cube")])
            action = next(
                action
                for action in self.panel.get_pane(index).get_toolbar().actions()
                if action.text() == "Isolate Selection"
            )
            action.trigger()
            for _ in range(2):
                for other_index in reversed(range(4)):
                    view = views[other_index]
                    self.capture_viewport(view, "isolated_{}.png".format(other_index))
                    picked = view.pick_single_prim(
                        project_to_viewport(view, position), int(core.MergeFlags.FULL_SELECTION)
                    )
                    expected = [] if other_index == index else [Sdf.Path("/Other")]
                    self.assertEqual(list(picked.get_fully_selected_paths()), expected)
            action.trigger()

    def test_component_picking_from_each_pane(self):
        views = self.quad()
        previous = self.app.get_selection_mode()
        self.addCleanup(self.app.set_selection_mode, previous)
        for index, view in enumerate(views):
            face = ((0, 1, 0), (0, 0, 1), (0, 0, 1), (1, 0, 0))[index]
            edge = ((1, 1, 0), (1, 0, 1), (1, 0, 1), (1, 1, 0))[index]
            for mode, mask, attribute, position, draw_mode in (
                (
                    core.Application.SelectionMode.POINTS,
                    core.MergeFlags.POINTS,
                    "point_indices",
                    (1, 1, 1),
                    "viewport_set_points_draw_mode",
                ),
                (
                    core.Application.SelectionMode.EDGES,
                    core.MergeFlags.EDGES,
                    "edge_indices",
                    edge,
                    "viewport_set_wireframe on _surface_draw_mode",
                ),
                (
                    core.Application.SelectionMode.FACES,
                    core.MergeFlags.ELEMENTS,
                    "element_indices",
                    face,
                    "viewport_set_shaded _smooth_draw_mode",
                ),
            ):
                with self.subTest(pane=index, component=attribute):
                    self.app.set_selection_mode(mode)
                    action = self.panel.get_pane(index).findChild(QtCore.QObject, draw_mode)
                    self.assertIsNotNone(action)
                    action.setChecked(True)
                    self.capture_viewport(view, "components_{}.png".format(index))
                    picked = view.pick_single_prim(project_to_viewport(view, position), int(mask))
                    self.assertEqual(list(picked.get_selected_paths()), [Sdf.Path("/Cube")])
                    self.assertTrue(list(getattr(picked[Sdf.Path("/Cube")], attribute)))
                    self.assertFalse(picked[Sdf.Path("/Cube")].fully_selected)

    def test_move_manipulator_and_undo_from_each_pane(self):
        views = self.quad()
        previous = self.app.get_selection_mode()
        self.addCleanup(self.app.set_selection_mode, previous)
        self.addCleanup(self.app.set_current_viewport_tool, None)
        self.addCleanup(self.app.get_undo_stack().clear)
        self.app.set_selection_mode(core.Application.SelectionMode.PRIMS)
        self.app.set_prim_selection([Sdf.Path("/Cube")])
        set_tool_context("move_tool")
        cube = UsdGeom.Xformable(self.stage.GetPrimAtPath("/Cube"))
        for index, view in enumerate(views):
            widget = view.get_widget()
            start = project_to_viewport(view, (0, 0, 0))
            end = start + Gf.Vec2f(25, 0)
            for event_type, point, button, buttons in (
                (QtCore.QEvent.MouseMove, start, QtCore.Qt.NoButton, QtCore.Qt.NoButton),
                (QtCore.QEvent.MouseButtonPress, start, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton),
                (QtCore.QEvent.MouseMove, end, QtCore.Qt.NoButton, QtCore.Qt.LeftButton),
                (QtCore.QEvent.MouseButtonRelease, end, QtCore.Qt.LeftButton, QtCore.Qt.NoButton),
            ):
                event = QtGui.QMouseEvent(
                    event_type, QtCore.QPointF(*point), button, buttons, QtCore.Qt.NoModifier
                )
                QtWidgets.QApplication.sendEvent(widget, event)
                self.capture_viewport(view, "manipulator_{}.png".format(index))
            self.assertEqual(self.panel.get_active_pane(), index)
            transform = cube.ComputeLocalToWorldTransform(Usd.TimeCode.Default())
            self.assertGreater(transform.ExtractTranslation().GetLength(), 0.1)
            self.app.get_undo_stack().undo()
            self.assertTrue(
                Gf.IsClose(
                    cube.ComputeLocalToWorldTransform(Usd.TimeCode.Default()), Gf.Matrix4d(1), 1e-6
                )
            )

    def test_floating_quad_preserves_views_and_reloads_docked_layout(self):
        dock = core.create_panel("viewport", False)
        panel = dock.widget()
        panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
        views = [panel.get_pane(i).get_viewport_view() for i in range(4)]
        for index, view in enumerate(views):
            self.capture_viewport(view, "docked_{}.png".format(index))
        transforms = [view.get_camera().transform for view in views]
        window = self.app.get_main_window()
        name = dock.objectName()
        widget = window.findChild(QtWidgets.QWidget, name)

        def close_dock():
            widget = window.findChild(QtWidgets.QWidget, name)
            if widget is not None:
                QtCore.QMetaObject.invokeMethod(widget, "closeDockWidget")

        self.addCleanup(close_dock)
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "docked.ini")
            core.save_layout(path)
            self.assertTrue(QtCore.QMetaObject.invokeMethod(widget, "setFloating"))
            QtCore.QCoreApplication.processEvents()
            self.assertTrue(panel.window().isWindow())
            self.assertIsNot(panel.window(), window)
            for index, view in enumerate(views):
                self.capture_viewport(view, "floating_{}.png".format(index))
                self.assertEqual(view.get_camera().transform, transforms[index])
                self.assertIs(panel.get_pane(index).get_viewport_view(), view)
                picked = view.pick_single_prim(
                    project_to_viewport(view, (0, 0, 0)), int(core.MergeFlags.FULL_SELECTION)
                )
                self.assertEqual(list(picked.get_fully_selected_paths()), [Sdf.Path("/Cube")])
            core.load_layout(path)
        docked = window.findChild(QtWidgets.QWidget, name).findChild(core.ViewportPanel)
        self.assertIs(docked.window(), window)
        for index in range(4):
            view = docked.get_pane(index).get_viewport_view()
            self.assertEqual(view.get_camera().transform, transforms[index])
            self.capture_viewport(view, "redocked_{}.png".format(index))
        self.assertTrue(
            QtCore.QMetaObject.invokeMethod(
                window.findChild(QtWidgets.QWidget, name), "closeDockWidget"
            )
        )
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.panel.set_active_pane(1)
        self.assertIs(self.app.get_active_view(), self.panel.get_pane(1).get_viewport_view())

    def test_left_toolbar_targets_the_active_panel(self):
        toolbar = self.app.get_main_window().findChild(QtWidgets.QToolBar, "tools_toolbar")
        button = toolbar.findChild(QtWidgets.QToolButton, "viewport_layout_button")
        self.assertIsNotNone(button)
        other = self.create_panel()
        self.panel.get_pane(1).get_viewport_view().get_widget().setFocus()
        QtCore.QCoreApplication.processEvents()
        self.panel.set_active_pane(1)
        self.assertTrue(button.isEnabled())
        self.assertIs(
            button.menu(), self.panel.get_pane(1).findChild(QtWidgets.QMenu, "viewport_layout_menu")
        )
        button.menu().actions()[1].trigger()
        self.assertEqual(self.panel.get_layout_mode(), core.ViewportPanel.LayoutMode.FOUR_PANES)
        self.assertEqual(other.get_layout_mode(), core.ViewportPanel.LayoutMode.SINGLE)
        other.set_active_pane(1)
        self.assertTrue(button.menu().actions()[0].isChecked())
        button.menu().actions()[1].trigger()
        self.assertEqual(other.get_layout_mode(), core.ViewportPanel.LayoutMode.FOUR_PANES)
        button.menu().actions()[4].trigger()
        self.assertTrue(other.is_maximized())
        self.assertFalse(self.panel.is_maximized())
        button.menu().actions()[4].trigger()
        self.assertFalse(other.is_maximized())
        standalone = core.ViewportWidget()
        try:
            standalone.show()
            QtCore.QCoreApplication.processEvents()
            QtWidgets.QApplication.sendEvent(
                standalone.get_viewport_view().get_widget(),
                QtGui.QFocusEvent(QtCore.QEvent.FocusIn),
            )
            self.assertFalse(button.isEnabled())
        finally:
            standalone.close()
            standalone.deleteLater()

    def test_hiding_active_panel_notifies_and_routes_to_visible_view(self):
        other = self.create_panel()
        active = other.get_pane(1).get_viewport_view()
        other.set_active_pane(1)
        self.assertIs(self.app.get_active_view(), active)
        notified = []

        def on_active_view_changed():
            notified.append(self.app.get_active_view())

        handle = self.app.register_event_callback("active_view_changed", on_active_view_changed)
        self.addCleanup(self.app.unregister_event_callback, "active_view_changed", handle)
        other.hide()
        self.assertFalse(active.get_widget().isVisible())
        visible = self.app.get_active_view()
        self.assertIsNotNone(visible)
        self.assertTrue(visible.get_widget().isVisible())
        self.assertIs(notified[-1], visible)

    def test_space_toggles_only_the_focused_viewport(self):
        self.panel.activateWindow()
        widget = self.panel.get_pane(1).get_viewport_view().get_widget()
        widget.setFocus()
        QtCore.QCoreApplication.processEvents()

        def press_space(widget):
            for event_type in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
                QtWidgets.QApplication.sendEvent(
                    widget,
                    QtGui.QKeyEvent(event_type, QtCore.Qt.Key_Space, QtCore.Qt.NoModifier, " "),
                )
            QtCore.QCoreApplication.processEvents()

        press_space(widget)
        self.assertEqual(self.panel.get_layout_mode(), core.ViewportPanel.LayoutMode.FOUR_PANES)
        widget = self.panel.get_pane(2).get_viewport_view().get_widget()
        widget.setFocus()
        QtCore.QCoreApplication.processEvents()
        press_space(widget)
        self.assertTrue(self.panel.is_maximized())
        self.assertEqual(self.panel.get_active_pane(), 2)
        press_space(widget)
        self.assertFalse(self.panel.is_maximized())
        entry = QtWidgets.QLineEdit(self.panel.get_pane(2))
        entry.show()
        entry.setFocus()
        QtCore.QCoreApplication.processEvents()
        press_space(entry)
        self.assertFalse(self.panel.is_maximized())
        self.assertEqual(entry.text(), " ")

    def test_two_pane_layouts_preserve_cameras_and_split_ratios(self):
        modes = core.ViewportPanel.LayoutMode
        self.panel.set_layout_mode(modes.TWO_STACKED)
        QtCore.QCoreApplication.processEvents()
        self.assertIsNone(self.panel.get_pane(2))
        self.assertIsNone(self.panel.get_pane(3))
        views = [self.panel.get_pane(i).get_viewport_view() for i in range(2)]
        for i, view in enumerate(views):
            self.capture_cube(view, "two_stacked_" + str(i))
        splitters = self.panel.findChildren(QtWidgets.QSplitter)
        row = next(
            splitter
            for splitter in splitters
            if splitter.widget(0) == self.panel.get_pane(0).parentWidget()
        )
        self.assertEqual(row.orientation(), QtCore.Qt.Vertical)
        row.setSizes([250, 500])
        stacked_ratio = row.sizes()[0] / sum(row.sizes())
        cameras = [view.get_navigation_camera() for view in views]
        self.panel.set_layout_mode(modes.TWO_SIDE_BY_SIDE)
        QtCore.QCoreApplication.processEvents()
        self.assertEqual(row.orientation(), QtCore.Qt.Horizontal)
        row.setSizes([300, 600])
        side_ratio = row.sizes()[0] / sum(row.sizes())
        self.panel.set_layout_mode(modes.FOUR_PANES)
        self.panel.set_active_pane(3)
        self.panel.set_layout_mode(modes.TWO_STACKED)
        QtCore.QCoreApplication.processEvents()
        self.assertEqual(self.panel.get_active_pane(), 1)
        self.assertFalse(self.panel.get_pane(2).isVisible())
        self.assertFalse(self.panel.get_pane(3).isVisible())
        self.assertAlmostEqual(row.sizes()[0] / sum(row.sizes()), stacked_ratio, delta=0.01)
        for index, view in enumerate(views):
            self.assertEqual(view.get_navigation_camera().transform, cameras[index].transform)
        self.panel.set_layout_mode(modes.TWO_SIDE_BY_SIDE)
        QtCore.QCoreApplication.processEvents()
        self.assertAlmostEqual(row.sizes()[0] / sum(row.sizes()), side_ratio, delta=0.01)

    def test_two_pane_state_and_space_restore_the_selected_layout(self):
        for mode in (
            core.ViewportPanel.LayoutMode.TWO_STACKED,
            core.ViewportPanel.LayoutMode.TWO_SIDE_BY_SIDE,
        ):
            with self.subTest(mode=mode):
                self.panel.set_layout_mode(mode)
                self.panel.activateWindow()
                widget = self.panel.get_pane(0).get_viewport_view().get_widget()
                widget.setFocus()
                QtCore.QCoreApplication.processEvents()
                action = next(
                    action
                    for action in widget.actions()
                    if action.objectName() == "viewport_toggle_pane_layout"
                )
                action.trigger()
                self.assertTrue(self.panel.is_maximized())
                state = self.panel.save_state()
                restored = self.create_panel()
                self.assertTrue(restored.restore_state(state))
                self.assertEqual(restored.get_layout_mode(), mode)
                self.assertTrue(restored.is_maximized())
                restored.restore_panes()
                self.assertTrue(restored.get_pane(0).isVisible())
                self.assertTrue(restored.get_pane(1).isVisible())
                action.trigger()
                self.assertFalse(self.panel.is_maximized())
                self.assertEqual(self.panel.get_layout_mode(), mode)

    def test_version_one_layout_state_still_restores(self):
        self.quad()
        data = json.loads(bytes(self.panel.save_state()))
        data["version"] = 1
        data["quad"] = True
        data["splitters"] = data.pop("layout_splitters")[1]
        del data["mode"]
        restored = self.create_panel()
        self.assertTrue(restored.restore_state(QtCore.QByteArray(json.dumps(data).encode())))
        self.assertEqual(restored.get_layout_mode(), core.ViewportPanel.LayoutMode.FOUR_PANES)
        self.assertEqual(restored.get_active_pane(), self.panel.get_active_pane())
        for i in range(4):
            self.assertEqual(
                restored.get_pane(i).get_viewport_view().get_camera().transform,
                self.panel.get_pane(i).get_viewport_view().get_camera().transform,
            )

    def test_stacked_viewport_tabs_present_a_frame_on_switch(self):
        if tuple(map(int, QtCore.qVersion().split(".")[:2])) < (6, 4):
            self.skipTest("Qt 6.4 introduced the backing-store composition path under test")
        from PySide6.QtCore import Q_ARG

        self.panel.hide()
        window = self.app.get_main_window()
        first = core.create_panel("viewport", False)
        second = core.create_panel("viewport", False, first.dockAreaWidget())
        panels = first.widget(), second.widget()
        area = panels[1].parentWidget()
        while area is not None and not area.inherits("ads::CDockAreaWidget"):
            area = area.parentWidget()
        self.assertIsNotNone(area)
        self.addCleanup(
            QtCore.QMetaObject.invokeMethod, area, "closeArea", QtCore.Qt.DirectConnection
        )

        def switch(index):
            self.assertTrue(
                QtCore.QMetaObject.invokeMethod(
                    area, "setCurrentIndex", QtCore.Qt.DirectConnection, Q_ARG(int, index)
                )
            )

        def background_pixel(panel):
            widget = panel.get_pane(1).get_viewport_view().get_widget()
            point = widget.mapTo(window, QtCore.QPoint(22, 70))
            image = window.screen().grabWindow(window.winId(), point.x(), point.y(), 1, 1).toImage()
            if image.isNull():
                self.skipTest("The display backend cannot capture the composed window")
            return image.pixelColor(0, 0).getRgb()[:3]

        for index, panel in enumerate(panels):
            switch(index)
            panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
            QtCore.QCoreApplication.processEvents()

        expected_background = background_pixel(panels[1])
        self.assertGreater(max(expected_background), 0)
        for index in (0, 1, 0, 1):
            with self.subTest(tab=index):
                switch(index)
                QtCore.QCoreApplication.processEvents()
                self.assertTrue(panels[index].isVisible())
                self.assertEqual(background_pixel(panels[index]), expected_background)

    def test_named_layout_preserves_separate_viewport_panels(self):
        first = core.create_panel("viewport", False)
        second = core.create_panel("viewport", False)
        first_panel, second_panel = first.widget(), second.widget()
        first_panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
        first_panel.maximize_pane(0)
        self.assertEqual(second_panel.get_layout_mode(), core.ViewportPanel.LayoutMode.SINGLE)
        names = first.objectName(), second.objectName()
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "quad.ini")
            core.save_layout(path)
            core.load_layout(path)
        window = self.app.get_main_window()
        docks = [window.findChild(QtWidgets.QWidget, name) for name in names]
        self.assertTrue(docks[0].findChild(core.ViewportPanel).is_maximized())
        self.assertEqual(docks[0].findChild(core.ViewportPanel).get_active_pane(), 0)
        self.assertEqual(
            docks[1].findChild(core.ViewportPanel).get_layout_mode(),
            core.ViewportPanel.LayoutMode.SINGLE,
        )

    def test_stage_replacement_updates_each_pane_and_panel_destruction_releases_views(self):
        views = self.quad()
        for index, view in enumerate(views):
            self.capture_cube(view, "stage_" + str(index))
        replacement = Usd.Stage.CreateInMemory()
        UsdGeom.Cube.Define(replacement, "/Replacement")
        self.addCleanup(self.app.get_session().close_stage, replacement)
        self.app.get_session().set_current_stage(replacement)
        for index, view in enumerate(views):
            self.capture_cube(view, "replacement_" + str(index))
            picked = view.pick_single_prim(
                project_to_viewport(view, (0, 0, 0)), int(core.MergeFlags.FULL_SELECTION)
            )
            self.assertEqual(list(picked.get_fully_selected_paths()), [Sdf.Path("/Replacement")])
        self.panel.close()
        self.panel.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        for view in views:
            with self.assertRaisesRegex(RuntimeError, "viewport widget has been destroyed"):
                view.get_camera()
