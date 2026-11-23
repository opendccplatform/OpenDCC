# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
import opendcc.core as dcc_core
from opendcc import cmds
from opendcc.ui.scene_outliner import SceneOutliner
from ._usd_outliner import UsdStageDataSource, UsdSelectionContext


class UsdOutliner(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._app = dcc_core.Application.instance()
        self.outliner = SceneOutliner(UsdStageDataSource(), UsdSelectionContext(), self)
        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.outliner)

        view = self.outliner.tree_view()
        view.setProperty("unfocusedKeyEvent_enable", True)
        view.installEventFilter(self)
        self._shortcuts = [
            (QtGui.QKeySequence("F"), view.reveal_selection),
            (QtGui.QKeySequence.Delete, self._delete_selected),
            (QtGui.QKeySequence.Copy, lambda: cmds.copy_prims(self._selected_paths())),
            (QtGui.QKeySequence.Cut, lambda: cmds.cut_prims(self._selected_paths())),
            (QtGui.QKeySequence.Paste, lambda: cmds.paste_prims("/")),
        ]
        self.outliner.context_menu_requested.connect(self._populate_context_menu)
        view.header_menu_requested.connect(self._populate_header_menu)
        self._show_menu = self.outliner.menu_bar().addMenu(self.tr("Show"))
        self._show_menu.aboutToShow.connect(self._populate_show_menu)
        self.outliner.filters_changed.connect(self._populate_show_menu)
        self._populate_show_menu()

    def eventFilter(self, watched, event):
        if event.type() not in (QtCore.QEvent.ShortcutOverride, QtCore.QEvent.KeyPress):
            return False
        modifiers = getattr(event.modifiers(), "value", event.modifiers())
        key = QtGui.QKeySequence(event.key() | int(modifiers))
        matching = []
        for sequence, callback in self._shortcuts:
            matches = (
                event.matches(sequence)
                if isinstance(sequence, QtGui.QKeySequence.StandardKey)
                else key == sequence
            )
            if matches:
                matching.append(callback)
        if not matching:
            return False
        event.accept()
        if event.type() == QtCore.QEvent.KeyPress and len(matching) == 1:
            matching[0]()
        return True

    def _selected_paths(self):
        return list(self._app.get_prim_selection())

    def _current_stage(self):
        session = self._app.get_session()
        return session.get_current_stage() if session else None

    def _delete_selected(self):
        paths = self._selected_paths()
        if paths:
            cmds.remove_prims(paths)

    def _populate_context_menu(self, menu):
        outliner = self.outliner
        paths = self._selected_paths()
        if not paths:
            return
        stage = self._current_stage()
        source = outliner.data_source()

        def is_current():
            return self._current_stage() == stage and outliner.data_source() is source

        def add_action(text, callback):
            def invoke():
                if is_current():
                    callback()

            return menu.addAction(text, invoke)

        def copy_paths():
            QtGui.QGuiApplication.clipboard().setText("\n".join(str(p) for p in paths))

        add_action("Copy Prim Path", copy_paths)

        if len(paths) == 1:
            add_action("Rename", outliner.edit_current)

        if not stage:
            return

        menu.addSeparator()

        add_action("Duplicate", lambda: cmds.duplicate_prim(paths=paths))
        add_action("Group", lambda: cmds.group_prim(paths=paths))

        menu.addSeparator()
        add_action("Copy", lambda: cmds.copy_prims(paths))
        add_action("Cut", lambda: cmds.cut_prims(paths))
        add_action("Paste", lambda: cmds.paste_prims(paths[0] if len(paths) == 1 else "/"))

        if len(paths) == 1:
            prim = stage.GetPrimAtPath(paths[0])
            if prim and prim.HasVariantSets():
                self._add_variants_submenu(menu, prim, is_current)

        menu.addSeparator()

        add_action("Make Visible", lambda: cmds.set_prim_visibility(paths, True))
        add_action("Make Invisible", lambda: cmds.set_prim_visibility(paths, False))

        first = stage.GetPrimAtPath(paths[0])
        is_active = bool(first) and first.IsActive()
        add_action(
            "Deactivate" if is_active else "Activate",
            lambda: cmds.set_prim_active(paths, not is_active),
        )

        menu.addSeparator()
        add_action("Delete", lambda: cmds.remove_prims(paths))

    def _add_variants_submenu(self, menu, prim, is_current=None):
        stage = prim.GetStage()
        path = prim.GetPath()

        def set_variant(variant_set, value):
            if self._current_stage() == stage and (is_current is None or is_current()):
                cmds.set_variant_selection(path, variant_set.GetName(), value)

        variants_menu = QtWidgets.QMenu("Variants", menu)
        menu.addMenu(variants_menu)
        variant_sets = prim.GetVariantSets()
        for set_name in variant_sets.GetNames():
            variant_set = variant_sets.GetVariantSet(set_name)
            current = variant_set.GetVariantSelection()
            submenu = QtWidgets.QMenu(set_name, variants_menu)
            variants_menu.addMenu(submenu)

            none_action = submenu.addAction("<No Variant Selected>")
            none_action.setCheckable(True)
            none_action.setChecked(not current)
            none_action.triggered.connect(lambda checked=False, vs=variant_set: set_variant(vs, ""))
            submenu.addSeparator()

            for name in variant_set.GetVariantNames():
                action = submenu.addAction(name)
                action.setCheckable(True)
                action.setChecked(name == current)
                action.triggered.connect(
                    lambda checked=False, vs=variant_set, n=name: set_variant(vs, n)
                )

    def _populate_show_menu(self):
        menu = self._show_menu
        menu.clear()
        action = menu.addAction(self.tr("Pseudo-root"))
        action.setCheckable(True)
        action.setChecked(self.outliner.tree_view().shows_root())
        action.toggled.connect(lambda shown: self.outliner.tree_view().set_show_root(shown))
        source = self.outliner.data_source()
        filters = source.filters() if source else []
        if filters:
            menu.addSeparator()
        for category in filters:
            action = menu.addAction(category.label)
            action.setCheckable(True)
            action.setChecked(self.outliner.filter_shown(category.id))
            action.toggled.connect(
                lambda shown, key=category.id: self.outliner.set_filter_shown(key, shown)
            )

    def _populate_header_menu(self, menu):
        model = self.outliner.model()
        view = self.outliner.tree_view()
        source = self.outliner.data_source()

        def set_column_shown(shown, column_id):
            if self.outliner.model() is not model or self.outliner.data_source() is not source:
                return
            for number, column in enumerate(model.columns()):
                if column.id == column_id:
                    if number != model.tree_column():
                        view.setColumnHidden(number, not shown)
                    return

        for number, column in enumerate(model.columns()):
            action = menu.addAction(column.title or column.id)
            if column.header_icon:
                action.setIcon(QtGui.QIcon(column.header_icon))
            action.setCheckable(True)
            action.setChecked(not view.isColumnHidden(number))
            action.setEnabled(number != model.tree_column())
            action.toggled.connect(
                lambda shown, column_id=column.id: set_column_shown(shown, column_id)
            )
