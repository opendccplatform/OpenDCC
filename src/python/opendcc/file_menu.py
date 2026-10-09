# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui

from . import stage_utils
import opendcc.core as dcc_core
from opendcc import cmds
from functools import partial
import os
from pxr import Sdf, UsdUtils, Tf

from .i18n import i18n


import six

recent_files_key = "recent_files"

### When we call QSettings.value() on a simple string it returns plain string.
### Calling it with QSettings.value(key, type=list) returns a list only if a contained
### value is not the list itself (in this case it returns []).
### We use this logic to ensure that the stored value is a list or a string
### and return this value as a list of strings or None if value is invalid.
def get_recent_files():
    app = dcc_core.Application.instance()
    settings = app.get_ui_settings()
    recent_files = settings.value(recent_files_key)
    if not recent_files:
        return []
    if type(recent_files) == list:
        return recent_files
    if type(recent_files) == six.text_type:
        return [recent_files]
    return None


def get_open_dir():
    directory = ""
    recent_files = get_recent_files()
    if recent_files:
        directory = os.path.dirname(recent_files[0])
    return directory


def add_recent_file(scene_path):
    app = dcc_core.Application.instance()
    settings = app.get_ui_settings()
    recent_files = get_recent_files()
    if scene_path in recent_files:
        recent_files.remove(scene_path)
    recent_files = recent_files[:6]
    recent_files.insert(0, scene_path)
    settings.setValue(recent_files_key, recent_files)


def on_open_stage_action(action_name, close_current_stage=False):
    app = dcc_core.Application.instance()

    scene_path, _ = QtWidgets.QFileDialog.getOpenFileName(
        app.get_main_window(),
        action_name,
        get_open_dir(),
        get_extensions(),
    )
    if scene_path:
        if close_current_stage:
            on_close_current_stage_action()
        add_recent_file(scene_path)
        stage_utils.open_stage(scene_path)


def get_extensions():
    extensions = Sdf.FileFormat.FindAllFileFormatExtensions()
    if "sdf" in extensions:
        extensions.remove("sdf")
    extensions = sorted(extensions, key=lambda x: "" if x.startswith("usd") else x)

    all_file_extensions = []
    for extension in extensions:
        try:
            file_format = Sdf.FileFormat.FindByExtension(extension)
            file_extensions = file_format.GetFileExtensions()
            all_file_extensions = all_file_extensions + file_extensions
        except:
            Tf.Warn('Failed to get file extensions for "{}" file format.'.format(extension))
    all_file_extensions = ["*." + x for x in all_file_extensions]
    result = "*.usd *.usda *.usdc *.usdz;;" + ";;".join(all_file_extensions)
    return result


def on_save(stage, force_save_as=False):
    if not stage:
        return False

    layer = stage.GetEditTarget().GetLayer()

    app = dcc_core.Application.instance()
    session = app.get_session()
    if layer.anonymous or force_save_as:
        caption = (
            "Save Current Layer (%s)" % layer.identifier
            if session.get_current_stage() == stage
            else "Save Layer"
        )
        scene_path, _ = QtWidgets.QFileDialog.getSaveFileName(
            app.get_main_window(),
            caption,
            layer.realPath,
            get_extensions(),
        )

        if not scene_path:
            return False

        if layer == stage.GetRootLayer():
            old_suffix = layer.GetFileFormat().formatId.lower()
            new_suffix = scene_path.split(".")[-1].lower()
            if old_suffix != new_suffix:
                new_layer = Sdf.Layer.FindOrOpen(scene_path)
                if new_layer:
                    new_layer.Clear()
                else:
                    new_layer = Sdf.Layer.CreateNew(scene_path)
                new_layer.TransferContent(layer)
                new_layer.Save()
                add_recent_file(new_layer.identifier)
                stage_utils.open_stage(new_layer.identifier)
                return True
        else:
            dialog = QtWidgets.QMessageBox()
            dialog.setIcon(QtWidgets.QMessageBox.Critical)
            dialog.setModal(True)
            dialog.setWindowTitle("Error")
            dialog.setText('"Save As" for non-root layers isn\'t currently supported.')
            dialog.exec_()
            return False

        layer.identifier = scene_path
        layer.Save()
        add_recent_file(layer.identifier)
        # force ui update
        session.force_update_stage_list()
    else:
        layer.Save()
        add_recent_file(layer.identifier)

    return True


