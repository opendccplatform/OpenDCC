# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from pxr import Gf


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
