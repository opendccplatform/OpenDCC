# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets, QtSvg
from opendcc import cmds
from opendcc.undo import UsdEditsUndoBlock
from opendcc.ui import node_editor
from opendcc.usd_editor import usd_node_editor
import opendcc.core as dcc_core
from opendcc.i18n import i18n
from Qt import QtCompat
from pxr import UsdUI, Gf


class EntryScreen(QtWidgets.QWidget):
    def __init__(self, model, parent):
        QtWidgets.QWidget.__init__(self, parent)
        self.model = model
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)

        main_layout = QtWidgets.QVBoxLayout()
        main_layout.addStretch()
        self.setLayout(main_layout)

        self.label = QtWidgets.QLabel()
        main_layout.addWidget(self.label)

        choice_layout = QtWidgets.QHBoxLayout()
        choice_layout.addStretch()
        main_layout.addLayout(choice_layout)

        self.create_new_btn = QtWidgets.QPushButton()
        self.create_new_btn.clicked.connect(self._on_create_new)
        choice_layout.addWidget(self.create_new_btn)
        self.set_widgets_text()
        self.label.setAlignment(QtCore.Qt.AlignCenter)
        self.label.setSizePolicy(
            QtWidgets.QSizePolicy.MinimumExpanding, QtWidgets.QSizePolicy.Minimum
        )
        main_layout.addStretch()

        open_selected_btn = QtWidgets.QPushButton(
            i18n("node_editor_entry_screen_preform", "Open Selected")
        )
        open_selected_btn.clicked.connect(self._on_open_selected)
        choice_layout.addWidget(open_selected_btn)
        choice_layout.addStretch()

        self.setAutoFillBackground(True)

        if parent:
            self.fit(parent)
        self.show()

    def closeEvent(self, event):
        if self.parent():
            self.parent().entry_screen = None
        event.accept()

    def _on_create_new(self):
        return

    def _on_open_selected(self):
        return

    def set_widgets_text(self):
        return

    def fit(self, widget):
        self.setGeometry(widget.rect())