def save_stage_if_needed(stage):
    if not stage:
        return False

    app = dcc_core.Application.instance()
    session = app.get_session()
    layer = stage.GetEditTarget().GetLayer()
    session_layer = (
        stage.GetSessionLayer()
    )  # TODO we check now only root session layer without session sublayers
    caption = (
        "Save Current Layer (%s)" % layer.identifier
        if session.get_current_stage() == stage
        else "Save Layer"
    )
    result = QtWidgets.QMessageBox.No
    is_session_layer = layer == session_layer
    if (layer.anonymous and not is_session_layer) or (layer.dirty and not is_session_layer):
        result = QtWidgets.QMessageBox.question(
            app.get_main_window(),
            caption,
            "Do you want to save '{:s}'?".format(layer.identifier),
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No | QtWidgets.QMessageBox.Cancel,
            QtWidgets.QMessageBox.Yes,
        )

    if result == QtWidgets.QMessageBox.Yes and not on_save(stage):
        return False
    return result != QtWidgets.QMessageBox.Cancel


def on_save_current_layer_action():
    app = dcc_core.Application.instance()
    session = app.get_session()
    on_save(session.get_current_stage())


def on_save_current_layer_as_action():
    app = dcc_core.Application.instance()
    session = app.get_session()
    on_save(session.get_current_stage(), force_save_as=True)


def on_export_all_action():
    app = dcc_core.Application.instance()
    session = app.get_session()
    stage = session.get_current_stage()
    if stage:
        export_path, _ = QtWidgets.QFileDialog.getSaveFileName(
            app.get_main_window(),
            "Export Stage",
            "",
            get_extensions(),
        )
        if not export_path:
            return
        stage.Export(export_path)


# def on_export_selection_action():
#     app =  dcc_core.Application.instance()
#     session = app.get_session()
#     stage = session.get_current_stage()
#     if stage:
#         print "export selection"


def on_reload_current_stage_action():
    app = dcc_core.Application.instance()
    session = app.get_session()
    stage = session.get_current_stage()
    if stage:
        stage_utils.reload_stage(stage)


def on_close_current_stage_action():
    app = dcc_core.Application.instance()
    session = app.get_session()
    stage = session.get_current_stage()
    if not save_stage_if_needed(stage):
        return
    session.close_stage(stage)


def on_close_all_stages_action():
    app = dcc_core.Application.instance()
    session = app.get_session()
    for stage in session.get_stage_list():
        if not save_stage_if_needed(stage):
            return
        session.close_stage(stage)


def open_recent_file(scene_path):
    on_close_current_stage_action()
    add_recent_file(scene_path)
    stage_utils.open_stage(scene_path)


from opendcc.actions.custom_widget_action import CustomAction, fix_menu
from opendcc.actions.options_widget import OptionsWidget, make_option


session_layer_option = (
    i18n("main_menu.new_stage", "Default Edit Target"),
    "new_stage_command.default_edit_target",
    {
        "type": "option",
        "options": [["Root Layer", "root_layer"], ["Session Layer", "session_layer"]],
    },
    "root_layer",
)


create_session_layer_option = (
    i18n("main_menu.new_stage", "Create Session Layer"),
    "new_stage_command.create_session_layer",
    {"type": "bool"},
    True,
)


class NewStageAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("main_menu.file", "New Stage"), parent)
        shortcut = QtGui.QKeySequence("Ctrl+N")
        self.setShortcut(shortcut, self.do)
        self.on_checkbox_clicked(self.show_options)
        self.custom_triggered.connect(self.do)

        options = []
        options.append(
            make_option(
                i18n("main_menu.new_stage", "Default Up Axis"),
                "new_stage_command.default_up_axis",
                {"type": "option", "options": ["Y", "Z"], "enable": True},
                "Y",
            )
        )
        options.append(
            make_option(
                i18n("main_menu.new_stage", "Default Animation Rate"),
                "new_stage_command.default_animation_rate",
                {"type": "float", "enable": False},
                24.0,
            )
        )
        options.append(
            make_option(
                i18n("main_menu.new_stage", "Default Meters Per Unit"),
                "new_stage_command.default_meters_per_unit",
                {"type": "float", "enable": False},
                0.01,
            )
        )
        options.append(
            make_option(
                i18n("main_menu.new_stage", "Default Frame Range"),
                "new_stage_command.default_frame_range",
                {"type": "float2", "enable": True},
                [1.0, 100.0],
            )
        )
        options.append(
            make_option(
                i18n("main_menu.new_stage", "Default DefaultPrim Name"),
                "new_stage_command.default_prim",
                {"type": "string", "enable": False},
                "world",
            )
        )

        options.append(make_option(None, None, {"type": "spacer"}, None))
        options.append(make_option(*session_layer_option))
        options.append(make_option(*create_session_layer_option))
        options.append(make_option(None, None, {"type": "spacer"}, None))

        self.options_widget = OptionsWidget(
            i18n("main_menu.new_stage", "New Stage"),
            i18n("main_menu.new_stage", "New Stage"),
            self.do,
            options,
            self.parent(),
        )

    def do(self):
        stage_utils.new_stage()

    def show_options(self):
        self.options_widget.show()


open_stage_options = []
open_stage_options.append(
    make_option(
        i18n("main_menu.new_stage", "Load Operation"),
        "open_stage_command.load_operation",
        {
            "type": "option",
            "options": [["Load All", "load_all"], ["Load None", "load_none"]],
        },
        "load_all",
    )
)

open_stage_options.append(make_option(*session_layer_option))
open_stage_options.append(make_option(*create_session_layer_option))


class OpenStageAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("main_menu.file", "Open Stage"), parent)
        shortcut = QtGui.QKeySequence("Ctrl+O")
        self.setShortcut(shortcut, self.do)
        self.on_checkbox_clicked(self.show_options)
        self.custom_triggered.connect(self.do)

        self.options_widget = OptionsWidget(
            i18n("main_menu.open_stage", "Open Stage"),
            i18n("main_menu.open_stage", "Open Stage"),
            self.do,
            open_stage_options,
            self.parent(),
        )

    def do(self):
        on_open_stage_action(i18n("main_menu.open_stage", "Open Stage"), True)

    def show_options(self):
        self.options_widget.show()


class AddStageAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("main_menu.file", "Add Stage"), parent)
        shortcut = QtGui.QKeySequence("Ctrl+Shift+O")  # maybe a bad idea
        self.setShortcut(shortcut, self.do)
        self.on_checkbox_clicked(self.show_options)
        self.custom_triggered.connect(self.do)

        self.options_widget = OptionsWidget(
            i18n("main_menu.add_stage", "Add Stage"),
            i18n("main_menu.add_stage", "Add Stage"),
            self.do,
            open_stage_options,
            self.parent(),
        )

    def do(self):
        on_open_stage_action(i18n("main_menu.add_stage", "Add Stage"))

    def show_options(self):
        self.options_widget.show()


class ExportUSDZAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("main_menu.file", "Export USDZ"), parent)
        self.on_checkbox_clicked(self.show_options)
        self.custom_triggered.connect(self.do)

        options = []
        options.append(
            make_option(
                "ARKit",
                "export_usdz.arkit",
                {"type": "bool"},
                False,
            )
        )

        self.options_widget = OptionsWidget(
            i18n("main_menu.export_usdz", "Export USDZ"),
            i18n("main_menu.export_usdz", "Export USDZ"),
            self.do,
            options,
            self.parent(),
        )

    def do(self):
        app = dcc_core.Application.instance()
        path, _ = QtWidgets.QFileDialog.getSaveFileName(
            app.get_main_window(),
            i18n("main_menu.export_usdz", "Export USDZ"),
            get_open_dir(),
            "*.usdz",
        )

        if path:
            settings = dcc_core.Application.instance().get_settings()
            arkit = settings.get_bool("export_usdz.arkit", False)

            session = app.get_session()
            stage = session.get_current_stage()
            layer = stage.GetRootLayer()

            if not layer.anonymous:
                if not arkit:
                    UsdUtils.CreateNewUsdzPackage(layer.identifier, path)
                else:
                    UsdUtils.CreateNewARKitUsdzPackage(layer.identifier, path)

    def show_options(self):
        self.options_widget.show()


class ExportSelectionAction(CustomAction):
    def __init__(self, parent=None):
        CustomAction.__init__(self, i18n("main_menu.file", "Export Selection"), parent)
        self.on_checkbox_clicked(self.show_options)
        self.custom_triggered.connect(self.do)

        options = []
        export_selection_mode = make_option(
            i18n("main_menu.export_selection", "Mode"),
            "export_selection_command.mode",
            {"type": "option", "options": ["Edit Target", "Collapsed"]},
            "Edit Target",
        )

        export_parents_option = make_option(
            i18n("main_menu.export_selection", "Export Parents"),
            "export_selection_command.export_parents",
            {"type": "bool"},
            True,
        )

        export_root_option = make_option(
            i18n("main_menu.export_selection", "Export Root"),
            "export_selection_command.export_root",
            {"type": "bool"},
            True,
        )

        export_connections_option = make_option(
            i18n("main_menu.export_selection", "Export Connections"),
            "export_selection_command.export_connections",
            {"type": "bool"},
            True,
        )

        options.append(export_selection_mode)
        options.append(export_parents_option)
        options.append(export_root_option)
        options.append(export_connections_option)

        self.options_widget = OptionsWidget(
            i18n("main_menu.export_selection", "Export Selection"),
            i18n("main_menu.export_selection", "Export Selection"),
            self.do,
            options,
            self.parent(),
        )

    def do(self):
        app = dcc_core.Application.instance()

        scene_path, _ = QtWidgets.QFileDialog.getSaveFileName(
            app.get_main_window(),
            "Export Selection",
            get_open_dir(),
            get_extensions(),
        )

        if scene_path:
            settings = dcc_core.Application.instance().get_settings()
            export_selection_mode = settings.get_string(
                "export_selection_command.mode", "Edit Target"
            )

            export_parents_option = settings.get_bool(
                "export_selection_command.export_parents", True
            )

            export_root_option = settings.get_bool("export_selection_command.export_root", True)

            export_connections_option = settings.get_bool(
                "export_selection_command.export_connections", True
            )

            if export_selection_mode == "Collapsed":
                collapsed_option = True
            else:
                collapsed_option = False

            result = cmds.export_selection(
                scene_path,
                collapsed=collapsed_option,
                export_parents=export_parents_option,
                export_root=export_root_option,
                export_connections=export_connections_option,
            )

            if result.is_successful():
                add_recent_file(scene_path)

    def show_options(self):
        self.options_widget.show()


