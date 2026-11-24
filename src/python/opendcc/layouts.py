# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui
import opendcc.core as dcc_core
import os
from functools import partial
from opendcc.core import close_panels, create_panel, arrange_splitters, DockWidgetArea
import warnings

from opendcc.i18n import i18n

# To integrate a QToolBar with layouts, you must:
# * Assign an object name using setObjectName()
# * Set property opendcc_toolbar True
# * Set property opendcc_toolbar_side (top, left, right, bottom)
# * Set property opendcc_toolbar_row (0, 1, 2...)
# * Set property opendcc_toolbar_index (for ordering)

# Example:

"""
toolbar = QtWidgets.QToolBar("My Toolbar")
toolbar.setObjectName("my_toolbar")
toolbar.setProperty("opendcc_toolbar", True)
toolbar.setProperty("opendcc_toolbar_side", "right")
toolbar.setProperty("opendcc_toolbar_row", 0)
toolbar.setProperty("opendcc_toolbar_index", 0)
"""


def rebuild_toolbars(top, left, right, bottom):
    app = dcc_core.Application.instance()
    mw = app.get_main_window()

    widgets = {}
    top_list = [item for sublist in top for item in sublist]
    left_list = [item for sublist in left for item in sublist]
    right_list = [item for sublist in right for item in sublist]
    bottom_list = [item for sublist in bottom for item in sublist]
    toolbars = top_list + left_list + right_list + bottom_list

    for name in toolbars:
        widget = mw.findChild(QtWidgets.QToolBar, name)
        if not widget:
            warnings.warn('Warning: "%s" toolbar not found' % name)
            continue

        widgets[name] = widget
        mw.removeToolBar(widget)

    def build_row(rows, area):
        for idx, row in enumerate(rows):
            for name in row:
                if name in widgets:
                    mw.addToolBar(area, widgets[name])
            if len(rows) > 1 and idx != len(rows):
                mw.addToolBarBreak(area)

    build_row(top, QtCore.Qt.ToolBarArea.TopToolBarArea)
    build_row(left, QtCore.Qt.ToolBarArea.LeftToolBarArea)
    build_row(right, QtCore.Qt.ToolBarArea.RightToolBarArea)
    build_row(bottom, QtCore.Qt.ToolBarArea.BottomToolBarArea)

    for _, widget in widgets.items():
        widget.show()


def restore_toolbars():

    top_dict = {}
    left_dict = {}
    right_dict = {}
    bottom_dict = {}

    app = dcc_core.Application.instance()
    mw = app.get_main_window()

    def add_item(side_dict, row, index, name):
        if not row in side_dict:
            side_dict[row] = []

        for item in side_dict[row]:
            if item[0] == index:
                warnings.warn('"%s" index collision' % name)

        side_dict[row].append([index, name])

    widgets = mw.findChildren(QtWidgets.QToolBar)
    for widget in widgets:
        name = widget.objectName()
        if widget.property("opendcc_toolbar"):
            if not name:
                warnings.warn("%s name isn't set" % str(widget))
                continue
            side = widget.property("opendcc_toolbar_side")
            if not isinstance(side, str):
                warnings.warn('"%s" side isn\'t set' % name)
                continue
            row = widget.property("opendcc_toolbar_row")
            if not isinstance(row, int):
                warnings.warn('"%s" row isn\'t set' % name)
                continue
            index = widget.property("opendcc_toolbar_index")
            if not isinstance(index, int):
                warnings.warn('"%s" index isn\'t set' % name)
                continue

            if side == "top":
                add_item(top_dict, row, index, name)

            elif side == "left":
                add_item(left_dict, row, index, name)

            elif side == "right":
                add_item(right_dict, row, index, name)

            elif side == "bottom":
                add_item(bottom_dict, row, index, name)

    def convert_dict(side):
        result = []
        for row in sorted(side.keys()):
            row_final = []
            for item in sorted(side[row], key=lambda x: x[0]):
                row_final.append(item[1])
            result.append(row_final)
        return result

    top = convert_dict(top_dict)
    left = convert_dict(left_dict)
    right = convert_dict(right_dict)
    bottom = convert_dict(bottom_dict)

    rebuild_toolbars(top, left, right, bottom)


def default_layout():
    close_panels()
    restore_toolbars()
    viewport = create_panel("viewport", False)
    outliner = create_panel(
        "outliner", False, viewport.dockAreaWidget(), DockWidgetArea.LeftDockWidgetArea
    )
    arrange_splitters(viewport, [1.0, 1.62])
    attribute_editor = create_panel(
        "attribute_editor", False, outliner.dockAreaWidget(), DockWidgetArea.BottomDockWidgetArea
    )
    arrange_splitters(attribute_editor, [1.0, 1.0])


