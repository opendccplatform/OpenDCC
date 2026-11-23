# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import os
from pathlib import Path
import sys
import unittest

prefix = Path(sys.argv.pop(1))
sys.path[:0] = [
    str(prefix / "python/lib/site-packages"),
    str(prefix / "packages/opendcc.ui.scene_outliner/python"),
]
handles = []
if sys.platform == "win32":
    directories = [
        prefix / "bin",
        prefix / "lib",
        prefix / "packages/opendcc.ui.scene_outliner/lib",
    ]
    for name in ("shiboken6", "PySide6", "shiboken2", "PySide2"):
        spec = importlib.util.find_spec(name)
        if spec and spec.submodule_search_locations:
            directories.extend(Path(path) for path in spec.submodule_search_locations)
    handles = [os.add_dll_directory(str(path)) for path in directories if path.is_dir()]

from Qt import QtCore, QtWidgets
from opendcc.ui.scene_outliner import (
    Column,
    DataSource,
    SceneItemId,
    SceneOutliner,
    SceneOutlinerProxyModel,
    SceneOutlinerTreeView,
    _scene_outliner,
)


class Source(DataSource):
    def columns(self):
        column = Column()
        column.id = "name"
        column.title = "Name"
        column.role = Column.Role.TREE
        return [column]

    def roots(self):
        return [SceneItemId("example", "root")]

    def children(self, parent):
        return []

    def parent(self, item):
        return SceneItemId()

    def has_item(self, item):
        return item == self.roots()[0]

    def display_name(self, item):
        return item.key


class TestStandalone(unittest.TestCase):
    def test_native_only_methods_are_not_exposed(self):
        self.assertFalse(hasattr(SceneOutlinerProxyModel, "set_hidden_filters"))
        self.assertFalse(hasattr(SceneOutlinerTreeView, "expand_branch_to"))
        self.assertFalse(hasattr(SceneOutlinerTreeView, "collapse_branch_to"))

    def test_native_widget_and_python_backend_without_application_or_usd(self):
        self.assertIs(SceneOutliner, _scene_outliner.SceneOutliner)
        widget = SceneOutliner(Source())
        self.assertTrue(widget.index_for_item(SceneItemId("example", "root")).isValid())
        self.assertEqual(widget.menu_bar().actions(), [])
        self.assertNotIn("opendcc.core", sys.modules)
        self.assertFalse(any(name == "pxr" or name.startswith("pxr.") for name in sys.modules))
        widget.deleteLater()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)


if __name__ == "__main__":
    plugins = prefix / "qt-plugins"
    if plugins.is_dir():
        QtCore.QCoreApplication.setLibraryPaths([str(plugins)])
    application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    unittest.main()
