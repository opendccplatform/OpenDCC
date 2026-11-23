# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from contextlib import contextmanager
import ctypes
from ctypes.util import find_library
import sys

from pxr import Gf


def _depth_write_mask(functions):
    depth_write_mask = 0x0B72  # GL_DEPTH_WRITEMASK
    if hasattr(functions, "glGetBooleanv"):
        return functions.glGetBooleanv(depth_write_mask)

    # PySide2 does not expose glGetBooleanv's output pointer.
    if sys.platform == "win32":
        gl = ctypes.WinDLL("opengl32")
    else:
        library = find_library("OpenGL" if sys.platform == "darwin" else "GL")
        if not library:
            raise RuntimeError("Could not find the OpenGL library")
        gl = ctypes.CDLL(library)
    get_boolean = gl.glGetBooleanv
    get_boolean.argtypes = (ctypes.c_uint, ctypes.POINTER(ctypes.c_ubyte))
    get_boolean.restype = None
    value = ctypes.c_ubyte(0xFF)
    get_boolean(depth_write_mask, ctypes.byref(value))
    if value.value not in (0, 1):
        raise RuntimeError("Could not read the OpenGL depth-write mask")
    return bool(value.value)


@contextmanager
def _current_context(widget):
    widget.makeCurrent()
    try:
        yield
    finally:
        widget.doneCurrent()


@contextmanager
def disabled_depth_writes(view):
    """Emulate Qt composition's GL state and check that picking leaves it intact."""
    from Qt import QtCompat, QtOpenGL, QtWidgets, __binding__

    if hasattr(QtWidgets, "QOpenGLWidget"):
        widget_type = QtWidgets.QOpenGLWidget
    elif __binding__ == "PySide2":
        from PySide2.QtWidgets import QOpenGLWidget as widget_type
    else:
        from PySide6.QtOpenGLWidgets import QOpenGLWidget as widget_type

    widget = QtCompat.wrapInstance(QtCompat.getCppPointer(view.get_widget()), widget_type)
    with _current_context(widget):
        functions = QtOpenGL.QOpenGLFunctions_2_0()
        if not functions.initializeOpenGLFunctions():
            raise RuntimeError("Could not initialize viewport GL functions")
        previous = _depth_write_mask(functions)
        functions.glDepthMask(False)
    try:
        yield
    finally:
        with _current_context(widget):
            try:
                unchanged = not _depth_write_mask(functions)
            finally:
                functions.glDepthMask(previous)
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