def fill_file_menu(menu):
    fix_menu(menu)

    new_stage_action = NewStageAction(menu)
    new_stage_action.setObjectName("new_stage_action")
    menu.addAction(new_stage_action)

    open_stage_action = OpenStageAction(menu)
    open_stage_action.setObjectName("open_stage_action")
    menu.addAction(open_stage_action)

    def build_recent_files_menu():
        recent_files_menu.clear()
        recent_files = get_recent_files()
        if recent_files:
            for recent_file in recent_files:
                action = QtWidgets.QAction(QtGui.QIcon(":/icons/usd_small"), recent_file, menu)
                action.triggered.connect(
                    partial(lambda scene_path: open_recent_file(scene_path), recent_file)
                )
                recent_files_menu.addAction(action)
        else:
            action = QtWidgets.QAction("Empty", menu)
            action.setEnabled(False)
            recent_files_menu.addAction(action)

    recent_files_menu = QtWidgets.QMenu(i18n("main_menu.file", "Recent Files"), menu)
    recent_files_menu.setObjectName("recent_files_menu")
    menu.addMenu(recent_files_menu)
    recent_files_menu.aboutToShow.connect(build_recent_files_menu)

    add_stage_to_session_action = AddStageAction(menu)
    add_stage_to_session_action.setObjectName("add_stage_to_session_action")
    menu.addAction(add_stage_to_session_action)

    menu.addSeparator()

    save_current_layer_action = QtWidgets.QAction(
        i18n("main_menu.file", "Save Current Layer"), menu
    )
    save_current_layer_action.setObjectName("save_current_layer_action")
    save_current_layer_action.setShortcut(QtGui.QKeySequence("Ctrl+S"))
    save_current_layer_action.triggered.connect(on_save_current_layer_action)
    menu.addAction(save_current_layer_action)

    save_current_layer_as_action = QtWidgets.QAction(
        i18n("main_menu.file", "Save Current Layer As"), menu
    )
    save_current_layer_as_action.setObjectName("save_current_layer_as_action")
    save_current_layer_as_action.setShortcut(QtGui.QKeySequence("Ctrl+Shift+S"))
    save_current_layer_as_action.triggered.connect(on_save_current_layer_as_action)
    menu.addAction(save_current_layer_as_action)

    menu.addSeparator()

    export_all_action = QtWidgets.QAction(i18n("main_menu.file", "Export All"), menu)
    export_all_action.setObjectName("export_all_action")
    export_all_action.triggered.connect(on_export_all_action)
    menu.addAction(export_all_action)

    export_selection_action = ExportSelectionAction(menu)
    export_selection_action.setObjectName("export_selection_action")
    menu.addAction(export_selection_action)

    # export_selection_action = QtWidgets.QAction("Export Selection", menu)
    # export_selection_action.triggered.connect(on_export_selection_action)
    # menu.addAction(export_selection_action)

    export_usdz_action = ExportUSDZAction(menu)
    export_usdz_action.setObjectName("export_usdz_action")
    menu.addAction(export_usdz_action)

    menu.addSeparator()

    reload_current_stage_action = QtWidgets.QAction(
        i18n("main_menu.file", "Reload Current Stage"), menu
    )
    reload_current_stage_action.setObjectName("reload_current_stage_action")
    reload_current_stage_action.triggered.connect(on_reload_current_stage_action)
    menu.addAction(reload_current_stage_action)

    menu.addSeparator()

    close_current_stage_action = QtWidgets.QAction(
        i18n("main_menu.file", "Close Current Stage"), menu
    )
    close_current_stage_action.setObjectName("close_current_stage_action")
    close_current_stage_action.triggered.connect(on_close_current_stage_action)
    menu.addAction(close_current_stage_action)

    close_all_stages_action = QtWidgets.QAction(i18n("main_menu.file", "Close All Stages"), menu)
    close_all_stages_action.setObjectName("close_all_stages_action")
    close_all_stages_action.triggered.connect(on_close_all_stages_action)
    menu.addAction(close_all_stages_action)

    menu.addSeparator()
    quit_action = menu.addAction(
        i18n("main_menu.file", "Quit"),
        lambda: dcc_core.Application.instance().get_main_window().close(),
    )
    quit_action.setObjectName("quit_action")
    quit_action.setShortcut(QtGui.QKeySequence("Ctrl+Q"))
