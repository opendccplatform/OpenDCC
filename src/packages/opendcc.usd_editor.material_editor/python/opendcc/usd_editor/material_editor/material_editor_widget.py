# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets, QtSvg
from opendcc import cmds
from opendcc.i18n import i18n
import opendcc.core as dcc_core
from opendcc.ui import node_editor
from opendcc.usd_editor import material_editor
from opendcc.usd_editor.usd_node_editor.node_editor_preform import (
    EntryScreen,
    CommonNodeEditorWidgetPreform,
)
import opendcc.usd_fallback_proxy as ufp

from Qt import QtCompat
from pxr import Usd, UsdShade, Sdr

GraphicsItemType = node_editor.NodeEditorScene.GraphicsItemType


def on_material_context_menu(pos, view, model, __set_all_groups):
    menu = QtWidgets.QMenu(view)

    collapse_all_groups = menu.addAction(
        i18n("material_editor.context_menu", "Collapse All Input Groups")
    )
    collapse_all_groups.triggered.connect(lambda: __set_all_groups(False, pos))

    expand_all_groups = menu.addAction(
        i18n("material_editor.context_menu", "Expand All Input Groups")
    )
    expand_all_groups.triggered.connect(lambda: __set_all_groups(True, pos))

    menu.addSeparator()

    def assign_material_to_selected():
        root = model.get_root()
        cmds.assign_material(material=root)

    menu.addAction(
        i18n("material_editor.context_menu", "Assign Material to Selected"),
        assign_material_to_selected,
    )
    assign_material_with_purpose_menu = menu.addMenu(
        i18n("material_editor.context_menu", "Assign Material with Purpose")
    )

    def assign_material_full():
        root = model.get_root()
        cmds.assign_material(material=root, material_purpose="full")

    assign_material_with_purpose_menu.addAction(
        i18n("material_editor.menu_bar", "Full"), assign_material_full
    )

    def assign_material_preview():
        root = model.get_root()
        cmds.assign_material(material=root, material_purpose="preview")

    assign_material_with_purpose_menu.addAction(
        i18n("material_editor.menu_bar", "Preview"), assign_material_preview
    )

    menu.exec_(view.mapToGlobal(pos))


