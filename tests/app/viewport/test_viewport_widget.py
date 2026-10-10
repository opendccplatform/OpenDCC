# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
from pxr import Gf, Sdf, Usd
import opendcc.core as core
from image_utils import ImageDiffingTestCase


class TestViewportWidget(ImageDiffingTestCase):
    def create_viewport(self):
        viewport = core.ViewportWidget()
        self.addCleanup(viewport.deleteLater)
        self.addCleanup(viewport.close)
        return viewport

    def test_default_constructor_creates_independent_views(self):
        first = self.create_viewport()
        second = self.create_viewport()
        self.assertIsNone(first.parentWidget())
        self.assertIsNone(second.parentWidget())
        first_view = first.get_viewport_view()
        second_view = second.get_viewport_view()
        self.assertIsNot(first_view, second_view)
        self.assertIsNot(first_view.get_widget(), second_view.get_widget())
        self.assertIs(first_view.get_widget().parentWidget(), first)
        self.assertIs(second_view.get_widget().parentWidget(), second)

    def test_menu_and_toolbar_accept_qt_actions(self):
        viewport = self.create_viewport()
        menubar = viewport.get_menubar()
        toolbar = viewport.get_toolbar()
        self.assertIsInstance(menubar, QtWidgets.QMenuBar)
        self.assertIsInstance(toolbar, QtWidgets.QToolBar)
        self.assertTrue(viewport.isAncestorOf(menubar))
        self.assertTrue(viewport.isAncestorOf(toolbar))
        self.assertIs(viewport.get_menubar(), menubar)
        self.assertIs(viewport.get_toolbar(), toolbar)

        # Pass a Qt action back through the native toolbar API without changing its owner.
        action = toolbar.addAction("Test action")
        toolbar.removeAction(action)
        triggered = []
        action.triggered.connect(lambda checked: triggered.append(checked))
        viewport.toolbar_add_action(action)
        self.assertIn(action, toolbar.actions())
        self.assertIs(action.parent(), toolbar)
        action.trigger()
        self.assertEqual(triggered, [False])

    def test_framebuffer_tracks_viewport_size(self):
        viewport = self.create_viewport()
        view = viewport.get_viewport_view()
        session = core.Application.instance().get_session()
        stage = Usd.Stage.CreateInMemory()
        self.addCleanup(session.close_stage, stage)
        session.set_current_stage(stage)
        viewport.show()

        for width, height in ((160, 120), (240, 160)):
            with self.subTest(size=(width, height)):
                view.get_widget().setFixedSize(width, height)
                image = self.capture_viewport(view, "viewport_{}x{}.png".format(width, height))
                _, _, actual_width, actual_height = view.get_viewport_dimensions()
                self.assertEqual((actual_width, actual_height), (width, height))
                self.assertEqual((image.spec().width, image.spec().height), (width, height))
                framebuffer = view.grab_framebuffer()
                self.assertIsInstance(framebuffer, QtGui.QImage)
                self.assertEqual((framebuffer.width(), framebuffer.height()), (width, height))

    def test_qt_parent_owns_viewport(self):
        parent = QtWidgets.QWidget()
        try:
            viewport = core.ViewportWidget(parent)
            self.assertIsInstance(viewport, QtWidgets.QWidget)
            self.assertIs(viewport.parentWidget(), parent)
            view = viewport.get_viewport_view()
            self.assertIsInstance(view, core.ViewportView)
            self.assertIs(viewport.get_viewport_view(), view)
            self.assertIs(view.get_widget().parentWidget(), viewport)
            self.assertEqual(view.get_scene_context_type(), "USD")
        finally:
            parent.deleteLater()
            QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

        # Qt deletion must invalidate the Python wrapper too.
        with self.assertRaises(RuntimeError):
            viewport.get_viewport_view()

        # The shared view survives Qt deletion, but must reject access to its former widget.
        for method, args in (
            (view.get_widget, ()),
            (view.get_camera, ()),
            (view.get_viewport_dimensions, ()),
            (view.grab_framebuffer, ()),
            (view.get_scene_context_type, ()),
            (view.pick_single_prim, (Gf.Vec2f(0), int(core.MergeFlags.FULL_SELECTION))),
            (
                view.pick_multiple_prims,
                (Gf.Vec2f(0), Gf.Vec2f(1), int(core.MergeFlags.FULL_SELECTION)),
            ),
            (view.look_through, (Sdf.Path("/Camera"),)),
            (view.set_rollover_prim, (Sdf.Path(),)),
        ):
            with self.subTest(method=method.__name__):
                with self.assertRaisesRegex(RuntimeError, "viewport widget has been destroyed"):
                    method(*args)
