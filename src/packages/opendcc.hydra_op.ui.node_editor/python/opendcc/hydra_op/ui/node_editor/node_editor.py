# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
from opendcc import cmds
import opendcc.core as dcc_core
from opendcc.usd_editor import material_editor
from . import hydra_op_node_editor
from opendcc.usd_editor.usd_node_editor.palette_widget import (
    CLASSIC_COLORS,
    PASTEL_COLORS,
    COLOR_NAME_BLACK,
    COLOR_NAME_GRAY,
)
from opendcc.usd_editor.material_editor.material_editor_widget import (
    on_material_context_menu,
    init_material_menu,
)
from opendcc.usd_editor.usd_node_editor.node_editor_preform import (
    EntryScreen,
    CommonNodeEditorWidgetPreform,
)
from opendcc.i18n import i18n
from opendcc import rendersystem
from pxr import Usd, Tf, UsdShade, Sdr
import opendcc.usd_fallback_proxy as ufp


def open_render_view():
    import os, subprocess

    app_root = dcc_core.Application.instance().get_application_root_path()
    RENDER_VIEW_PATH = os.path.join(app_root, "bin", "render_view")
    subprocess.Popen(RENDER_VIEW_PATH)


def get_hydra_op_render():
    controls = rendersystem.RenderControlHub.instance().get_controls()
    return controls.get("hydra_op")


def start_render(render_type):
    render_control = get_hydra_op_render()
    if not render_control:
        return

    if dcc_core.Application.instance().get_settings().get_bool("image_view.standalone", True):
        open_render_view()

    status = render_control.render_status()
    if (
        status == rendersystem.RenderStatus.IN_PROGRESS
        or status == rendersystem.RenderStatus.RENDERING
    ):
        render_control.stop_render()
    render_control.init_render(render_type)
    render_control.start_render()
    if render_type == rendersystem.RenderMethod.DISK:
        render_control.wait_render()


def debug_output():
    render_control = get_hydra_op_render()
    if not render_control:
        return

    import tempfile

    tmp_dir = tempfile.TemporaryDirectory(prefix="hydra_op_dump_")
    print("Trying to dump hydra_op graph to {}".format(tmp_dir.name))

    status = render_control.render_status()
    if (
        status == rendersystem.RenderStatus.IN_PROGRESS
        or status == rendersystem.RenderStatus.RENDERING
    ):
        render_control.stop_render()
    render_control.init_render(rendersystem.RenderMethod.DISK)
    render_control.dump(tmp_dir.name)


class HydraOpNodeEditorEntryScreen(EntryScreen):
    def _on_create_new(self):
        res = cmds.create_prim("HydraOpNodegraph", "HydraOpNodegraph")
        if res.is_successful():
            self.model.set_root(res.get_result())
            self.close()

    def _on_open_selected(self):
        selection = dcc_core.Application.instance().get_prim_selection()
        stage = dcc_core.Application.instance().get_session().get_current_stage()
        if not stage or len(selection) == 0:
            return

        prim = stage.GetPrimAtPath(selection[0])
        if prim.IsPseudoRoot():
            candidates = [prim]
        else:
            candidates = [prim, prim.GetParent()]

        for cur in candidates:
            if cur.GetTypeName() == "HydraOpNodegraph":
                self.model.set_root(cur.GetPath())
                self.close()
                return

    def set_widgets_text(self):
        self.create_new_btn.setText(
            i18n("hydra_op_node_editor.entry_screen", "Create New Nodegraph")
        )
        self.label.setText(
            i18n(
                "hydra_op_node_editor.entry_screen",
                "Create a new HydraOp Nodegraph or select existing to get started.",
            )
        )