def init_material_menu(menu, item_registry, scene, init_node_item, create_node):
    def create_shader_node(shader_name, shader_id):
        def make_fn(shader_name, shader_id):
            item = item_registry.make_live_shader_node(scene, shader_name, shader_id)
            init_node_item(item)

        return lambda: make_fn(shader_name, shader_id)

    class GroupType:
        Category = 1
        Label = 2

    def group_by(group_type, nodes, display_name, blocklist):
        grouping_result = {}
        for node in nodes:
            if Usd.GetMinorVersion() < 21:
                node_identifier = dcc_core.ShaderNodeRegistry.get_node_identifier(node)
            else:
                node_identifier = node.identifier
            shader_node = sdr_registry.GetShaderNodeByIdentifier(node_identifier)
            if not shader_node:
                continue
            if display_name == "renderman" and shader_node.GetContext() in blocklist:
                continue

            if group_type == GroupType.Category:
                group = shader_node.GetRole()
                if display_name == "renderman":
                    group = shader_node.GetContext()
            elif group_type == GroupType.Label:
                group = shader_node.GetLabel()

            if group == node.name or not group in grouping_result:
                grouping_result[group] = [node]
            else:
                grouping_result[group].append(node)
        return grouping_result

    def create_menu(group_nodes, menu_name, action_pixmap, plugin_name):
        menu = QtWidgets.QMenu(menu_name)
        menu.setIcon(QtGui.QIcon(action_pixmap))
        for node in group_nodes:
            if Usd.GetMinorVersion() < 21:
                node_identifier = dcc_core.ShaderNodeRegistry.get_node_identifier(node)
            else:
                node_identifier = node.identifier

            shader_node = sdr_registry.GetShaderNodeByIdentifier(node_identifier)
            action_name = shader_node.GetLabel()
            if action_name == "" or plugin_name == "usdMtlx":
                action_name = node.name
            action = menu.addAction(action_name, create_shader_node(node.name, node_identifier))
            action.setIcon(QtGui.QIcon(action_pixmap))
        return menu

    # TODO: NodeIconRegistry::get_icon() returns QPixmap but it uses Boost python and fails
    # to return correct result, create QPixmap directly for now
    def get_icon(icon_path):
        return QtGui.QPixmap(icon_path)

    # block list for what is not suitable for the context of the materials editor
    blocklist = {
        "usdLux",
        "displaydriver",
        "integrator",
        "sampleFilter",
        "light",
        "lightFilter",
        "lightfilter",
        "displayfilter",
    }
    sdr_registry = Sdr.Registry()
    icon_registry = dcc_core.NodeIconRegistry.instance()

    menu.addAction(get_icon(":/icons/backdrop"), "Backdrop", lambda: create_node("Backdrop"))
    menu.addAction(get_icon(":/icons/nodegraph"), "NodeGraph", lambda: create_node("NodeGraph"))

    for plugin_name in dcc_core.ShaderNodeRegistry.get_loaded_node_plugin_names():
        display_name = plugin_name
        pixmap = get_icon(":/icons/shader")

        if plugin_name == "ndrArnold":
            display_name = "arnold"
            pixmap = QtGui.QPixmap(":/icons/render_arnold")
        elif plugin_name == "rmanDiscovery":
            display_name = "renderman"
            pixmap = QtGui.QPixmap(":/icons/render_renderman")
        elif plugin_name == "ndrCycles":
            display_name = "cycles"
            pixmap = QtGui.QPixmap(":/icons/render_cycles")
        elif plugin_name == "sdrKarmaDiscovery":
            display_name = "karma"
            pixmap = QtGui.QPixmap(":/icons/render_karma")
        elif plugin_name == "usdShaders":
            pixmap = QtGui.QPixmap(":/icons/render_usd")
        elif plugin_name == "usdMtlx":
            pixmap = QtGui.QPixmap(":/icons/render_materialx")

        if display_name in blocklist:
            continue

        nodes = dcc_core.ShaderNodeRegistry.get_ndr_plugin_nodes(plugin_name)
        node_menu = QtWidgets.QMenu(display_name)
        node_menu.setIcon(QtGui.QIcon(pixmap))
        groups = group_by(GroupType.Category, nodes, display_name, blocklist)
        for group in groups:
            if len(groups[group]) == 1:
                if Usd.GetMinorVersion() < 21:
                    node_identifier = dcc_core.ShaderNodeRegistry.get_node_identifier(
                        groups[group][0]
                    )
                else:
                    node_identifier = groups[group][0].identifier
                shader_node = sdr_registry.GetShaderNodeByIdentifier(node_identifier)
                action_name = shader_node.GetLabel()
                if action_name == "" or plugin_name == "usdMtlx":
                    action_name = groups[group][0].name
                action = node_menu.addAction(
                    action_name, create_shader_node(action_name, node_identifier)
                )
                action.setIcon(QtGui.QIcon(pixmap))
                continue
            if display_name == "usdMtlx":
                group_menu = QtWidgets.QMenu(group)
                group_menu.setIcon(QtGui.QIcon(pixmap))
                sorted_groups = group_by(GroupType.Label, groups[group], display_name, blocklist)
                for sorted_group in sorted_groups:
                    if len(sorted_groups[sorted_group]) == 1:
                        if Usd.GetMinorVersion() < 21:
                            node_identifier = dcc_core.ShaderNodeRegistry.get_node_identifier(
                                sorted_groups[sorted_group][0]
                            )
                        else:
                            node_identifier = sorted_groups[sorted_group][0].identifier
                        shader_node = sdr_registry.GetShaderNodeByIdentifier(node_identifier)
                        action_name = sorted_groups[sorted_group][0].name
                        action = group_menu.addAction(
                            action_name, create_shader_node(action_name, node_identifier)
                        )
                        action.setIcon(QtGui.QIcon(pixmap))
                    else:
                        group_menu.addMenu(
                            create_menu(
                                sorted_groups[sorted_group], sorted_group, pixmap, plugin_name
                            )
                        )
                node_menu.addMenu(group_menu)
            else:
                node_menu.addMenu(create_menu(groups[group], group, pixmap, plugin_name))

        if node_menu.actions():
            menu.addMenu(node_menu)


class MaterialEditorResizeEventFilter(QtCore.QObject):
    def __init__(self, widget):
        QtCore.QObject.__init__(self, widget)
        self.widget = widget

    def eventFilter(self, widget, event):
        if event.type() == QtCore.QEvent.Resize:
            if self.widget.palette_widget:
                self.widget.palette_widget.fit(self.widget)
        try:
            return QtCore.QObject.eventFilter(self, widget, event)
        except RuntimeError:
            return True


