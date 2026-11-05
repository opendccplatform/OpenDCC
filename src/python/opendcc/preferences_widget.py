# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui
from opendcc.preferences.common_page import CommonPage
from opendcc.preferences.def_cam_page import DefCamPage
from opendcc.preferences.undo_page import UndoPage
from opendcc.preferences.viewport_page import ViewportPage
from opendcc.preferences.color_management_page import ColorManagementPage
from opendcc.preferences.render_page import RenderPage
from opendcc.preferences.color_theme_page import ColorThemePage
import opendcc.core as dcc_core
import sys
from opendcc.common_widgets import RolloutWidget, DoubleWidget
from .i18n import i18n


class PreferencePageFactory:
    _pages = {}

    @staticmethod
    def register_preference_page(name, create_fn):
        PreferencePageFactory._pages[name] = create_fn

    @staticmethod
    def unregister_preference_page(name):
        del PreferencePageFactory._pages[name]

    @staticmethod
    def make_preference_pages():
        return [v() for _, v in sorted(PreferencePageFactory._pages.items())]


class TimelinePage(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super(TimelinePage, self).__init__(parent=parent)
        self.setLayout(QtWidgets.QVBoxLayout())
        self.setObjectName(i18n("preferences.timeline", "Timeline"))
        rollout = RolloutWidget(i18n("preferences.timeline", "Timeline"), expandable=False)
        self.snap_ui = QtWidgets.QCheckBox()

        self.playback_mode = QtWidgets.QComboBox()

        timeline_every_frame = i18n("preferences.timeline", "Play Every Frame")
        timeline_realtime = i18n("preferences.timeline", "Real-time")
        self.playback_mode.addItems([timeline_every_frame, timeline_realtime])
        playback_mode_layout = QtWidgets.QHBoxLayout()
        playback_mode_layout.addWidget(self.playback_mode)
        playback_mode_layout.addStretch()

        self.playback_by = DoubleWidget()
        self.playback_by.set_range(0.001, 10000)
        self.playback_by.setFixedWidth(80)
        form = QtWidgets.QFormLayout()
        form.addRow(i18n("preferences.timeline", "Snap:"), self.snap_ui)
        form.addRow(i18n("preferences.timeline", "Playback Mode:"), playback_mode_layout)
        form.addRow(i18n("preferences.timeline", "Playback by:"), self.playback_by)
        self.snap_key = "timeline.snap"
        self.playback_mode_key = "timeline.playback_mode"
        self.playback_by_key = "timeline.playback_by"
        rollout.set_layout(form)

        appearance_rollout = RolloutWidget(
            i18n("preferences.timeline", "Appearance"), expandable=False
        )

        self.cursor_ui = QtWidgets.QComboBox()
        self.cursor_ui.addItems(
            [i18n("preferences.timeline", "Rect"), i18n("preferences.timeline", "Arrow")]
        )
        cursor_layout = QtWidgets.QHBoxLayout()
        cursor_layout.addWidget(self.cursor_ui)
        cursor_layout.addStretch()
        self.cursor_key = "timeline.current_time_indicator"

        self.keyframe_ui = QtWidgets.QComboBox()
        self.keyframe_ui.addItems(
            [
                i18n("preferences.timeline", "Line"),
                i18n("preferences.timeline", "Rect"),
                i18n("preferences.timeline", "Arrow"),
            ]
        )
        keyframe_layout = QtWidgets.QHBoxLayout()
        keyframe_layout.addWidget(self.keyframe_ui)
        keyframe_layout.addStretch()
        self.keyframe_key = "timeline.keyframe_display_type"

        self.background_ui = QtWidgets.QComboBox()
        self.background_ui.addItems(
            [
                i18n("preferences.timeline", "Striped"),
                i18n("preferences.timeline", "Flat"),
            ]
        )
        background_layout = QtWidgets.QHBoxLayout()
        background_layout.addWidget(self.background_ui)
        background_layout.addStretch()
        self.background_key = "timeline.background_type"

        self.subdivisions_ui = QtWidgets.QCheckBox()
        subdivisions_layout = QtWidgets.QHBoxLayout()
        subdivisions_layout.addWidget(self.subdivisions_ui)
        subdivisions_layout.addStretch()
        self.subdivisions_key = "timeline.subdivisions_type"

        appearance_form = QtWidgets.QFormLayout()
        appearance_form.addRow(i18n("preferences.timeline", "Cursor:"), cursor_layout)
        appearance_form.addRow(i18n("preferences.timeline", "Keyframe:"), keyframe_layout)
        appearance_form.addRow(i18n("preferences.timeline", "Background:"), background_layout)
        appearance_form.addRow(i18n("preferences.timeline", "Subdivisions:"), subdivisions_layout)

        appearance_rollout.set_layout(appearance_form)

        self.layout().setContentsMargins(0, 0, 0, 0)
        self.layout().addWidget(rollout)
        self.layout().addWidget(appearance_rollout)

        self.layout().addStretch()

        settings = dcc_core.Application.instance().get_settings()
        self.snap_value_old = settings.get_bool(self.snap_key, True)
        self.playback_mode_old = settings.get_int(self.playback_mode_key, 1)
        self.playback_by_old = settings.get_double(self.playback_by_key, 1.0)

        self.cursor_old = settings.get_int(self.cursor_key, 0)
        self.keyframe_old = settings.get_int(self.keyframe_key, 0)
        self.background_old = settings.get_int(self.background_key, 0)
        self.subdivisions_old = settings.get_bool(self.subdivisions_key, True)

        self.restore_previous_settings()
        self.snap_ui.stateChanged.connect(self.set_snap_value_mode)
        self.playback_mode.currentIndexChanged.connect(self.set_playback_mode)
        self.playback_by.value_changed.connect(self.set_playback_by)
        self.cursor_ui.currentIndexChanged.connect(self.set_cursor)
        self.keyframe_ui.currentIndexChanged.connect(self.set_keyframe)
        self.background_ui.currentIndexChanged.connect(self.set_background)
        self.subdivisions_ui.stateChanged.connect(self.set_subdivisions)

    def set_snap_value_mode(self, state):
        value = True if state == QtCore.Qt.Checked else False
        settings = dcc_core.Application.instance().get_settings()
        settings.set_bool(self.snap_key, value)

    def set_playback_mode(self, value):
        settings = dcc_core.Application.instance().get_settings()
        settings.set_int(self.playback_mode_key, value)
        self.playback_by.setEnabled(value == 0)

    def set_playback_by(self, value):
        settings = dcc_core.Application.instance().get_settings()
        settings.set_double(self.playback_by_key, value)

    def set_cursor(self, value):
        settings = dcc_core.Application.instance().get_settings()
        settings.set_int(self.cursor_key, value)

    def set_keyframe(self, value):
        settings = dcc_core.Application.instance().get_settings()
        settings.set_int(self.keyframe_key, value)

    def set_background(self, value):
        settings = dcc_core.Application.instance().get_settings()
        settings.set_int(self.background_key, value)

    def set_subdivisions(self, state):
        value = True if state == QtCore.Qt.Checked else False
        settings = dcc_core.Application.instance().get_settings()
        settings.set_bool(self.subdivisions_key, value)

    def save_settings(self):
        self.snap_value_old = self.snap_ui.isChecked()
        self.playback_mode_old = self.playback_mode.currentIndex()
        self.playback_by_old = self.playback_by.get_value()
        self.cursor_old = self.cursor_ui.currentIndex()
        self.keyframe_old = self.keyframe_ui.currentIndex()
        self.background_old = self.background_ui.currentIndex()
        self.subdivisions_old = self.subdivisions_ui.isChecked()

        settings = dcc_core.Application.instance().get_settings()
        settings.set_bool(self.snap_key, self.snap_value_old)
        settings.set_int(self.playback_mode_key, self.playback_mode_old)
        settings.set_double(self.playback_by_key, self.playback_by_old)
        settings.set_int(self.cursor_key, self.cursor_old)
        settings.set_int(self.keyframe_key, self.keyframe_old)
        settings.set_int(self.background_key, self.background_old)
        settings.set_bool(self.subdivisions_key, self.subdivisions_old)

    def restore_previous_settings(self):
        self.snap_ui.setChecked(self.snap_value_old)
        self.playback_mode.setCurrentIndex(self.playback_mode_old)
        self.playback_by.set_value(self.playback_by_old)

        self.playback_by.setEnabled(self.playback_mode_old == 0)

        self.cursor_ui.setCurrentIndex(self.cursor_old)
        self.keyframe_ui.setCurrentIndex(self.keyframe_old)
        self.background_ui.setCurrentIndex(self.background_old)
        self.subdivisions_ui.setChecked(self.subdivisions_old)


class PreferencesWidget(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super(PreferencesWidget, self).__init__(parent=parent)
        self.setWindowFlags(QtCore.Qt.Window)
        self.setWindowTitle(i18n("preferences.window_title", "Preferences"))
        self.setLayout(QtWidgets.QVBoxLayout())
        self.resize(800, 600)
        self.list_widget = QtWidgets.QListWidget()
        self.list_widget.setMaximumWidth(140)
        self.pages_widget = QtWidgets.QStackedWidget()
        self.pages_widget.addWidget(CommonPage())
        self.pages_widget.addWidget(TimelinePage())
        self.pages_widget.addWidget(DefCamPage())
        self.pages_widget.addWidget(UndoPage())
        self.pages_widget.addWidget(ViewportPage())
        self.pages_widget.addWidget(ColorManagementPage())
        self.pages_widget.addWidget(RenderPage())

        if (
            dcc_core.Application.instance()
            .get_app_config()
            .get_bool("settings.ui.allow_color_theme_prefs", False)
        ):
            self.pages_widget.addWidget(ColorThemePage())

        for page in PreferencePageFactory.make_preference_pages():
            self.pages_widget.addWidget(page)

        for i in range(self.pages_widget.count()):
            widget = self.pages_widget.widget(i)
            item = QtWidgets.QListWidgetItem(self.list_widget)
            item.setText(widget.objectName())
            item.setFlags(item.flags() | QtCore.Qt.ItemIsSelectable)
        self.list_widget.setCurrentRow(0)
        self.pages_widget.setCurrentIndex(0)

        hlt = QtWidgets.QHBoxLayout()
        hlt.addWidget(self.list_widget)
        hlt.addWidget(self.pages_widget, 1)
        self.layout().addLayout(hlt)

        self.list_widget.itemSelectionChanged.connect(self.change_page)

        self.buttons_layout = QtWidgets.QHBoxLayout()
        self.layout().addLayout(self.buttons_layout)
        self.button_save = QtWidgets.QPushButton(i18n("preferences", "Save"))
        self.buttons_layout.addWidget(self.button_save)
        self.button_save.pressed.connect(self.save_settings)

        self.button_close = QtWidgets.QPushButton(i18n("preferences", "Close"))
        self.buttons_layout.addWidget(self.button_close)
        self.button_close.pressed.connect(self.restore_previous_settings)

    def change_page(self):
        for (
            model_index
        ) in self.list_widget.selectedIndexes():  # because currentRow() is somewhat bugged
            self.pages_widget.setCurrentIndex(model_index.row())

    def save_settings(self):
        for index in range(self.pages_widget.count()):
            widget = self.pages_widget.widget(index)
            widget.save_settings()
        dcc_core.Application.instance().get_ui_settings().sync()

    def restore_previous_settings(self):
        for index in range(self.pages_widget.count()):
            widget = self.pages_widget.widget(index)
            widget.restore_previous_settings()
        self.close()

    def closeEvent(self, event):
        self.restore_previous_settings()
        QtWidgets.QWidget.closeEvent(self, event)

    @staticmethod
    def show_window():
        window = PreferencesWidget(dcc_core.Application.instance().get_main_window())
        window.show()