class HydraOpNodeEditorWidget(CommonNodeEditorWidgetPreform):
    is_material_mode = False

    def __init__(self, editor_name, model, view, scene, editor, item_registry, parent=None):
        CommonNodeEditorWidgetPreform.__init__(
            self, editor_name, model, view, scene, editor, item_registry, parent
        )
        self.terminal_node = ""
        self.navigation_bar.set_path(self.model.get_root())
        self.__on_terminal_node_changed(self.model.get_terminal_node())
        self.view.fit_to_view()
        self.terminal_node = ""
        self.bypass_action = QtWidgets.QAction(
            i18n("hydra_op_node_editor.menu.edit", "Toggle Node Bypass")
        )
        self.bypass_action.setCheckable(False)
        shortcut = QtGui.QKeySequence(QtCore.Qt.Key_D)
        self.bypass_action.setShortcut(shortcut)
        self.bypass_action.triggered.connect(self.__bypass_selected_node)
        self.edit_menu.addAction(self.bypass_action)
        self.model.terminal_node_changed.connect(self.__on_terminal_node_changed)
        self.setProperty("unfocusedKeyEvent_enable", True)

        palette_menu = self.menu_bar.addMenu(i18n("hydra_op_node_editor.menu", "Colors"))
        self.init_colors_menu(palette_menu)

        if self.model.get_root().isEmpty:
            self._pop_entry_screen()
        else:
            self.navigation_bar.set_path(self.model.get_root())
            self.__on_terminal_node_changed(self.model.get_terminal_node())
            self.view.fit_to_view()

    def keyPressEvent(self, event):
        """Overloaded function for hotkeys"""
        if event.key() == QtCore.Qt.Key_C and event.modifiers() == QtCore.Qt.ControlModifier:
            cmds.copy_prims(dcc_core.Application.instance().get_prim_selection())
        elif event.key() == QtCore.Qt.Key_X and event.modifiers() == QtCore.Qt.ControlModifier:
            cmds.cut_prims(dcc_core.Application.instance().get_prim_selection())
        elif event.key() == QtCore.Qt.Key_V and event.modifiers() == QtCore.Qt.ControlModifier:
            clipboard_stage = dcc_core.Application.get_usd_clipboard().get_clipboard_stage()
            if clipboard_stage is None:
                return
            for prim in clipboard_stage.GetPseudoRoot().GetAllChildren():
                if not self.model.is_supported_prim_type(prim):
                    # TODO: add logging about an paste error
                    return
            cmds.paste_prims(self.model.get_root())
        else:
            QtWidgets.QWidget.keyPressEvent(self, event)

    def _on_node_double_clicked(self, node_id):
        is_material = self.model.get_prim_for_node(node_id).IsA(UsdShade.Material)
        if self.model.can_fall_through(node_id) or is_material:
            if is_material:
                self.__pop_to_material(node_id)
            else:
                self.model.set_root(node_id)

    def __update_model_connections(self):
        self.model.model_reset.connect(self._on_model_reset)
        self.model.node_created.connect(self.scene.add_node_item)
        self.model.node_removed.connect(self.scene.remove_node_item)
        self.model.connection_created.connect(self.scene.add_connection_item)
        self.model.connection_removed.connect(self.scene.remove_connection_item)
        self.model.selection_changed.connect(self.scene.set_selection)
        self.model.port_updated.connect(self.scene.update_port)

        self.scene.selection_changed.connect(self.model.on_selection_set)
        self.scene.nodes_moved.connect(self.model.on_nodes_moved)
        self.scene.node_renamed.connect(self.model.rename)

        self._init_tab_menu()
        self.new_menu.clear()
        self._init_menu(self.new_menu)
        self.editor_name_label.setText("Material" if self.is_material_mode else "HydraOp")

    def _on_model_reset(self):
        if self.is_material_mode:
            self.is_material_mode = False
            old_model_root = self.model.get_root()
            self.model = hydra_op_node_editor.HydraOpGraphModel()
            self.model.set_root(old_model_root)
            self.model.terminal_node_changed.connect(self.__on_terminal_node_changed)
            self.scene.set_model(self.model)
            self.item_registry = hydra_op_node_editor.HydraOpItemRegistry(self.model)
            self.scene.set_item_registry(self.item_registry)
            self.__update_model_connections()
        self.scene.initialize()
        self.navigation_bar.set_path(self.model.get_root())
        if self.model.get_root().isEmpty:
            self._pop_entry_screen()
        elif self.entry_screen:
            self.entry_screen.deleteLater()
            self.entry_screen = None

        self.__on_terminal_node_changed(self.model.get_terminal_node())
        self.view.fit_to_view()

    def __pop_to_material(self, node_id):
        if not self.entry_screen and not self.is_material_mode:
            self.is_material_mode = True
            parent_root = self.model.get_root()
            self.model = material_editor.MaterialGraphModel()
            self.navigation_bar.set_path(node_id)
            self.scene.set_model(self.model)
            self.item_registry = material_editor.MaterialEditorItemRegistry(self.model)
            self.scene.set_item_registry(self.item_registry)
            self.scene.initialize()
            self.__update_model_connections()
            self.view.fit_to_view()

    def is_vertical_layout(self):
        return True

    def _on_navigation_path_changed(self, path):
        if (
            self.is_material_mode
            and self.model.get_prim_for_node(
                self.model.from_usd_path(path, self.model.get_root())
            ).GetTypeName()
            != "Material"
        ):
            self.is_material_mode = False
            old_model_root = self.model.get_root()
            self.model = hydra_op_node_editor.HydraOpGraphModel()
            self.model.set_root(old_model_root)
            self.model.terminal_node_changed.connect(self.__on_terminal_node_changed)
            self.scene.set_model(self.model)
            self.item_registry = hydra_op_node_editor.HydraOpItemRegistry(self.model)
            self.scene.set_item_registry(self.item_registry)
            self.__update_model_connections()
        self.model.set_root(path)

    def _node_info_text(self, node_id):
        prim = self.model.get_prim_for_node(node_id)
        node_name = prim.GetName() if prim else ""
        node_type = prim.GetTypeName() if prim else ""
        return "{} {} node".format(node_name, node_type) if (node_name and node_type) else ""

    def _port_info_text(self, port):
        port_name = self.model.get_property_name(port.id)
        port_type = "Input" if port.type == 1 else ("Output" if port.type == 2 else "Unknown")
        port_info = (
            "{} ({}) port".format(port_name, port_type)
            if port_name
            else "({}) port".format(port_type)
        )
        return port_info

    def _pop_entry_screen(self):
        if self.entry_screen:
            self.entry_screen.deleteLater()
        self.entry_screen = HydraOpNodeEditorEntryScreen(self.model, self)

    def __on_terminal_node_changed(self, node_id):
        self.terminal_node = hydra_op_node_editor.change_terminal_node(
            self.model, self.scene, self.terminal_node, node_id
        )

    def __bypass_selected_node(self):
        nodes, connections = self.scene.get_selection()
        for node in nodes:
            self.model.toggle_node_bypass(node)

    def create_node(self, node_type):
        if self.is_material_mode:
            node_item = self.item_registry.make_live_usd_node(self.scene, node_type)
        else:
            node_item = self.item_registry.make_live_node(self.scene, node_type)
        self.init_node_item(node_item)

    def _init_menu(self, menu):
        if self.is_material_mode:
            init_material_menu(
                menu, self.item_registry, self.scene, self.init_node_item, self.create_node
            )
        else:
            icon_registry = dcc_core.NodeIconRegistry.instance()

            def get_icon(type):
                icon_path = icon_registry.get_svg("USD", type, dcc_core.IconFlags.NONE)
                return QtGui.QIcon(icon_path)

            def create_light_menu():
                result = QtWidgets.QMenu("Lights")
                for light in [
                    "DiskLight",
                    "DistantLight",
                    "DomeLight",
                    "GeometryLight",
                    "LightFilter",
                    "PortalLight",
                    "RectLight",
                    "SphereLight",
                ]:
                    result.addAction(
                        get_icon(light), light, lambda type=light: self.create_node(type)
                    )
                return result

            hydra_op_base_node = Tf.Type.FindByName("UsdHydraOpBaseNode")
            if hydra_op_base_node:
                types_list = []
                menu.addAction(
                    get_icon("Backdrop"), "Backdrop", lambda: self.create_node("Backdrop")
                )

                derived = hydra_op_base_node.GetAllDerivedTypes()
                for type in derived:
                    type_name = Usd.SchemaRegistry.GetSchemaTypeName(type)
                    if type_name != "HydraOpNodegraph" and type_name != "HydraOpEmpty":
                        types_list.append(type_name)
                for type in types_list:
                    node_display_name = type.split("HydraOp", 1)[1]
                    menu.addAction(
                        get_icon(type), node_display_name, lambda t=type: self.create_node(t)
                    )

                menu.addAction(get_icon("Camera"), "Camera", lambda: self.create_node("Camera"))
                menu.addAction(
                    get_icon("Material"), "Material", lambda: self.create_node("Material")
                )

                menu.addAction(
                    get_icon("RenderSettings"),
                    "RenderSettings",
                    lambda: self.create_node("RenderSettings"),
                )

                menu.addMenu(create_light_menu())

    def _on_context_menu(self, pos):
        if self.is_material_mode:
            on_material_context_menu(pos, self.view, self.model, self.__set_all_groups)
        else:
            menu = QtWidgets.QMenu(self.view)

            node = self.view.node_at(pos)
            if node:
                preview_render_cmd = QtWidgets.QAction(
                    i18n("hydra_op_node_editor.node_context_menu", "Preview Render"), menu
                )
                preview_render_cmd.triggered.connect(
                    lambda: start_render(rendersystem.RenderMethod.PREVIEW)
                )
                menu.addAction(preview_render_cmd)

                ipr_render_cmd = QtWidgets.QAction(
                    i18n("hydra_op_node_editor.node_context_menu", "IPR Render"), menu
                )
                ipr_render_cmd.triggered.connect(
                    lambda: start_render(rendersystem.RenderMethod.IPR)
                )
                menu.addAction(ipr_render_cmd)

                disk_render_cmd = QtWidgets.QAction(
                    i18n("hydra_op_node_editor.node_context_menu", "Disk Render"), menu
                )
                disk_render_cmd.triggered.connect(
                    lambda: start_render(rendersystem.RenderMethod.DISK)
                )
                menu.addAction(disk_render_cmd)

                debug_cmd = QtWidgets.QAction(
                    i18n("hydra_op_node_editor.node_context_menu", "Debug Output"), menu
                )
                debug_cmd.triggered.connect(debug_output)
                menu.addAction(debug_cmd)

            menu.exec_(self.view.mapToGlobal(pos))

    def init_colors_menu(self, palette_menu):
        def on_color_selected(action, mapa):
            color_txt = action.text()
            color = QtGui.QColor(*mapa[color_txt])
            self.set_node_color(color)

        # palette_menu.addAction('None') TODO
        self.color_icons = []
        classic_colors = CLASSIC_COLORS.copy()
        pastel_colors = PASTEL_COLORS.copy()
        pastel_colors[COLOR_NAME_GRAY] = pastel_colors.pop(COLOR_NAME_BLACK)

        def dye_icon(color):
            self.color_icons.append(QtGui.QPixmap(12, 12))
            painter = QtGui.QPainter(self.color_icons[-1])
            painter.setPen(QtGui.QColor(125, 125, 125, 255))
            painter.setBrush(QtGui.QColor(*(color)))
            painter.drawRect(0, 0, 11, 11)

        palette_menu.addSection(i18n("hydra_op_node_editor.menu.colors", "Classic"))
        for color in classic_colors:
            dye_icon(classic_colors[color])
            action = palette_menu.addAction(self.color_icons[-1], color)
            action.triggered.connect(
                lambda checked=False, act=action: on_color_selected(act, classic_colors)
            )

        palette_menu.addSection(i18n("hydra_op_node_editor.menu.colors", "Pastels"))
        for color in pastel_colors:
            dye_icon(pastel_colors[color])
            action = palette_menu.addAction(self.color_icons[-1], color)
            action.triggered.connect(
                lambda checked=False, act=action: on_color_selected(act, pastel_colors)
            )
