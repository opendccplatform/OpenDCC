# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtWidgets
from pxr import Gf
import sys

import opendcc.core as dcc_core
from opendcc.common_widgets import DoubleWidget, RolloutWidget
from opendcc.i18n import i18n


class DefCamPage(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName(i18n("preferences.def_cam", "Default Camera"))
        self._edited = set()

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self._rollout = RolloutWidget(self.objectName(), expandable=False)
        self._form = QtWidgets.QFormLayout()
        self._rollout.set_layout(self._form)
        layout.addWidget(self._rollout)
        layout.addStretch()

        self._camera_picker = QtWidgets.QComboBox()
        presets = dcc_core.ViewportViewPreset
        self._presets = (presets.PERSPECTIVE, presets.TOP, presets.FRONT, presets.SIDE)
        self._camera_picker.addItems(
            [dcc_core.ViewportView.get_view_preset_label(preset) for preset in self._presets]
        )
        self._form.addRow(i18n("preferences.def_cam", "Def Cam:"), self._camera_picker)

        self._fields = {}
        for field, label in (
            ("fov", i18n("preferences.def_cam", "Field of view:")),
            ("focal_length", i18n("preferences.def_cam", "Focal length:")),
            ("aperture", i18n("preferences.def_cam", "Horizontal aperture:")),
            ("size", i18n("preferences.def_cam", "Orthographic size:")),
            ("near", i18n("preferences.def_cam", "Near clip plane:")),
            ("far", i18n("preferences.def_cam", "Far clip plane:")),
        ):
            widget = DoubleWidget()
            widget.set_range(0.0001, sys.float_info.max)
            self._form.addRow(label, widget)
            self._fields[field] = widget
            widget.value_changed.connect(lambda value, field=field: self._set_field(field, value))

        self._fields["fov"].set_range(0.0001, 179.9999)
        self._fields["fov"].enable_slider(5, 120)
        self._fields["focal_length"].enable_slider(5, 250)
        self._camera_picker.currentIndexChanged.connect(self.load_values)
        self.load_values()

    def load_values(self):
        preset = self._presets[self._camera_picker.currentIndex()]
        camera = dcc_core.ViewportView.get_preset_camera_settings(preset)
        perspective = camera.projection == Gf.Camera.Perspective
        values = {
            "fov": camera.GetFieldOfView(Gf.Camera.FOVHorizontal) if perspective else 0,
            "focal_length": camera.focalLength,
            "aperture": camera.horizontalAperture,
            "size": camera.horizontalAperture * Gf.Camera.APERTURE_UNIT,
            "near": camera.clippingRange.GetMin(),
            "far": camera.clippingRange.GetMax(),
        }
        lens_fields = ("fov", "focal_length", "aperture") if perspective else ("size",)
        visible_fields = lens_fields + ("near", "far")
        for field, widget in self._fields.items():
            visible = field in visible_fields
            self._form.labelForField(widget).setVisible(visible)
            widget.setVisible(visible)
            blocker = QtCore.QSignalBlocker(widget)
            if field in ("near", "far"):
                widget.set_range(0.0001 if perspective else -sys.float_info.max, sys.float_info.max)
            if visible:
                widget.set_value(values[field])
            del blocker

    def _set_field(self, field, value):
        preset = self._presets[self._camera_picker.currentIndex()]
        camera = dcc_core.ViewportView.get_preset_camera_settings(preset)
        if field == "fov":
            camera.SetPerspectiveFromAspectRatioAndFieldOfView(
                camera.aspectRatio, value, Gf.Camera.FOVHorizontal, camera.horizontalAperture
            )
        elif field == "focal_length":
            camera.focalLength = value
        elif field == "aperture":
            camera.horizontalAperture = value
        elif field == "size":
            camera.horizontalAperture = value / Gf.Camera.APERTURE_UNIT
        else:
            near = value if field == "near" else camera.clippingRange.GetMin()
            far = value if field == "far" else camera.clippingRange.GetMax()
            if near >= far:
                self.load_values()
                return
            camera.clippingRange = Gf.Range1f(near, far)

        dcc_core.ViewportView.preview_preset_camera_settings(preset, camera)
        self._edited.add(preset)
        self.load_values()

    def save_settings(self):
        for preset in self._edited:
            dcc_core.ViewportView.finish_preset_camera_settings_edit(preset, True)
        self._edited.clear()

    def restore_previous_settings(self):
        for preset in self._edited:
            dcc_core.ViewportView.finish_preset_camera_settings_edit(preset, False)
        self._edited.clear()
        self.load_values()