def scripting_layout():
    close_panels()
    restore_toolbars()
    viewport = create_panel("viewport", False)
    outliner = create_panel(
        "outliner", False, viewport.dockAreaWidget(), DockWidgetArea.LeftDockWidgetArea
    )
    script_editor = create_panel(
        "script_editor", False, viewport.dockAreaWidget(), DockWidgetArea.RightDockWidgetArea
    )
    arrange_splitters(script_editor, [0.5, 1.62, 0.5])
    layers = create_panel(
        "usd_layers", False, viewport.dockAreaWidget(), DockWidgetArea.BottomDockWidgetArea
    )
    usd_details_view = create_panel("usd_details_view", False, layers.dockAreaWidget())
    logger = create_panel("logger", False, layers.dockAreaWidget())
    arrange_splitters(layers, [1.62, 1.0])
    attribute_editor = create_panel(
        "attribute_editor", False, outliner.dockAreaWidget(), DockWidgetArea.BottomDockWidgetArea
    )
    arrange_splitters(attribute_editor, [1.0, 1.0])


def get_layouts():
    result = []
    settings_path = dcc_core.Application.instance().get_settings_path()
    layouts_path = os.path.join(settings_path, "layouts")
    layouts_path_exists = os.path.isdir(layouts_path)
    if layouts_path_exists:
        for filename in os.listdir(layouts_path):
            if filename.endswith(".ini"):
                result.append([os.path.splitext(filename)[0], os.path.join(layouts_path, filename)])
    result = sorted(result, key=lambda x: x[0])
    return result


def rebuild_menu():
    app = dcc_core.Application.instance()
    mw = app.get_main_window()
    menu_bar = mw.menuBar()
    for menu in menu_bar.findChildren(QtWidgets.QMenu):
        if menu.objectName() == "layouts_menu":
            for action in menu.actions():
                if action.property("user-created"):
                    action.deleteLater()
            separator = menu.findChildren(QtWidgets.QAction, "layouts_menu_separator")[0]
            first_action = None
            for item in get_layouts():
                action = QtWidgets.QAction(menu)
                if not first_action:
                    first_action = action
                action.setIcon(QtGui.QIcon(":icons/layout"))
                action.setText(item[0])
                action.triggered.connect(partial(dcc_core.load_layout, item[1]))
                action.setProperty("user-created", True)
                menu.insertAction(separator, action)
            if first_action:
                last_separator = QtWidgets.QAction(menu)
                last_separator.setSeparator(True)
                menu.insertAction(first_action, last_separator)
                last_separator.setProperty("user-created", True)
            break


def save_layout():
    app = dcc_core.Application.instance()
    settings_path = app.get_settings_path()
    layouts_path = os.path.join(settings_path, "layouts")
    layouts_path_exists = os.path.isdir(layouts_path)
    if not layouts_path_exists:
        os.makedirs(layouts_path)
    name, ok = QtWidgets.QInputDialog.getText(
        app.get_main_window(),
        i18n("main_menu.layouts", "Save Layout"),
        i18n("main_menu.layouts", "Layout Name:"),
        QtWidgets.QLineEdit.Normal,
        i18n("main_menu.layouts", "Custom"),
    )
    if ok:
        save_path = layouts_path + "/" + name + ".ini"
        if os.path.isfile(save_path):  # not even gonna ask
            os.remove(save_path)
        dcc_core.save_layout(save_path)
        rebuild_menu()


class DeleteLayout(QtWidgets.QDialog):
    """A widget for deleting layouts"""

    def __init__(self, parent):
        QtWidgets.QDialog.__init__(self, parent=parent)
        self.setWindowFlags(self.windowFlags() & ~QtCore.Qt.WindowContextHelpButtonHint)
        self.setWindowTitle(i18n("main_menu.layouts", "Delete Layout"))
        self.setObjectName("delete_layout_dialog")
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)

        layout = QtWidgets.QVBoxLayout()
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        self.setLayout(layout)

        self.layouts = QtWidgets.QComboBox()

        layout.addWidget(self.layouts)

        buttons_layout = QtWidgets.QHBoxLayout()
        delete_button = QtWidgets.QPushButton(i18n("main_menu.layouts", "Delete"))
        cancel_button = QtWidgets.QPushButton(i18n("main_menu.layouts", "Cancel"))
        buttons_layout.addStretch()
        buttons_layout.addWidget(delete_button)
        buttons_layout.addWidget(cancel_button)
        layout.addLayout(buttons_layout)

        layout_list = get_layouts()

        if layout_list:
            for item in get_layouts():
                self.layouts.addItem(QtGui.QIcon(":icons/layout"), item[0], item[1])
        else:
            self.layouts.addItem(QtGui.QIcon(":icons/layout"), i18n("main_menu.layouts", "Empty"))
            self.layouts.setEnabled(False)
            delete_button.setEnabled(False)

        cancel_button.clicked.connect(self.close)
        delete_button.clicked.connect(self._delete)

    def _delete(self):
        os.remove(self.layouts.currentData())
        rebuild_menu()
        self.close()


def delete_layout():
    app = dcc_core.Application.instance()
    dialog = DeleteLayout(app.get_main_window())
    dialog.exec_()
