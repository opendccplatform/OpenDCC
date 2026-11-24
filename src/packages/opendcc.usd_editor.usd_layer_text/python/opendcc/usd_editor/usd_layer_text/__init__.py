# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from opendcc.packaging import PackageEntryPoint
import opendcc.core as dcc_core
from opendcc.i18n import i18n
from Qt import QtWidgets


class USDLayerTextEntryPoint(PackageEntryPoint):
    def initialize(self, package):
        from .usd_layer_text import UsdLayerText

        # Register before UI load: the Panels menu is built once during startup.
        if QtWidgets.QApplication.instance():
            registry = dcc_core.PanelFactory.instance()
            registry.register_panel(
                "usd_layer_text",
                lambda: UsdLayerText(),
                i18n("panels", "USD Layer Text"),
                singleton=True,
                icon=":/icons/panel_usd_layer_text",
            )

    def uninitialize(self, package):
        dcc_core.PanelFactory.instance().unregister_panel("usd_layer_text")
