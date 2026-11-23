# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from ._scene_outliner import (
    SceneOutliner,
    SceneOutlinerModel,
    SceneOutlinerTreeView,
    SceneOutlinerProxyModel,
    SceneItemId,
    Column,
    Change,
    Filter,
    DataSource,
    Selection,
)

from Qt import __binding__

if __binding__ == "PySide2":
    # Shiboken2 does not expose the explicit C++ bool conversion.
    SceneItemId.__bool__ = SceneItemId.is_valid
