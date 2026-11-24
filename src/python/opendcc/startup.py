# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

APPLY_SCHEMA_PATH_CALLBACK = None


def init():
    import opendcc.core as dcc_core
    import opendcc.app_config

    from . import hooks

    hooks.execute_user_setup()


def init_ui():
    from .ui_utils import update_window_title

    update_window_title()
    from .plugin_manager import plugin_manager as pm

    import opendcc.core as dcc_core

    app = dcc_core.Application.instance()

    mw = app.get_main_window()
    from .close_event_filter import AppCloseEventFilter

    filter_close_event = AppCloseEventFilter(mw)
    mw.installEventFilter(filter_close_event)

    from . import icon_registry

    icon_registry.init_usd_icons()

    from .ui_utils import init_update_window_title_callbacks

    init_update_window_title_callbacks()
    from .i18n import i18n

    registry = dcc_core.PanelFactory.instance()

    # creating menu Operations
    from .actions.reparent_prim import ReparentAction
    from .actions.unparent_prim import UnparentAction
    from .actions.group_prims import GroupAction
    from .actions.remove_prim import RemoveAction
    from .actions.duplicate_prims import DuplicateAction
    from .actions.change_prims_visibility import VisibilityAction
    from Qt import QtWidgets, QtCore

    undo_stack = app.get_undo_stack()
    menu_bar = mw.menuBar()

    menu_file = menu_bar.addMenu(i18n("main_menu", "File"))
    menu_file.setObjectName("file_menu")
    from . import file_menu

    file_menu.fill_file_menu(menu_file)

    # menu Edit and all it's actions
    menu_edit = menu_bar.addMenu(i18n("main_menu", "Edit"))
    from opendcc.actions.custom_widget_action import fix_menu

    fix_menu(menu_edit)

    menu_edit.setObjectName("edit_menu")

    reparent_action = ReparentAction(menu_edit)
    reparent_action.setObjectName("reparent_action")
    menu_edit.addAction(reparent_action)  # it's this way because it returns None otherwise
    unparent_action = UnparentAction(menu_edit)
    unparent_action.setObjectName("unparent_action")
    menu_edit.addAction(unparent_action)
    group_action = GroupAction(menu_edit)
    group_action.setObjectName("group_action")
    menu_edit.addAction(group_action)
    duplicate_action = DuplicateAction(menu_edit)
    duplicate_action.setObjectName("duplicate_action")
    menu_edit.addAction(duplicate_action)

    from pxr import Usd, UsdGeom

    set_visible_action = VisibilityAction(
        i18n("main_menu.edit", "Set Visible"), "Shift+H", UsdGeom.Tokens.inherited, menu_edit
    )
    set_visible_action.setObjectName("set_visible_action")
    menu_edit.addAction(set_visible_action)

    set_invisible_action = VisibilityAction(
        i18n("main_menu.edit", "Set Invisible"), "Ctrl+H", UsdGeom.Tokens.invisible, menu_edit
    )
    set_invisible_action.setObjectName("set_invisible_action")
    menu_edit.addAction(set_invisible_action)

    menu_edit.addSeparator()

    menu_edit.addAction(RemoveAction(menu_edit))
    menu_edit.addSeparator()

    redo_action = QtWidgets.QAction(i18n("main_menu.edit", "Redo"), menu_edit)
    redo_action.setObjectName("redo_action")
    redo_action.setShortcut("Ctrl+Y")
    redo_action.setShortcutContext(QtCore.Qt.ApplicationShortcut)
    redo_action.triggered.connect(lambda: undo_stack.redo())
    menu_edit.addAction(redo_action)
    undo_action = QtWidgets.QAction(i18n("main_menu.edit", "Undo"), menu_edit)
    undo_action.setObjectName("undo_action")
    undo_action.setShortcut("Ctrl+Z")
    undo_action.setShortcutContext(QtCore.Qt.ApplicationShortcut)
    undo_action.triggered.connect(lambda: undo_stack.undo())
    menu_edit.addAction(undo_action)

    # creating menu Create and all its actions
    from .actions.create_geometry_menu import CreateGeometryMenu
    from .actions.create_lights_menu import CreateLightsMenu

    menu_create = menu_bar.addMenu(i18n("main_menu", "Create"))
    fix_menu(menu_create)
    menu_create.setObjectName("create_menu")

    menu_create_geometry = CreateGeometryMenu(menu_create)
    menu_create.addMenu(menu_create_geometry)
    menu_create_light = CreateLightsMenu(menu_create)
    menu_create_light.setObjectName("create_light_menu")

    menu_create.addMenu(menu_create_light)
    from .actions.create_reference import CreateReferenceAction

    menu_create.addAction(
        CreateReferenceAction(i18n("main_menu.create", "Reference"), parent=menu_create)
    )
    from .actions.create_prim import CreatePrimAction
    from Qt.QtGui import QIcon

    menu_create.addAction(
        CreatePrimAction(
            "Camera",
            "Camera",
            icon=QIcon(":icons/camera.png"),
            label=i18n("main_menu.create", "Camera"),
            parent=menu_create,
        )
    )
    menu_modify = menu_bar.addMenu(i18n("main_menu", "Modify"))
    menu_modify.setObjectName("modify_menu")
    from .actions.center_pivot import CenterPivotAction

    center_pivot_action = CenterPivotAction(menu_modify)
    center_pivot_action.setObjectName("center_pivot_action")
    menu_modify.addAction(center_pivot_action)

    from .actions.set_instanceable import SetInstanceableAction

    set_instanceable_action = SetInstanceableAction(menu_modify)
    set_instanceable_action.setObjectName("set_instanceable_action")

    menu_modify.addAction(set_instanceable_action)

    from opendcc.render_toolbar import (
        OpenRenderViewAction,
        PreviewRenderAction,
        IprRenderAction,
        StopRenderAction,
        DiskRenderAction,
    )

    from opendcc.session_ui import subscribe_to_no_stage_signal

    subscribe_to_no_stage_signal([menu_edit, menu_create, menu_modify])

    # menu Render and all its actions
    menu_render = menu_bar.addMenu(i18n("main_menu", "Render"))
    menu_render.setObjectName("menu_render")

    menu_render.addAction(OpenRenderViewAction(menu_render))
    menu_render.addAction(PreviewRenderAction(menu_render))
    menu_render.addAction(IprRenderAction(menu_render))
    menu_render.addAction(DiskRenderAction(menu_render))
    menu_render.addAction(StopRenderAction(menu_render))

    # menu Windows and all its actions
    menu_windows = menu_bar.addMenu(i18n("main_menu", "Windows"))
    menu_windows.setObjectName("menu_windows")

    show_plugin_manager_action = QtWidgets.QAction(
        i18n("main_menu.windows", "Plug-in Manager"), menu_windows
    )
    show_plugin_manager_action.setObjectName("show_plugin_manager_action")
    show_plugin_manager_action.triggered.connect(pm.PluginManager.show_window)
    menu_windows.addAction(show_plugin_manager_action)
    from .preferences_widget import PreferencesWidget

    preferences_action = menu_windows.addAction(
        i18n("main_menu.windows", "Preferences"), PreferencesWidget.show_window
    )
    preferences_action.setObjectName("preferences_action")

    # panels menu
    menu_panels = menu_bar.addMenu(i18n("main_menu", "Panels"))

    # layouts menu
    from . import layouts

    layouts_menu = menu_bar.addMenu(i18n("main_menu", "Layouts"))
    layouts_menu.setObjectName("layouts_menu")
    layouts_menu.addAction(
        QIcon(":icons/layout_default"), i18n("main_menu.layouts", "Default"), layouts.default_layout
    )
    layouts_menu.addAction(
        QIcon(":icons/layout_scripting"),
        i18n("main_menu.layouts", "Scripting"),
        layouts.scripting_layout,
    )

    layouts_separator = layouts_menu.addSeparator()
    layouts_separator.setObjectName("layouts_menu_separator")

    layouts_menu.addAction(
        QIcon(":icons/save_small"), i18n("main_menu.layouts", "Save Layout"), layouts.save_layout
    )
    layouts_menu.addAction(
        QIcon(":icons/small_garbage"),
        i18n("main_menu.layouts", "Delete Layout"),
        layouts.delete_layout,
    )
    layouts.rebuild_menu()

    # help menu
    menu_help = menu_bar.addMenu(i18n("main_menu", "Help"))

    def _about():
        app = dcc_core.Application.instance()
        app_config = dcc_core.Application.get_app_config()
        title = app_config.get_string("settings.app.window.title")
        company_name = app_config.get_string("settings.app.window.about.company")
        message_txt = """
        {:s}
        {:s}: {:s}
        {:s}: {:s}
        {:s}: {:s}
        {:s}: {:s}
        2024 {:s}
        """.format(
            title,
            i18n("main_menu.about", "Application Version"),
            app_config.get_string("settings.app.version"),
            i18n("main_menu.about", "OpenDCC Version"),
            app.get_opendcc_version_string(),
            i18n("main_menu.about", "Git Hash"),
            app.get_commit_hash(),
            i18n("main_menu.about", "Build Date"),
            app.get_build_date(),
            company_name,
        )
        QtWidgets.QMessageBox.about(app.get_main_window(), title, message_txt)

    menu_help.addAction(i18n("main_menu.help", "About"), _about)
    # session_toolbar
    from .session_widget import SessionToolbarWidget

    session_toolbar = QtWidgets.QToolBar(i18n("session_toolbar", "Session"), mw)
    session_toolbar.setObjectName("session_toolbar")
    session_toolbar.setProperty("opendcc_toolbar", True)
    session_toolbar.setProperty("opendcc_toolbar_side", "top")
    session_toolbar.setProperty("opendcc_toolbar_row", 0)
    session_toolbar.setProperty("opendcc_toolbar_index", 0)
    session_toolbar.addWidget(SessionToolbarWidget())
    mw.addToolBar(QtCore.Qt.ToolBarArea.TopToolBarArea, session_toolbar)

    from . import render_toolbar

    render_toolbar.init_render_toolbar()

    # edit target toolbar
    from .current_edit_target import EditTargetPanel

    edit_target_panel = EditTargetPanel()
    edit_target_toolbar = QtWidgets.QToolBar(i18n("edit_target_panel", "Edit Target"), mw)
    edit_target_toolbar.setObjectName("edit_target_toolbar")
    edit_target_toolbar.setProperty("opendcc_toolbar", True)
    edit_target_toolbar.setProperty("opendcc_toolbar_side", "top")
    edit_target_toolbar.setProperty("opendcc_toolbar_row", 0)
    edit_target_toolbar.setProperty("opendcc_toolbar_index", 3)
    edit_target_toolbar.addWidget(edit_target_panel)
    mw.addToolBar(QtCore.Qt.ToolBarArea.TopToolBarArea, edit_target_toolbar)

    # shelf toolbar
    # it should be created last to be on the second row
    from .actions.tab_toolbar import Shelf

    shelf = Shelf()
    from Qt import QtCore

    mw.addToolBarBreak(QtCore.Qt.TopToolBarArea)
    mw.addToolBar(QtCore.Qt.TopToolBarArea, shelf)

    # Render Tab
    tool_bar_render_name = i18n("shelf.toolbar", "Render")
    tool_bar_render = QtWidgets.QToolBar(tool_bar_render_name)

    tool_bar_render.addAction(OpenRenderViewAction(tool_bar_render))
    tool_bar_render.addAction(PreviewRenderAction(tool_bar_render))
    tool_bar_render.addAction(IprRenderAction(tool_bar_render))
    tool_bar_render.addAction(DiskRenderAction(tool_bar_render))
    tool_bar_render.addAction(StopRenderAction(tool_bar_render))
    shelf.add_tab(tool_bar_render, tool_bar_render_name)

    # Tools
    from .actions.tool_context_actions import ToolContextActions
    from .actions.tool_settings_button import ToolSettingsButton

    tool_context_toolbar = QtWidgets.QToolBar(i18n("toolbars.tools", "Tools"), mw)
    tool_context_toolbar.setObjectName("tools_toolbar")
    tool_context_toolbar.setProperty("opendcc_toolbar", True)
    tool_context_toolbar.setProperty("opendcc_toolbar_side", "left")
    tool_context_toolbar.setProperty("opendcc_toolbar_row", 0)
    tool_context_toolbar.setProperty("opendcc_toolbar_index", 0)

    tool_context_toolbar.setIconSize(QtCore.QSize(20, 20))

    select_action = ToolContextActions().get_action("USD", "select_tool")
    move_action = ToolContextActions().get_action("USD", "move_tool")
    rotate_action = ToolContextActions().get_action("USD", "rotate_tool")
    scale_action = ToolContextActions().get_action("USD", "scale_tool")
    usd_actions = [select_action, move_action, rotate_action, scale_action]

    for action in usd_actions:
        tool_button = ToolSettingsButton(action)
        parent_action = tool_context_toolbar.addWidget(tool_button)
        tool_button.set_parent_action(parent_action)

    # selection and snap modes
    tool_context_toolbar_separator = tool_context_toolbar.addSeparator()

    from . import selection_mode

    selection_mode.init_toggle_selection_mode()  # F8 hotkey
    selection_mode_button = selection_mode.selection_mode_button()
    selection_mode_action = tool_context_toolbar.addWidget(selection_mode_button)

    from . import snap_mode

    snap_mode_button = snap_mode.snap_mode_button()
    snap_mode_action = tool_context_toolbar.addWidget(snap_mode_button)

    tool_context_mode_actions = [
        tool_context_toolbar_separator,
        selection_mode_action,
        snap_mode_action,
    ]

    def _on_active_view_changed():
        ctx = dcc_core.Application.instance().get_active_view_scene_context()

        if ctx:
            all_actions = ToolContextActions().get_actions()
            ctx_actions = ToolContextActions().get_actions(ctx)
            other_ctx_actions = [action for action in all_actions if action not in ctx_actions]
            show_actions = ctx_actions
            hide_actions = other_ctx_actions

            for action in show_actions:
                action.setVisible(True)
            for action in hide_actions:
                action.setVisible(False)
            for action in tool_context_mode_actions:
                action.setVisible(ctx == "USD")

        ToolContextActions().switch_context(ctx)

    dcc_core.Application.instance().register_event_callback(
        "active_view_changed", _on_active_view_changed
    )
    dcc_core.Application.instance().register_event_callback(
        "active_view_scene_context_changed", _on_active_view_changed
    )

    select_action.trigger()

    from .viewport_layout import viewport_layout_button

    viewport_layout_spacing = QtWidgets.QWidget(tool_context_toolbar)
    viewport_layout_spacing.setFixedSize(8, 8)
    tool_context_toolbar.addWidget(viewport_layout_spacing)
    tool_context_toolbar.addSeparator()
    tool_context_toolbar.addWidget(viewport_layout_button(tool_context_toolbar))
    mw.addToolBar(QtCore.Qt.LeftToolBarArea, tool_context_toolbar)

    # shortcuts
    from functools import partial
    import opendcc.cmds as cmds

    def add_pick_walk(key, direction):
        walk_action = QtWidgets.QAction(mw)
        walk_action.setShortcut(key)
        walk_action.triggered.connect(partial(lambda: cmds.pick_walk(direction)))
        mw.addAction(walk_action)

    add_pick_walk("Up", "up")
    add_pick_walk("Down", "down")
    add_pick_walk("Left", "left")
    add_pick_walk("Right", "right")

    from .actions.panels_menu import fill_panels_menu

    # late until we have dynamic update method
    fill_panels_menu(menu_panels)