class MaterialEditorEntryScreen(EntryScreen):
    def _on_create_new(self):
        res = cmds.create_material("Material")
        if res.is_successful():
            self.model.set_root(res.get_result())
            self.close()

    def _on_open_selected(self):
        selection = dcc_core.Application.instance().get_prim_selection()
        stage = dcc_core.Application.instance().get_session().get_current_stage()
        if not stage:
            return

        candidate = None
        for path in selection:
            prim = stage.GetPrimAtPath(path)
            if prim:
                mat = UsdShade.Material(prim)
                if mat:
                    if not candidate:
                        candidate = prim.GetPath()
                    else:
                        return  # more than one material detected, cannot determine which to open

        if candidate:
            self.model.set_root(candidate)
            self.close()

    def set_widgets_text(self):
        self.create_new_btn.setText(i18n("material_editor.entry_screen", "Create New Material"))
        self.label.setText(
            i18n(
                "material_editor.entry_screen",
                "Create a new material network or select existing to get started.",
            )
        )


class MaterialEditorWidget(QtWidgets.QWidget):
    def __init__(self, editor_name, model, view, scene, editor, item_registry, parent=None):
        QtWidgets.QWidget.__init__(self, parent=parent)
        self.model = model
        self.splitter = QtWidgets.QSplitter(QtCore.Qt.Vertical)
        self.splitter.setStyleSheet(
            """
            QSplitter::handle {
                background: rgb(50,50,50);
                height: 1px;
                }
            QSplitter::handle:pressed {
                background: rgb(170,170,170);
            }
            """
        )
        self.main_layout = QtWidgets.QVBoxLayout()
        self.main_layout.addWidget(self.splitter)
        self.main_layout.setSpacing(2)
        self.main_layout.setContentsMargins(0, 0, 0, 0)
        self.setLayout(self.main_layout)
        self.material_node_editor = MaterialEditor(
            editor_name, model, view, scene, editor, item_registry, parent=self
        )


        self.splitter.addWidget(self.material_node_editor)


