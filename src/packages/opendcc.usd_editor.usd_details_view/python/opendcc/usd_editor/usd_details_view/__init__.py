# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from opendcc.packaging import PackageEntryPoint
import opendcc.core as dcc_core
from Qt import QtWidgets
from opendcc.i18n import i18n


class USDDetailsViewEntryPoint(PackageEntryPoint):
    def initialize(self, package):
        from .usd_details_view import UsdDetailsView

        # Register before UI load: the Panels menu is built once during startup.
        if QtWidgets.QApplication.instance():
            registry = dcc_core.PanelFactory.instance()
            registry.register_panel(
                "usd_details_view",
                lambda: UsdDetailsView(),
                i18n("panels", "USD Details View"),
                False,
                ":/icons/panel_usd_details_view",
                "USD",
            )

    def uninitialize(self, package):
        dcc_core.PanelFactory.instance().unregister_panel("usd_details_view")