class CommonNodeEditorWidgetPreform(QtWidgets.QWidget):
    def __init__(
        self, editor_name, model, view, scene, editor, item_registry, parent=None
    ):  # entry_screen_default=True, parent=None):
        QtWidgets.QWidget.__init__(self, parent)
        from .palette_widget import PaletteWidget

        self.model = model
        self.editor_name = editor_name
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)
        self.scene = scene
        self.editor = editor
        self.item_registry = item_registry
        self.main_layout = QtWidgets.QVBoxLayout()
        self.setLayout(self.main_layout)
        self.setContentsMargins(0, 0, 0, 0)
        self.main_layout.setSpacing(2)
        self.main_layout.setContentsMargins(2, 2, 2, 2)

        header_layout = QtWidgets.QVBoxLayout()
        header_layout.setContentsMargins(0, 0, 0, 0)
        self.main_layout.addLayout(header_layout)
        navigation_layout = QtWidgets.QHBoxLayout()
        navigation_layout.setContentsMargins(0, 0, 0, 0)
        header_layout.addLayout(navigation_layout)

        self.navigation_bar = usd_node_editor.NavigationBar()
        navigation_layout.addWidget(self.navigation_bar)

        self.entry_screen = None
        self.scene.set_thumbnail_cache(usd_node_editor.OIIOThumbnailCache(self))

        self.model.model_reset.connect(self._on_model_reset)
        self.model.node_created.connect(self.scene.add_node_item)
        self.model.node_removed.connect(self.scene.remove_node_item)
        self.model.connection_created.connect(self.scene.add_connection_item)
        self.model.connection_removed.connect(self.scene.remove_connection_item)
        self.model.selection_changed.connect(self.scene.set_selection)
        self.model.port_updated.connect(self.scene.update_port)

        self.scene.selection_changed.connect(self.model.on_selection_set)
        self.scene.nodes_moved.connect(self.model.on_nodes_moved)
        self.scene.node_resized.connect(self.model.on_node_resized)
        self.scene.port_pressed.connect(self.__on_port_pressed)
        self.scene.node_renamed.connect(self.model.rename)
        self.scene.node_double_clicked.connect(self._on_node_double_clicked)

        self.app = dcc_core.Application.instance()
        self.escape_key_action_handle = self.app.register_event_callback(
            dcc_core.Application.EventType.UI_ESCAPE_KEY_ACTION, self.__cancel_created
        )
        self.selection_callback_id = self.app.register_event_callback(
            dcc_core.Application.EventType.SELECTION_CHANGED, self.__update_selection
        )

        self.navigation_bar.path_changed.connect(self._on_navigation_path_changed)

        self.view = view
        self._init_tab_menu()
        self.layout().addWidget(self.view)

        self.editor_name_label = EditorNameLabel(self.view, editor_name)

        self.tooltip = QtWidgets.QToolTip()
        tooltip_palette = QtGui.QPalette()
        tooltip_palette.setBrush(
            QtGui.QPalette.ToolTipText, QtWidgets.QApplication.palette().dark()
        )
        tooltip_palette.setBrush(
            QtGui.QPalette.ToolTipBase, QtWidgets.QApplication.palette().base()
        )
        self.tooltip.setPalette(tooltip_palette)

        self.scene.node_hovered.connect(self.__on_node_hovered)
        self.scene.port_hovered.connect(self.__on_port_hovered)
        self.scene.connection_hovered.connect(self.__on_connection_hovered)

        self.scene.port_need_tool_tip.connect(
            lambda port: self._show_tool_tip(self._port_info_text(port))
        )

        self.view.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)

        self.menu_layout = QtWidgets.QHBoxLayout()
        self.menu_layout.setContentsMargins(0, 0, 0, 0)
        header_layout.addLayout(self.menu_layout)
        self.menu_bar = QtWidgets.QMenuBar(self)
        self.menu_layout.addWidget(self.menu_bar)
        self.new_menu = self.menu_bar.addMenu(i18n("node_editor_preform.menu_bar", "New"))
        self._init_menu(self.new_menu)

        self.edit_menu = self.menu_bar.addMenu(i18n("node_editor_preform.menu_bar", "Edit"))
        layout_graph_cmd = self.edit_menu.addAction(
            i18n("node_editor_preform.menu_bar", "Layout All")
        )
        layout_graph_cmd.setShortcut(QtGui.QKeySequence(QtCore.Qt.Key_L))
        is_vertical_layout = self.is_vertical_layout()
        layout_graph_cmd.triggered.connect(
            lambda: cmds.node_editor_layout2(
                QtCompat.getCppPointer(self.scene)[0], vertical=is_vertical_layout
            )
        )
        layout_selected = self.edit_menu.addAction(
            i18n("node_editor_preform.menu_bar", "Layout Selected")
        )
        layout_selected.triggered.connect(
            lambda: cmds.node_editor_layout2(
                QtCompat.getCppPointer(self.scene)[0],
                vertical=is_vertical_layout,
                only_selected=True,
            )
        )

        self.view_menu = self.menu_bar.addMenu(i18n("node_editor_preform.menu_bar", "View"))

        self.backdrop_button = QtWidgets.QToolButton(
            icon=QtGui.QIcon(":/icons/node_editor/backdrop")
        )
        self.backdrop_button.setFixedSize(QtCore.QSize(20, 20))
        self.backdrop_button.setIconSize(QtCore.QSize(20, 20))
        self.backdrop_button.setAutoRaise(True)
        self.backdrop_button.clicked.connect(lambda: self.create_node("Backdrop"))

        self.view.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.view.customContextMenuRequested.connect(self._on_context_menu)

        self.menu_layout.addWidget(self.backdrop_button)
        self.palette_button = QtWidgets.QToolButton(icon=QtGui.QIcon(":/icons/node_editor/Palette"))
        self.palette_button.setFixedSize(QtCore.QSize(20, 20))
        self.palette_button.setIconSize(QtCore.QSize(20, 20))
        self.palette_button.setCheckable(True)
        self.palette_button.setAutoRaise(True)
        self.palette_button.clicked.connect(self.on_palette_toggled)
        self.menu_layout.addWidget(self.palette_button)
        self.palette_shortcut = QtWidgets.QShortcut(QtGui.QKeySequence(QtCore.Qt.Key_C), self)
        self.palette_shortcut.activated.connect(self.on_palette_shortcut)

        bottom_hint_action = self.view_menu.addAction(
            i18n("node_editor_preform.menu_bar", "Show Help Messages")
        )
        bottom_hint_action.setCheckable(True)
        bottom_hint_action.setChecked(True)
        bottom_hint_action.triggered.connect(
            lambda checked: self.view.get_hint_widget().setVisible(checked)
        )

        self.grid_actions = QtWidgets.QActionGroup(self)
        self.grid_actions.setExclusive(True)

        grid_types = {
            "No Grid": node_editor.NodeEditorView.GridType.NoGrid,
            "Grid Points": node_editor.NodeEditorView.GridType.GridPoints,
            "Grid Lines": node_editor.NodeEditorView.GridType.GridLines,
        }

        for grid_name, grid_type in grid_types.items():
            grid = self.grid_actions.addAction(i18n("node_editor_preform.menu_bar", grid_name))
            grid.setCheckable(True)
            if grid_type == node_editor.NodeEditorView.GridType.NoGrid:
                grid.setChecked(True)
            grid.setData(grid_type)

        grid_menu = QtWidgets.QMenu(i18n("node_editor_preform.menu_bar", "Show Grid"))
        grid_menu.addActions(self.grid_actions.actions())
        grid_menu.triggered.connect(self.__set_grid_type)
        self.view_menu.addMenu(grid_menu)

        align_snapping_action = self.view_menu.addAction(
            i18n("node_editor_preform.menu_bar", "Snap to Visible Nodes")
        )
        align_snapping_action.setCheckable(True)
        align_snapping_action.triggered.connect(
            lambda checked: self.view.enable_align_snapping(checked)
        )
        align_snapping_action.setChecked(True)
        self.view.enable_align_snapping(True)

        if self.editor_name == "Material":
            icon_registry = dcc_core.NodeIconRegistry.instance()
            icon_path = icon_registry.get_svg("USD", "Material", dcc_core.IconFlags.NONE)

            renderer = QtSvg.QSvgRenderer(icon_path)
            self.mat_pixmap = QtGui.QPixmap(40, 40)
            self.mat_pixmap.fill(QtGui.QColor(0, 0, 0, 0))

            painter = QtGui.QPainter(self.mat_pixmap)
            renderer.render(painter, self.mat_pixmap.rect())
            self.mat_icon = QtGui.QIcon(self.mat_pixmap)
            self.material_gallery_button = QtWidgets.QToolButton(icon=self.mat_icon)
            self.material_gallery_button.setFixedSize(QtCore.QSize(20, 20))
            self.material_gallery_button.setIconSize(QtCore.QSize(20, 20))
            self.material_gallery_button.setCheckable(True)
            self.material_gallery_button.setAutoRaise(True)

            self.menu_layout.addWidget(self.material_gallery_button)

        self.palette_widget = PaletteWidget(self)
        self.palette_widget.setVisible(self.palette_button.isChecked())
        self.palette_widget.color_selected.connect(self.set_node_color)
        self._pop_entry_screen()

    def __set_grid_type(self, action):
        self.view.set_grid_type(node_editor.NodeEditorView.GridType(action.data()))
        self.view.update_scene()

    def showEvent(self, event):
        self.__update_selection()
        event.accept()

    def __update_selection(self):
        return

    def _on_model_reset(self):
        self.scene.initialize()
        self.navigation_bar.set_path(self.model.get_root())
        if self.model.get_root().isEmpty:
            self._pop_entry_screen()
        elif self.entry_screen:
            self.entry_screen.deleteLater()
            self.entry_screen = None

        self.view.fit_to_view()

    def _on_navigation_path_changed(self, path):
        self.model.set_root(path)

    def _on_node_double_clicked(self, node_id):
        if self.model.can_fall_through(node_id):
            self.model.set_root(node_id)

    def _node_info_text(self, node_id):
        return

    def _port_info_text(self, port):
        return

    def is_vertical_layout(self):
        return False

    def __cancel_created(self):
        if self.scene.has_grabber_item():
            self.scene.remove_grabber_item()
            self.view.clear_align_lines()

    def __try_connect(self, mouse_event):
        self.editor.try_connect(self.model, self.scene, self.view, mouse_event)

    def _init_tab_menu(self):
        self.tab_menu = node_editor.TabSearchMenu(self)
        self.tab_menu.addSeparator()
        self._init_menu(self.tab_menu)
        self.view.set_tab_menu(self.tab_menu)

    def _on_context_menu(self, pos):
        return

    def init_node_item(self, item):
        cursor_scene_pos = self.view.mapToScene(self.view.mapFromGlobal(QtGui.QCursor.pos()))
        item.setPos(
            cursor_scene_pos
            - QtCore.QPointF(item.boundingRect().width() / 2, item.boundingRect().height() / 2)
        )
        self.scene.set_grabber_item(item)

    def create_node(self, usd_type):
        item = self.item_registry.make_live_usd_node(self.scene, usd_type)
        self.init_node_item(item)

    def _init_menu(self, menu):
        return

    def __on_node_hovered(self, node_id, is_hover):
        hint = self.view.get_hint_widget()
        if is_hover:
            hint.update_text(self._node_info_text(node_id))
        else:
            hint.clear_text()

    def _show_tool_tip(self, text):
        # QApplication.desktop() was removed in Qt6; QGuiApplication.screens() is in both
        app_width = 0
        for screen in QtGui.QGuiApplication.screens():
            app_width += screen.geometry().width()

        tooltip_pos = QtGui.QCursor.pos() + QtCore.QPoint(15, -15)
        if tooltip_pos.x() > app_width:
            tooltip_pos = QtGui.QCursor.pos() - QtCore.QPoint(15, 15)
        self.tooltip.showText(tooltip_pos, text)

    def __on_port_hovered(self, port, is_hover):
        hint = self.view.get_hint_widget()
        if is_hover:
            port_info = self._port_info_text(port)
            node_info = self._node_info_text(self.model.get_node_id_from_port(port.id))
            hint.update_text(
                "{}\n{}".format(node_info, port_info) if (node_info and port_info) else port_info
            )
        else:
            self.tooltip.hideText()
            hint.clear_text()

    def __on_connection_hovered(self, connection, is_hover):
        hint = self.view.get_hint_widget()
        if is_hover:
            start = "{} ({})".format(
                self.model.get_prim_for_node(
                    self.model.get_node_id_from_port(connection.start_port)
                ).GetName(),
                self.model.get_property_name(connection.start_port),
            )
            end = "{} ({})".format(
                self.model.get_prim_for_node(
                    self.model.get_node_id_from_port(connection.end_port)
                ).GetName(),
                self.model.get_property_name(connection.end_port),
            )
            hint.update_text("Connection {} -> {}".format(start, end))
        else:
            hint.clear_text()

    def on_palette_shortcut(self):
        checked = self.palette_button.isChecked()
        self.on_palette_toggled(not checked)
        self.palette_button.nextCheckState()

    def on_palette_toggled(self, checked):
        if checked:
            self.palette_widget.show()
        else:
            self.palette_widget.hide()

    def set_node_color(self, color):
        nodes, _ = self.scene.get_selection()
        with UsdEditsUndoBlock():
            for node_id in nodes:
                prim = self.model.get_prim_for_node(node_id)
                if prim:
                    api = UsdUI.NodeGraphNodeAPI.Apply(prim)
                    if api:  # if no api or prim is invalid
                        gf_color = Gf.Vec3f(color.redF(), color.greenF(), color.blueF())
                        api.GetDisplayColorAttr().Set(gf_color)
                        self.scene.update_color(node_id)
                        if "mat_in" in node_id:
                            self.scene.update_color(node_id.replace("mat_in", "mat_out"))
                        elif "mat_out" in node_id:
                            self.scene.update_color(node_id.replace("mat_out", "mat_in"))

    def __remove_selected(self):
        nodes, connections = self.scene.get_selection()
        self.model.remove(nodes, connections)

    def event(self, event):
        if event.type() == QtCore.QEvent.ShortcutOverride:
            if event.key() == QtCore.Qt.Key_Delete:
                self.__remove_selected()
                event.accept()

        return QtWidgets.QWidget.event(self, event)

    def _pop_entry_screen(self):
        return

    def __on_port_pressed(self, port):
        item = self.item_registry.make_live_connection(self.scene, self.view, port)
        if item:
            item.mouse_pressed.connect(self.__try_connect)
            item.mouse_released.connect(self.__try_connect)
            self.scene.set_grabber_item(item)

    def resizeEvent(self, event):
        if self.entry_screen:
            self.entry_screen.fit(self)
        if self.palette_widget:
            self.palette_widget.fit(self)
        self.editor_name_label.fit(self)
        QtWidgets.QWidget.resizeEvent(self, event)

    def closeEvent(self, event):
        QtWidgets.QWidget.closeEvent(self, event)
        self.model = None
        self.item_registry = None
        self.entry_screen = None
        self.scene = None
        self.view = None
        if self.selection_callback_id:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.SELECTION_CHANGED, self.selection_callback_id
            )
            self.selection_callback_id = None

        if self.escape_key_action_handle:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.UI_ESCAPE_KEY_ACTION, self.escape_key_action_handle
            )
            self.escape_key_action_handle = None

        event.accept()


class EditorNameLabel(QtWidgets.QLabel):
    def __init__(self, parent, editor_name):
        QtWidgets.QLabel.__init__(self, parent)
        self.setText(editor_name)
        font = QtGui.QFont()
        font.setBold(True)
        font.setPointSize(20)
        self.setFont(font)
        self.setStyleSheet("color: rgba(250, 250, 250, 40);")
        self.setParent(parent)
        self.setVisible(True)
        self.fit(parent)

    def fit(self, parent):
        self.move(parent.width() - self.width() - 20, 10)