class MaterialEditor(CommonNodeEditorWidgetPreform):

    def __init__(self, editor_name, model, view, scene, editor, item_registry, parent=None):
        CommonNodeEditorWidgetPreform.__init__(
            self, editor_name, model, view, scene, editor, item_registry, parent
        )
        self.preview_shader = ""
        self.model.preview_shader_changed.connect(self.__on_preview_shader_changed)

        self.scene.group_hovered.connect(self.__on_group_hovered)
        self.scene.group_need_tool_tip.connect(
            lambda name: self._show_tool_tip("Input group " + name)
        )

        action = self.view_menu.addAction(i18n("material_editor.menu_bar", "Show External Nodes"))
        action.setCheckable(True)
        action.setChecked(False)
        action.triggered.connect(lambda checked: self.model.set_show_external_nodes(checked))

        self.select_material_of_prim = False
        action = self.view_menu.addAction(
            i18n("material_editor.menu_bar", "Switch to Assigned Material")
        )
        action.setCheckable(True)
        action.setChecked(False)
        action.triggered.connect(lambda checked: self.set_select_material_of_prim(checked))

        self.select_material_of_shader = False
        action = self.view_menu.addAction(
            i18n("material_editor.menu_bar", "Sync Material on Selection")
        )
        action.setCheckable(True)
        action.setChecked(False)
        action.triggered.connect(self.set_select_material_of_shader)
        self.setProperty("unfocusedKeyEvent_enable", True)

        self.app = dcc_core.Application.instance()

        self.selection_callback_id = self.app.register_event_callback(
            dcc_core.Application.EventType.SELECTION_CHANGED, self.__update_selection
        )

    def event(self, event):
        if event.type() == QtCore.QEvent.ShortcutOverride:
            if event.key() == QtCore.Qt.CTRL or event.key() == QtCore.Qt.Key_G:
                cmds.group_prim_to_nodegraph()
                event.accept()

        return QtWidgets.QWidget.event(self, event)

    def set_select_material_of_prim(self, is_enabled):
        self.select_material_of_prim = is_enabled

    def set_select_material_of_shader(self, is_enabled):
        self.select_material_of_shader = is_enabled

    def __update_selection(self):
        selection = dcc_core.Application.instance().get_prim_selection()
        stage = dcc_core.Application.instance().get_session().get_current_stage()
        if not stage:
            return
        candidate = None
        root_prim = self.model.get_root()
        for path in selection:
            prim = stage.GetPrimAtPath(path)

            if prim:
                relationship = self.__get_relationship(prim)
                if relationship and self.select_material_of_prim:
                    for target in relationship.GetTargets():
                        candidate = target
                        break

                elif self.select_material_of_shader:
                    if prim.IsA(UsdShade.Shader):
                        prim_parent = prim.GetParent()
                        while prim_parent:
                            if prim_parent.IsA(UsdShade.Material) or prim_parent.IsA(
                                UsdShade.NodeGraph
                            ):
                                candidate = prim_parent.GetPath()
                                break
                            prim_parent = prim_parent.GetParent()

                    elif prim.IsA(UsdShade.Material) or (
                        prim.IsA(UsdShade.NodeGraph) and root_prim != prim.GetParent().GetPath()
                    ):
                        candidate = prim.GetPath()

            if candidate and root_prim != candidate:
                self.model.set_root(candidate)
                break

    def __get_relationship(self, prim):
        if not prim:
            return None

        api = UsdShade.MaterialBindingAPI(prim)

        if api:
            material_relationships = [
                UsdShade.Tokens.allPurpose,
                UsdShade.Tokens.full,
                UsdShade.Tokens.preview,
            ]

            for relationship_name in material_relationships:
                relationship = api.GetDirectBindingRel(relationship_name)
                if relationship:
                    return relationship

        return None

    def __get_property_type(self, port_id):
        property_proxy = ufp.SourceRegistry.get_property_proxy(
            self.model.get_prim_for_node((self.model.get_node_id_from_port(port_id))),
            self.model.get_property_name(port_id),
        )
        return property_proxy.get_type_name() if property_proxy else ""

    def __get_shader_type_name(self, node_id):
        prim = self.model.get_prim_for_node(node_id)
        shader_type = UsdShade.Shader(prim).GetIdAttr().Get()
        return shader_type

    def _node_info_text(self, node_id):
        prim = self.model.get_prim_for_node(node_id)
        node_name = prim.GetName() if prim else ""
        node_type = prim.GetTypeName() if prim else ""
        shader_type_name = self.__get_shader_type_name(node_id)
        if not shader_type_name:
            shader_type = node_id.rsplit("#", 1)[-1]
            if shader_type == "mat_in":
                shader_type_name = "Input"
            elif shader_type == "mat_out":
                shader_type_name = "Output"
        node_description = (
            "({} {})".format(node_type, shader_type_name)
            if (node_type and shader_type_name)
            else ""
        )
        return (
            "{} {} node".format(node_name, node_description)
            if node_name
            else ("{} node".format(node_description) if node_description else "")
        )

    def _port_info_text(self, port):
        port_name = self.model.get_property_name(port.id)
        port_description = self.__get_property_type(port.id)
        port_info = "{} ({})".format(port_name, port_description) if port_description else port_name
        return port_info

    def __on_group_hovered(self, name, is_hover):
        hint = self.view.get_hint_widget()
        if is_hover:
            shader_node = self.view.node_at(self.view.mapFromGlobal(QtGui.QCursor.pos()))
            node_info = self._node_info_text(shader_node.get_id())
            name = "Input group " + name

            hint.update_text("{}\n{}".format(node_info, name) if (node_info and name) else name)
        else:
            self.tooltip.hideText()
            hint.clear_text()

    def _pop_entry_screen(self):
        if self.entry_screen:
            self.entry_screen.deleteLater()
        self.entry_screen = MaterialEditorEntryScreen(self.model, self)

    def __on_preview_shader_changed(self, new_shader_path):
        self.preview_shader = material_editor.change_preview_shader(
            self.model, self.scene, self.preview_shader, new_shader_path
        )

    def init_node_item(self, item):
        cursor_scene_pos = self.view.mapToScene(self.view.mapFromGlobal(QtGui.QCursor.pos()))
        item.setPos(
            cursor_scene_pos
            - QtCore.QPointF(item.boundingRect().width() / 2, item.boundingRect().height() / 2)
        )
        self.scene.set_grabber_item(item)

    def _init_menu(self, menu):
        init_material_menu(
            menu, self.item_registry, self.scene, self.init_node_item, self.create_node
        )

    def __set_all_groups(self, is_expanded, pos):
        shader_node = self.view.node_at(pos)
        shader_node.__class__ = material_editor.ShaderNodeItem
        if shader_node:
            shader_node.set_all_groups(is_expanded)

    def _on_context_menu(self, pos):
        on_material_context_menu(pos, self.view, self.model, self.__set_all_groups)

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
