# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui
from opendcc.actions.options_widget import make_option
from opendcc.core import Application
from opendcc.common_widgets import RolloutWidget

from opendcc.i18n import i18n


class ColorManagementPage(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super(ColorManagementPage, self).__init__(parent=parent)
        self.setObjectName(i18n("preferences.color_management", "Color Management"))
        layout = QtWidgets.QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        rollout = RolloutWidget(
            i18n("preferences.color_management", "Color Management"), expandable=False
        )
        rollout_layout = QtWidgets.QFormLayout()
        rollout.set_layout(rollout_layout)
        layout.addWidget(rollout)
        layout.addStretch()
        self.setLayout(layout)

        self.options = []

        self.options.append(
            make_option(
                i18n("preferences.color_management", "Color Management:"),
                "colormanagement.color_management",
                {"type": "option", "options": ["disabled", "sRGB", "openColorIO"]},
                "openColorIO",
            )
        )

        (
            rendering_spaces,
            view_transforms,
            default_rendering_space,
            default_view,
        ) = Application.instance().get_ocio_config()

        self.options.append(
            make_option(
                i18n("preferences.color_management", "OpenColorIO Rendering Space:"),
                "colormanagement.ocio_rendering_space",
                {"type": "option", "options": rendering_spaces},
                default_rendering_space,
            )
        )

        self.options.append(
            make_option(
                i18n("preferences.color_management", "OpenColorIO View Transform:"),
                "colormanagement.ocio_view_transform",
                {"type": "option", "options": view_transforms},
                default_view,
            )
        )

        for option in self.options:
            option.create_widget(rollout_layout, self)

        self.restore_previous_settings()

    def save_settings(self):
        for option in self.options:
            option.save_options()

    def restore_previous_settings(self):
        for option in self.options:
            option.load_options()

    def get_ocio(self):
        config = OCIO.GetCurrentConfig()
        rendering_spaces = [x.getName() for x in config.getColorSpaces()]
        default_display = config.getDefaultDisplay()
        view_transforms = config.getViews(default_display)
        return rendering_spaces, view_transforms
