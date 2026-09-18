# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui
from .create_prim import CreatePrimAction
from .create_geometry_menu import create_geometry_actions
from .create_lights_menu import create_light_actions
from .create_reference import CreateReferenceAction
from pxr import Tf, Usd
import os, glob, uuid, copy, re
import json
import opendcc.core as dcc_core
from functools import partial

from opendcc.ui.code_editor.code_editor import CodeEditor
from opendcc.ui.script_editor.script_editor import ScriptEditor
from opendcc.color_theme import get_color_theme, ColorTheme
from opendcc.session_ui import subscribe_to_no_stage_signal

from opendcc.i18n import i18n


def execute(code):
    ScriptEditor.getInterpreter().runsource(code, symbol="exec")


def settings_path():
    path = dcc_core.Application.instance().get_settings_path()
    if not path:
        raise RuntimeError("Failed to get settings path")
    return "{0:s}/shelf".format(path)


class ShelfButton(QtWidgets.QFrame):
    """Tool that customizes the shelf"""

    def __init__(self, parent=None):
        QtWidgets.QFrame.__init__(self, parent)
        self.layout = QtWidgets.QBoxLayout(QtWidgets.QBoxLayout.TopToBottom)
        self.layout.addSpacing(2)
        self.select_button = QtWidgets.QPushButton()
        self.select_button.setStyleSheet("QPushButton::menu-indicator { image: none; }")
        self.select_button.setFlat(True)
        self.select_button.setIcon(QtGui.QIcon(":icons/equal"))
        self.select_button.setIconSize(QtCore.QSize(12, 12))
        self.select_button.setFixedSize(QtCore.QSize(16, 16))
        self.layout.addWidget(self.select_button, QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
        self.layout.addStretch()
        self.settings_button = QtWidgets.QPushButton()
        self.settings_button.setFlat(True)
        self.settings_button.setIcon(QtGui.QIcon(":icons/gear"))
        self.settings_button.setIconSize(QtCore.QSize(12, 12))
        self.settings_button.setFixedSize(QtCore.QSize(16, 16))
        self.layout.setContentsMargins(0, 0, 0, 0)  # heh?
        self.layout.addWidget(self.settings_button, QtCore.Qt.AlignHCenter | QtCore.Qt.AlignVCenter)
        self.layout.addSpacing(2)
        self.setLayout(self.layout)
        if get_color_theme() == ColorTheme.DARK:
            self.setStyleSheet("QFrame {background: rgb(55, 55, 55);}")
        else:
            self.setStyleSheet("QFrame {background: palette(light);}")

    def setOrientation(self, case):
        if case == 0:
            self.layout.setDirection(QtWidgets.QBoxLayout.TopToBottom)
            # self.setSizePolicy(QtWidgets.QSizePolicy.Maximum, QtWidgets.QSizePolicy.Minimum)
        else:
            self.layout.setDirection(QtWidgets.QBoxLayout.LeftToRight)
            # self.setSizePolicy(QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Maximum)


class MTabWidget(QtWidgets.QTabWidget):
    def __init__(self, parent=None):
        QtWidgets.QTabWidget.__init__(self, parent)
        self.setStyle()

    def setStyle(self):
        """Set styles for outliner"""
        resourceDir = os.path.dirname(os.path.realpath(__file__)) + "/"
        if get_color_theme() == ColorTheme.DARK:
            stylesheet_filename = "tab_toolbar.qss"
        else:
            stylesheet_filename = "tab_toolbar_light.qss"
        with open(os.path.join(resourceDir, stylesheet_filename), "r") as sheet:
            sheetString = sheet.read()
            self.setStyleSheet(sheetString)


class Shelf(QtWidgets.QToolBar):

    __instance = None

    @staticmethod
    def instance():
        """Static access method."""
        if Shelf.__instance == None:
            Shelf()
        return Shelf.__instance

    def change_orientation(self, orientation):
        if orientation == QtCore.Qt.Vertical:
            self.tab_widget.setTabPosition(QtWidgets.QTabWidget.West)
            self.tool_button.setOrientation(1)
        else:
            self.tab_widget.setTabPosition(QtWidgets.QTabWidget.North)
            self.tool_button.setOrientation(0)

        for index in range(self.tab_widget.count()):
            widget = self.tab_widget.widget(index)
            if "setOrientation" in dir(widget):
                widget.setOrientation(orientation)

    def add_tab(self, widget, title):
        self.tab_widget.insertTab(self.tabs_number, widget, title)
        self.tabs_number += 1
        self.widgets[title] = widget

        if self.already_built:  # it should be faster to load this way hopefully
            self.create_select_menu()

    def get_tab_widget(self, title):
        """get non-custom tab"""
        return self.widgets[title]

    def __init__(self, parent=None):
        Shelf.__instance = self

        QtWidgets.QToolBar.__init__(self, i18n("shelf.toolbar", "Shelf"), parent=parent)

        self.tool_button = ShelfButton(self)
        self.addWidget(self.tool_button)

        spacer = QtWidgets.QWidget()
        spacer.setFixedSize(1, 1)
        self.addWidget(spacer)

        self.tab_widget = MTabWidget(self)
        self.addWidget(self.tab_widget)

        self.widgets = {}
        self.tabs_number = 0
        self.already_built = False

        self.setObjectName("shelf")
        self.setProperty("opendcc_toolbar", True)
        self.setProperty("opendcc_toolbar_side", "top")
        self.setProperty("opendcc_toolbar_row", 1)
        self.setProperty("opendcc_toolbar_index", 0)

        self.orientationChanged.connect(self.change_orientation)
        app = dcc_core.Application.instance()

        # General Tab
        self.tool_bar_general = QtWidgets.QToolBar(i18n("shelf.toolbar", "General"), self)
        self.tool_bar_general.setIconSize(QtCore.QSize(20, 20))
        self.tool_bar_general.addAction(CreateReferenceAction(parent=self.tool_bar_general))
        self.tool_bar_general.addAction(
            CreatePrimAction(
                "Camera",
                "Camera",
                icon=QtGui.QIcon(":icons/camera.png"),
                label=i18n("shelf.toolbar", "Camera"),
                parent=self.tool_bar_general,
            )
        )
        self.add_tab(self.tool_bar_general, i18n("shelf.toolbar", "General"))

        # Geometry Tab
        self.tool_bar_geometry = QtWidgets.QToolBar(i18n("shelf.toolbar", "Geometry"), self)

        geometry_actions = create_geometry_actions(self.tool_bar_geometry)
        if geometry_actions:
            self.tool_bar_geometry.addActions(geometry_actions)

        self.tool_bar_geometry.addSeparator()

        self.add_tab(self.tool_bar_geometry, i18n("shelf.toolbar", "Geometry"))

        # Light Tab
        tool_bar_light_name = i18n("shelf.toolbar", "Light")
        self.tool_bar_light = QtWidgets.QToolBar(tool_bar_light_name, self)

        light_actions = create_light_actions(self.tool_bar_light)
        if light_actions:
            self.tool_bar_light.addActions(light_actions)
        self.add_tab(self.tool_bar_light, tool_bar_light_name)

        # self.tool_bar_utils = QtWidgets.QToolBar('Utils', self)
        # self.start_watch_action = QtWidgets.QAction("Start Filesystem Watch")
        # self.start_watch_action.triggered.connect(data_watcher.start_watch_layers)
        # self.stop_watch_action = QtWidgets.QAction("Stop Filesystem Watch")
        # self.stop_watch_action.triggered.connect(data_watcher.stop_watch_layers)
        # self.tool_bar_utils.addAction(self.start_watch_action)
        # self.tool_bar_utils.addAction(self.stop_watch_action)
        # self.add_tab(self.tool_bar_utils, 'Utils')

        subscribe_to_no_stage_signal(
            [self.tool_bar_general, self.tool_bar_geometry, self.tool_bar_light]
        )

        # Custom Tabs
        self.shelves = []
        self.custom_tabs = []
        self.settings_path = settings_path()
        self.settings = app.get_ui_settings()
        self.settings_order_key = "shelf_order"

        self.setAcceptDrops(True)
        self.load_custom_tabs()
        self.rebuild_custom_tabs()
        self.tab_widget.currentChanged.connect(self.currentChanged)
        self.tool_button.settings_button.clicked.connect(self.edit_tabs)

        self.already_built = True

    def currentChanged(self, index):
        self.select_menu.actions()[index].setChecked(True)

    def create_select_menu(self):
        self.select_menu = QtWidgets.QMenu()
        group = QtWidgets.QActionGroup(self)
        group.setExclusive(True)
        for index in range(self.tab_widget.count()):
            name = self.tab_widget.tabText(index)
            checked = self.tab_widget.currentIndex() == index
            action = QtWidgets.QAction(name)
            action.setCheckable(True)
            action.setChecked(checked)

            def toggle(checked, index=index):
                if checked:
                    self.tab_widget.setCurrentIndex(index)

            action.toggled.connect(toggle)
            self.select_menu.addAction(action)
            group.addAction(action)
        self.tool_button.select_button.setMenu(self.select_menu)

    def dragEnterEvent(self, event):
        if event.mimeData().hasText():
            if self.tab_widget.currentIndex() >= self.tabs_number:
                event.acceptProposedAction()
            elif not self.shelves:
                event.acceptProposedAction()

    def create_custom_action(self, text):
        action = {"type": "button", "icon": ":/icons/python", "code": text}
        if self.shelves:
            if self.tab_widget.currentIndex() >= self.tabs_number:
                self.shelves[self.tab_widget.currentIndex() - self.tabs_number]["actions"].append(
                    action
                )
                self.rebuild_custom_tabs()
            else:
                raise Exception("Custom shelf isn't selected")
        else:
            self.create_custom_tab("Custom")
            self.shelves[0]["actions"].append(action)
            self.rebuild_custom_tabs()
            self.tab_widget.setCurrentIndex(self.tabs_number)
        # event.acceptProposedAction() #it makes selected code disappear
        self.save_custom_tabs()

    def dropEvent(self, event):
        text = event.mimeData().text()
        self.create_custom_action(text)

    def add_custom_tab(self, widget, title):
        self.tab_widget.addTab(widget, title)
        self.custom_tabs.append(widget)

    def clear_custom_tabs(self):
        for widget in self.custom_tabs:
            self.tab_widget.removeTab(self.tab_widget.indexOf(widget))
        self.custom_tabs = []

    def rebuild_custom_tabs(self):
        current_index = self.tab_widget.currentIndex()
        self.clear_custom_tabs()
        for shelf_counter, shelf in enumerate(self.shelves):
            toolbar = QtWidgets.QToolBar(shelf["name"], self)
            # toolbar.setToolButtonStyle(QtCore.Qt.ToolButtonTextBesideIcon) #it looks bad and you should feel bad
            for action_counter, action in enumerate(shelf["actions"]):
                button = None
                if action["type"] == "button":
                    button = QtWidgets.QAction(parent=toolbar)
                    if "icon" in action:
                        button.setIcon(QtGui.QIcon(action["icon"]))
                    if "text" in action:
                        button.setText(action["text"])
                    if "toolTip" in action:
                        button.setToolTip(action["toolTip"])
                    button.triggered.connect(partial(lambda code: execute(code), action["code"]))
                    toolbar.addAction(button)
                elif action["type"] == "separator":
                    button = toolbar.addSeparator()
                button.number = action_counter
                toolbar.addAction(button)

            def create_menu(pos, widget, shelf_counter):
                menu = QtWidgets.QMenu()
                action = widget.actionAt(pos)
                if action:
                    menu.addAction(
                        QtGui.QIcon(":/icons/gear"),
                        "Edit Action",
                        lambda shelf_number=shelf_counter, action_number=action.number: self.edit_tabs(
                            shelf_number, action_number
                        ),
                    )
                    menu.addAction(
                        QtGui.QIcon(":/icons/small_garbage"),
                        "Delete Action",
                        lambda shelf_number=shelf_counter, action_number=action.number: self.delete_action(
                            shelf_number, action_number
                        ),
                    )
                else:
                    menu.addAction(
                        QtGui.QIcon(":/icons/gear"),
                        "Edit Toolbar",
                        lambda shelf_counter=shelf_counter: self.edit_tabs(shelf_counter),
                    )
                    menu.addAction(
                        QtGui.QIcon(":/icons/small_garbage"),
                        "Delete Toolbar",
                        lambda shelf_counter=shelf_counter: self.delete_custom_tab(shelf_counter),
                    )
                menu.exec_(widget.mapToGlobal(pos))

            toolbar.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
            toolbar.customContextMenuRequested.connect(
                lambda pos, widget=toolbar, shelf_counter=shelf_counter: create_menu(
                    pos, widget, shelf_counter
                )
            )
            toolbar.number = shelf_counter
            self.add_custom_tab(toolbar, shelf["name"])
        self.tab_widget.setCurrentIndex(current_index)
        self.create_select_menu()
        self.change_orientation(self.orientation())  # seems like a bug in Qt

    def delete_action(self, shelf_number, action_number):
        del self.shelves[shelf_number]["actions"][action_number]
        self.rebuild_custom_tabs()
        self.save_custom_tabs()

    def delete_custom_tab(self, shelf_number):
        msg = QtWidgets.QMessageBox()
        msg.setIcon(QtWidgets.QMessageBox.Warning)
        msg.setText('"%s" shelf will be permanently removed.' % self.shelves[shelf_number]["name"])
        msg.setWindowTitle(i18n("shelf.messagebox", "Shelf Editor"))
        msg.setStandardButtons(QtWidgets.QMessageBox.Ok | QtWidgets.QMessageBox.Cancel)
        retval = msg.exec_()
        if retval == QtWidgets.QMessageBox.Ok:
            os.remove(self.shelves[shelf_number]["file_path"])  # hip hip hooray!
            del self.shelves[shelf_number]
            self.rebuild_custom_tabs()

    def create_custom_tab(self, name):
        path = "%s/shelf_%s.json" % (self.settings_path, str(uuid.uuid4()))
        shelf = {"name": name, "file_path": path, "actions": []}
        self.shelves.append(shelf)
        self.rebuild_custom_tabs()
        self.save_custom_tabs()

    def load_custom_tabs(self):
        paths = glob.glob(self.settings_path + "/shelf_*.json")
        # paths.sort(key=os.path.getctime) # well...
        sort_order = self.settings.value(self.settings_order_key, [])
        sorted_paths = [path for x in sort_order for path in paths if self.get_uuid(path) == x]
        for path in paths:
            if path not in sorted_paths:
                sorted_paths.append(path)
        for path in sorted_paths:
            with open(path) as f:
                shelf = json.load(f)
            if shelf["file_path"] != path:  # in case the file was copied or something
                shelf["file_path"] = path
            self.shelves.append(shelf)

    def get_uuid(self, name):
        match = re.match(r".*shelf_(.+)\.json", name, re.IGNORECASE)
        if match:
            return match.group(1)
        else:
            return ""  # something went very wrong

    def save_custom_tabs(self):
        for shelf in self.shelves:
            if not os.path.exists(os.path.dirname(shelf["file_path"])):
                os.makedirs(os.path.dirname(shelf["file_path"]))
            with open(shelf["file_path"], "w") as f:
                json.dump(shelf, f, indent=4, sort_keys=True)

        # save order
        sort_order = [self.get_uuid(x["file_path"]) for x in self.shelves]
        self.settings.setValue(self.settings_order_key, sort_order)

    def edit_tabs(self, shelf=-1, action=-1):
        dialog = ShelfEditor(shelf, action, copy.deepcopy(self.shelves), self)
        dialog.exec_()
        if dialog.result:
            self.shelves = dialog.result
            self.rebuild_custom_tabs()
            self.save_custom_tabs()


class ShelfEditor(QtWidgets.QDialog):
    """Editor for custom shelves"""

    def __init__(self, shelf, action, shelves, parent=None):
        QtWidgets.QDialog.__init__(self, parent)
        self.setWindowFlags(self.windowFlags() & ~QtCore.Qt.WindowContextHelpButtonHint)
        self.setWindowTitle(i18n("shelf.shelf_editor", "Shelf Editor"))
        self.setObjectName("Shelf Editor")  # hello there
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)

        self.shelf = shelf
        self.action = action
        self.shelves = shelves
        self.result = None

        self.layout = QtWidgets.QVBoxLayout()
        self.layout.setContentsMargins(4, 4, 4, 4)
        self.layout.setSpacing(4)

        header_layout = QtWidgets.QHBoxLayout()

        left_layout = QtWidgets.QVBoxLayout()
        right_layout = QtWidgets.QVBoxLayout()
        header_layout.addLayout(left_layout)
        header_layout.addLayout(right_layout)

        left_toolbar = QtWidgets.QToolBar()
        left_toolbar.setIconSize(QtCore.QSize(16, 16))
        shelves_text = i18n("shelf.shelf_editor", "Shelves")
        left_toolbar.addWidget(QtWidgets.QLabel("<b>&nbsp;%s</b>" % shelves_text))
        left_spacer = QtWidgets.QWidget()
        left_spacer.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Preferred)
        left_toolbar.addWidget(left_spacer)
        left_toolbar.addAction(
            QtGui.QIcon(":/icons/small_up"),
            i18n("shelf.shelf_editor", "Move Shelf Up"),
            lambda: self.move_shelf(-1),
        )
        left_toolbar.addAction(
            QtGui.QIcon(":/icons/small_down"),
            i18n("shelf.shelf_editor", "Move Shelf Down"),
            lambda: self.move_shelf(1),
        )
        left_toolbar.addAction(
            QtGui.QIcon(":/icons/small_plus"),
            i18n("shelf.shelf_editor", "Add Shelf"),
            self.create_tab,
        )
        left_toolbar.addAction(
            QtGui.QIcon(":/icons/small_garbage"),
            i18n("shelf.shelf_editor", "Delete Shelf"),
            self.delete_tab,
        )
        left_layout.addWidget(left_toolbar)

        right_toolbar = QtWidgets.QToolBar()
        right_toolbar.setIconSize(QtCore.QSize(16, 16))
        actions_text = i18n("shelf.shelf_editor", "Actions")
        right_toolbar.addWidget(QtWidgets.QLabel("<b>&nbsp;%s</b>" % actions_text))
        right_spacer = QtWidgets.QWidget()
        right_spacer.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Preferred)
        right_toolbar.addWidget(right_spacer)
        right_toolbar.addAction(
            QtGui.QIcon(":/icons/small_up"),
            i18n("shelf.shelf_editor", "Move Action Up"),
            lambda: self.move_action(-1),
        )
        right_toolbar.addAction(
            QtGui.QIcon(":/icons/small_down"),
            i18n("shelf.shelf_editor", "Move Action Down"),
            lambda: self.move_action(1),
        )
        right_toolbar.addAction(
            QtGui.QIcon(":/icons/small_plus"),
            i18n("shelf.shelf_editor", "Add Action"),
            self.create_action,
        )
        right_toolbar.addAction(
            QtGui.QIcon(":/icons/small_garbage"),
            i18n("shelf.shelf_editor", "Delete Action"),
            self.delete_action,
        )
        right_layout.addWidget(right_toolbar)

        self.left_list = QtWidgets.QListWidget()
        self.left_list.setIconSize(QtCore.QSize(20, 20))
        self.right_list = QtWidgets.QListWidget()
        self.right_list.setIconSize(QtCore.QSize(20, 20))
        left_layout.addWidget(self.left_list)
        right_layout.addWidget(self.right_list)
        self.layout.addLayout(header_layout)

        params_layout = QtWidgets.QGridLayout()
        self.layout.addLayout(params_layout)

        type_label = QtWidgets.QLabel(i18n("shelf.shelf_editor", "Type"))
        type_label.setMinimumWidth(140)
        type_label.setAlignment(QtCore.Qt.AlignRight | QtCore.Qt.AlignVCenter)
        params_layout.addWidget(type_label, 0, 0, QtCore.Qt.AlignRight)
        self.action_type = QtWidgets.QComboBox()
        self.action_type.addItem(i18n("shelf.shelf_editor", "Button"))
        self.action_type.addItem(i18n("shelf.shelf_editor", "Separator"))
        params_layout.addWidget(self.action_type, 0, 1)

        params_layout.addWidget(QtWidgets.QLabel("Icon"), 1, 0, QtCore.Qt.AlignRight)
        self.action_icon = QtWidgets.QComboBox()
        self.action_icon.setEditable(True)
        self.action_icon.addItem(QtGui.QIcon(":/icons/no_icon"), i18n("shelf.shelf_editor", "None"))
        self.action_icon.addItem(QtGui.QIcon(":/icons/python"), ":/icons/python")
        self.action_icon.addItem(QtGui.QIcon(":/icons/python_color"), ":/icons/python_color")
        self.action_icon.addItem(QtGui.QIcon(":/icons/dcc"), ":/icons/dcc")
        self.action_icon.addItem(QtGui.QIcon(":/icons/default"), ":/icons/default")
        self.action_icon.addItem(QtGui.QIcon(":/icons/layers"), ":/icons/layers")
        self.action_icon.addItem(QtGui.QIcon(":/icons/scope"), ":/icons/scope")
        self.action_icon.addItem(QtGui.QIcon(":/icons/withouttype"), ":/icons/withouttype")
        self.action_icon.addItem(QtGui.QIcon(":/icons/package"), ":/icons/package")
        self.action_icon.addItem(QtGui.QIcon(":/icons/component"), ":/icons/component")
        self.action_icon.addItem(QtGui.QIcon(":/icons/send_to_farm"), ":/icons/send_to_farm")
        self.action_icon.addItem(QtGui.QIcon(":/icons/garbage"), ":/icons/garbage")

        self.action_icon.setCurrentIndex(1)
        params_layout.addWidget(self.action_icon, 1, 1)

        params_layout.addWidget(
            QtWidgets.QLabel(i18n("shelf.shelf_editor", "Text")), 2, 0, QtCore.Qt.AlignRight
        )
        self.action_text = QtWidgets.QLineEdit()
        params_layout.addWidget(self.action_text, 2, 1)

        params_layout.addWidget(
            QtWidgets.QLabel(i18n("shelf.shelf_editor", "Tool Tip")), 3, 0, QtCore.Qt.AlignRight
        )
        self.action_tooltip = QtWidgets.QLineEdit()
        params_layout.addWidget(self.action_tooltip, 3, 1)

        self.code_edit = CodeEditor(self, lang="python", highlight_line=True, line_numbers=True)
        self.code_edit.code_editor.setReadOnly(True)
        self.layout.addWidget(self.code_edit)

        buttons_layout = QtWidgets.QHBoxLayout()
        save_button = QtWidgets.QPushButton(i18n("shelf.shelf_editor", "Save"))
        cancel_button = QtWidgets.QPushButton(i18n("shelf.shelf_editor", "Cancel"))
        buttons_layout.addStretch()
        buttons_layout.addWidget(save_button)
        buttons_layout.addWidget(cancel_button)
        self.layout.addLayout(buttons_layout)

        self.setLayout(self.layout)
        self.setStyleSheet("QToolBar {background: palette(light);}")
        self.fill_left()

        self.left_list.itemSelectionChanged.connect(self.fill_right)
        self.right_list.itemSelectionChanged.connect(self.fill_params)

        self.action_type.currentTextChanged.connect(self.param_edited)
        self.action_icon.currentTextChanged.connect(self.param_edited)
        self.action_text.editingFinished.connect(self.param_edited)
        self.action_tooltip.editingFinished.connect(self.param_edited)
        self.code_edit.code_editor.textChanged.connect(self.param_edited)

        cancel_button.clicked.connect(self.close)
        save_button.clicked.connect(self.save)

        self.left_list.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.left_list.customContextMenuRequested.connect(self.left_menu)
        self.right_list.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.right_list.customContextMenuRequested.connect(self.right_menu)

        self.left_list.itemChanged.connect(self.left_name_changed)
        self.right_list.itemChanged.connect(self.right_name_changed)

        if shelf >= 0:
            self.left_list.setCurrentRow(shelf)
        if action >= 0:
            self.right_list.setCurrentRow(action)

    def save(self):
        self.result = self.shelves
        self.close()

    def left_menu(self, pos):
        selected = self.left_list.selectedItems()
        menu = QtWidgets.QMenu()
        menu.addAction(
            QtGui.QIcon(":/icons/small_plus"),
            i18n("shelf.shelf_editor", "Add Shelf"),
            self.create_tab,
        )
        if selected:
            menu.addAction(
                QtGui.QIcon(":/icons/small_garbage"),
                i18n("shelf.shelf_editor", "Delete Shelf"),
                self.delete_tab,
            )
            menu.addSeparator()
            menu.addAction(
                QtGui.QIcon(":/icons/small_up"),
                i18n("shelf.shelf_editor", "Move Up"),
                lambda: self.move_shelf(-1),
            )
            menu.addAction(
                QtGui.QIcon(":/icons/small_down"),
                i18n("shelf.shelf_editor", "Move Down"),
                lambda: self.move_shelf(1),
            )
        menu.exec_(self.left_list.mapToGlobal(pos))

    def right_menu(self, pos):
        selected = self.right_list.selectedItems()
        menu = QtWidgets.QMenu()
        menu.addAction(
            QtGui.QIcon(":/icons/small_plus"),
            i18n("shelf.shelf_editor", "Add Action"),
            self.create_action,
        )
        if selected:
            menu.addAction(
                QtGui.QIcon(":/icons/small_garbage"),
                i18n("shelf.shelf_editor", "Delete Action"),
                self.delete_action,
            )
            menu.addSeparator()
            menu.addAction(
                QtGui.QIcon(":/icons/small_up"),
                i18n("shelf.shelf_editor", "Move Up"),
                lambda: self.move_action(-1),
            )
            menu.addAction(
                QtGui.QIcon(":/icons/small_down"),
                i18n("shelf.shelf_editor", "Move Down"),
                lambda: self.move_action(1),
            )
        menu.exec_(self.right_list.mapToGlobal(pos))

    def left_name_changed(self):
        item = self.left_list.currentItem()
        self.shelves[item.number]["name"] = item.text()

    def right_name_changed(self):
        item = self.right_list.currentItem()
        self.action_text.setText(item.text())
        self.param_edited()

    def create_action(self):
        selected = self.left_list.selectedItems()
        if selected:
            action = {"type": "button", "icon": ":/icons/python", "code": ""}
            self.shelves[selected[0].number]["actions"].append(action)
            self.fill_right()
            self.right_list.setCurrentRow(self.right_list.count() - 1)

    def delete_action(self):
        selected = self.right_list.selectedItems()
        if selected:
            del self.shelves[selected[0].shelf]["actions"][selected[0].number]
            self.fill_right()

    def move_shelf(self, direction):
        selected = self.left_list.selectedItems()
        if selected:
            pos = selected[0].number
            next_pos = pos + direction
            if next_pos >= 0 and next_pos < self.left_list.count():
                self.shelves[pos], self.shelves[next_pos] = (
                    self.shelves[next_pos],
                    self.shelves[pos],
                )
                self.fill_left()
                self.left_list.setCurrentRow(next_pos)

    def move_action(self, direction):
        selected = self.right_list.selectedItems()
        if selected:
            pos = selected[0].number
            next_pos = pos + direction
            if next_pos >= 0 and next_pos < self.right_list.count():
                actions = self.shelves[selected[0].shelf]["actions"]
                actions[pos], actions[next_pos] = actions[next_pos], actions[pos]
                self.fill_right()
                self.right_list.setCurrentRow(next_pos)

    def create_tab(self):
        name, ok = QtWidgets.QInputDialog.getText(
            self,
            i18n("shelf.shelf_editor", "Create Tab"),
            i18n("shelf.shelf_editor", "Enter name:"),
            QtWidgets.QLineEdit.Normal,
            i18n("shelf.shelf_editor", "Custom"),
        )
        if ok:
            self.parent().create_custom_tab(name)
            self.shelves = copy.deepcopy(self.parent().shelves)
            self.fill_left()

    def delete_tab(self):
        selected = self.left_list.selectedItems()
        if selected:
            self.parent().delete_custom_tab(selected[0].number)
            self.shelves = copy.deepcopy(self.parent().shelves)
            self.fill_left()

    def param_edited(self):
        selected = self.right_list.selectedItems()
        if selected:
            action = {}
            action["type"] = self.action_type.currentText().lower()
            if self.action_icon.currentIndex() != 0:
                action["icon"] = self.action_icon.currentText()
            if self.action_text.text():
                action["text"] = self.action_text.text()
            if self.action_tooltip.text():
                action["toolTip"] = self.action_tooltip.text()
            action["code"] = self.code_edit.code_editor.toPlainText()
            self.decorate_item(selected[0], action)
            self.shelves[selected[0].shelf]["actions"][selected[0].number] = action

    def clear(self):
        self.left_list.clear()
        self.right_list.clear()

    def fill_left(self):
        self.clear()
        for shelf_counter, shelf in enumerate(self.shelves):
            item = QtWidgets.QListWidgetItem(QtGui.QIcon(":/icons/shelf"), shelf["name"])
            item.setFlags(item.flags() | QtCore.Qt.ItemIsEditable)
            item.number = shelf_counter
            self.left_list.addItem(item)

    def fill_right(self):
        self.right_list.clear()
        selected = self.left_list.selectedItems()
        if selected:
            for action_counter, action in enumerate(self.shelves[selected[0].number]["actions"]):
                item = QtWidgets.QListWidgetItem()
                item.setFlags(item.flags() | QtCore.Qt.ItemIsEditable)
                self.decorate_item(item, action)
                item.number = action_counter
                item.shelf = selected[0].number
                self.right_list.addItem(item)

    def block_params(self, value):
        self.action_type.blockSignals(value)
        self.action_icon.blockSignals(value)
        self.action_text.blockSignals(value)
        self.action_tooltip.blockSignals(value)
        self.code_edit.code_editor.blockSignals(value)

    def clear_params(self):
        self.block_params(True)
        self.action_type.setCurrentIndex(0)
        self.action_icon.setCurrentIndex(1)
        self.action_text.setText("")
        self.action_tooltip.setText("")
        self.code_edit.code_editor.setPlainText("")
        self.block_params(False)

    def fill_params(self):
        self.clear_params()
        selected = self.right_list.selectedItems()
        if selected:
            self.code_edit.code_editor.setReadOnly(False)
            action = self.shelves[selected[0].shelf]["actions"][selected[0].number]
            self.block_params(True)
            if action["type"] == "separator":
                self.action_type.setCurrentIndex(1)
            else:
                self.action_type.setCurrentIndex(0)
            if "icon" in action:
                index = self.action_icon.findData(action["icon"], QtCore.Qt.MatchExactly)
                if index != -1:
                    self.action_icon.setCurrentIndex(index)
                else:
                    self.action_icon.addItem(QtGui.QIcon(action["icon"]), action["icon"])
                    self.action_icon.setCurrentIndex(self.action_icon.count() - 1)
            else:
                self.action_icon.setCurrentIndex(0)
            if "text" in action:
                self.action_text.setText(action["text"])
            if "toolTip" in action:
                self.action_tooltip.setText(action["toolTip"])
            if "code" in action:
                self.code_edit.code_editor.setPlainText(action["code"])
            self.block_params(False)
        else:
            self.code_edit.code_editor.setReadOnly(True)

    def decorate_item(self, item, action):
        icon = ":/icons/no_icon"
        text = i18n("shelf.shelf_editor", "[Unnamed]")
        pal = QtWidgets.QApplication.palette()
        self.right_list.blockSignals(True)
        item.setBackground(pal.color(QtGui.QPalette.Base))
        item.setForeground(pal.color(QtGui.QPalette.WindowText))

        if action["type"] == "separator":
            icon = ":/icons/separator"
            text = i18n("shelf.shelf_editor", "Separator")
            item.setBackground(QtGui.QColor("#373737"))
        else:
            if "icon" in action:
                icon = action["icon"]
            else:
                icon = ":/icons/no_icon"
            if "text" in action:
                text = action["text"]
            else:
                item.setForeground(QtGui.QColor("#4d9fc7"))
        item.setText(text)
        item.setIcon(QtGui.QIcon(icon))
        self.right_list.blockSignals(False)
