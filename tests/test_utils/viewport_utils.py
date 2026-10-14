# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from contextlib import contextmanager

from pxr import Gf


@contextmanager
def disabled_depth_writes(view):
    """Emulate Qt composition's GL state and check that picking leaves it intact."""
    from Qt import QtCompat, QtOpenGL, QtWidgets

    if hasattr(QtWidgets, "QOpenGLWidget"):
        widget_type = QtWidgets.QOpenGLWidget
    else:
        from PySide6.QtOpenGLWidgets import QOpenGLWidget

        widget_type = QOpenGLWidget
    widget = QtCompat.wrapInstance(QtCompat.getCppPointer(view.get_widget()), widget_type)
    widget.makeCurrent()
    functions = QtOpenGL.QOpenGLFunctions_2_0()
    if not functions.initializeOpenGLFunctions():
        widget.doneCurrent()
        raise RuntimeError("Could not initialize viewport GL functions")
    depth_write_mask = 0x0B72  # GL_DEPTH_WRITEMASK
    previous = functions.glGetBooleanv(depth_write_mask)
    functions.glDepthMask(False)
    widget.doneCurrent()
    try:
        yield
    finally:
        widget.makeCurrent()
        unchanged = not functions.glGetBooleanv(depth_write_mask)
        functions.glDepthMask(previous)
        widget.doneCurrent()
        assert unchanged, "Picking changed the caller's depth-write mask"


def project_to_viewport(view, position):
    """Project a world position to physical viewport pixels, with the origin at the top left."""
    _, _, width, height = view.get_viewport_dimensions()
    camera = view.get_camera()
    # The viewport fits the camera aperture to its aspect ratio.
    camera.horizontalAperture = max(
        camera.horizontalAperture, camera.verticalAperture * width / height
    )
    camera.verticalAperture = max(
        camera.verticalAperture, camera.horizontalAperture * height / width
    )
    frustum = camera.frustum
    world_to_clip = frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix()
    point = world_to_clip.Transform(Gf.Vec3d(*position))
    return Gf.Vec2f((point[0] + 1) * width / 2, (1 - point[1]) * height / 2)
