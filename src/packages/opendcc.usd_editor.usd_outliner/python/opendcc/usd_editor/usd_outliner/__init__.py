# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import opendcc.core as dcc_core
from opendcc.packaging import PackageEntryPoint
from opendcc.i18n import i18n
from Qt import QtWidgets

from ._usd_outliner import UsdStageDataSource, UsdSelectionContext, usd_item_id, usd_path_of
from .outliner import UsdOutliner

PANEL_TYPE = "outliner"


class UsdOutlinerEntryPoint(PackageEntryPoint):
    def initialize(self, package):
        if not QtWidgets.QApplication.instance():
            return
        dcc_core.PanelFactory.instance().register_panel(
            PANEL_TYPE,
            UsdOutliner,
            i18n("panels", "USD Outliner"),
            icon=":/icons/panel_outliner",
        )

    def uninitialize(self, package):
        dcc_core.PanelFactory.instance().unregister_panel(PANEL_TYPE)
