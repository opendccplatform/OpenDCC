# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtWidgets, QtCore, QtGui
import opendcc.core as dcc_core
from opendcc.i18n import i18n
import pprint
import copy

from pxr import Sdf, Vt, Tf, Usd


class ExpressionVariableArray(QtWidgets.QWidget):
    def __init__(self, title, data_type, data, hide_column=False, parent=None):
        QtWidgets.QWidget.__init__(self, parent)
        self.setAutoFillBackground(True)
        layout = QtWidgets.QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.setLayout(layout)

        self.data_type = data_type
        self.data = data
        self.hide_column = hide_column

        style = """
.expression-variable-array-header {
    background: rgba(0, 0, 0, 40);
}
"""
        self.setStyleSheet(style)

        self.header = QtWidgets.QWidget()
        self.header.setAutoFillBackground(True)
        self.header.setProperty("class", "expression-variable-array-header")
        header_layout = QtWidgets.QHBoxLayout()
        header_layout.setContentsMargins(1, 1, 1, 1)
        header_layout.setSpacing(2)
        self.header.setLayout(header_layout)
        self.header.setSizePolicy(QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Minimum)
        self.setSizePolicy(QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Maximum)

        header_layout.addSpacing(6)
        if title:
            label = QtWidgets.QLabel(title)
            label.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
            header_layout.addWidget(label)

        self.type_string = QtWidgets.QLabel()
        self.type_string.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
        header_layout.addWidget(self.type_string)

        header_layout.addStretch()

        add_button = QtWidgets.QToolButton()
        add_button.setAutoRaise(True)
        add_button.setToolTip("Add")
        add_button.setIcon(QtGui.QIcon(":/icons/plus"))
        add_button.clicked.connect(self.add)
        header_layout.addWidget(add_button)

        self.table = QtWidgets.QTableWidget()
        self.table.setVerticalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff)
        self.table.setColumnCount(2)
        self.table.setHorizontalHeaderLabels(["Index", "Value"])

        if hide_column:
            self.table.horizontalHeader().hide()
            self.table.setColumnHidden(0, True)

        self.table.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)
        self.table.customContextMenuRequested.connect(self.open_menu)

        header = self.table.horizontalHeader()
        header.setStretchLastSection(True)
        header.resizeSection(0, 38)
        header.setSectionResizeMode(0, QtWidgets.QHeaderView.Fixed)
        self.table.verticalHeader().hide()

        layout.addWidget(self.header)
        layout.addWidget(self.table)
        layout.setAlignment(QtCore.Qt.AlignTop)

        self.build()

    def action(self, row, action):
        data_array = list(self.get_data())
        if action == "move_up":
            data_array[row - 1], data_array[row] = data_array[row], data_array[row - 1]
        elif action == "delete":
            del data_array[row]
        elif action == "move_down":
            data_array[row], data_array[row + 1] = data_array[row + 1], data_array[row]
        elif action == "clear_all":
            data_array = []

        if self.data_type == "string":
            self.data = Vt.StringArray(data_array)
        elif self.data_type == "int":
            self.data = Vt.IntArray(data_array)
        elif self.data_type == "bool":
            self.data = Vt.BoolArray(data_array)
        self.build()

    def open_menu(self, position):
        menu = QtWidgets.QMenu()

        item_under_cursor = self.table.itemAt(position)
        if item_under_cursor:
            row = item_under_cursor.row()
            row_count = self.table.rowCount()
            if row != 0:
                menu.addAction("Move Up", lambda row=row: self.action(row, "move_up"))
            menu.addAction("Delete", lambda row=row: self.action(row, "delete"))
            if row != row_count - 1:
                menu.addAction("Move Down", lambda row=row: self.action(row, "move_down"))
        menu.addSeparator()
        menu.addAction("Clear All", lambda row=-1: self.action(row, "clear_all"))

        menu.exec_(self.table.viewport().mapToGlobal(position))

    def add(self):
        data = self.get_data()
        data = list(data)
        if self.data_type == "string":
            data.append("")
            self.data = Vt.StringArray(data)
        elif self.data_type == "int":
            data.append(0)
            self.data = Vt.IntArray(data)
        elif self.data_type == "bool":
            data.append(False)
            self.data = Vt.BoolArray(data)
        self.build()

    def build(self):
        if self.data_type == "string":
            self.type_string.setText(
                '<b><font color="#5f5f5f">string[%d]</font></b>' % len(self.data)
            )
        elif self.data_type == "int":
            self.type_string.setText('<b><font color="#5f5f5f">int[%d]</font></b>' % len(self.data))
        elif self.data_type == "bool":
            self.type_string.setText(
                '<b><font color="#5f5f5f">bool[%d]</font></b>' % len(self.data)
            )
        self.table.clearContents()
        num_rows = len(self.data)
        self.table.setRowCount(num_rows)

        for index, value in enumerate(self.data):
            index_item = QtWidgets.QTableWidgetItem(str(index))
            index_item.setFlags(index_item.flags() & ~QtCore.Qt.ItemIsEditable)
            self.table.setItem(index, 0, index_item)

            if self.data_type == "string":
                self.table.setItem(index, 1, QtWidgets.QTableWidgetItem(value))
            elif self.data_type == "int":
                spin_box = QtWidgets.QSpinBox()
                spin_box.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
                spin_box.setMaximum(2147483647)
                spin_box.setMinimum(-2147483647)
                spin_box.setValue(value)
                self.table.setCellWidget(index, 1, spin_box)
            elif self.data_type == "bool":
                check_box = QtWidgets.QCheckBox()
                check_box.setChecked(value)
                self.table.setCellWidget(index, 1, check_box)

        table_height = self.table.horizontalHeader().height() if not self.hide_column else 0
        for index in range(num_rows):
            table_height += self.table.rowHeight(index)
        self.table.setFixedHeight(table_height)

    def get_data(self):
        if self.data_type == "string":
            data = []
            for row in range(self.table.rowCount()):
                item = self.table.item(row, 1)
                data.append(item.text())
            return Vt.StringArray(data)
        elif self.data_type == "int":
            data = []
            for row in range(self.table.rowCount()):
                widget = self.table.cellWidget(row, 1)
                data.append(widget.value())
            return Vt.IntArray(data)
        elif self.data_type == "bool":
            data = []
            for row in range(self.table.rowCount()):
                widget = self.table.cellWidget(row, 1)
                data.append(widget.isChecked())
            return Vt.BoolArray(data)


class ExpressionVariableContainer(QtWidgets.QWidget):
    def __init__(self, variable_name, variable_type, widgets, parent=None):
        QtWidgets.QWidget.__init__(self, parent)
        self.setAutoFillBackground(True)
        layout = QtWidgets.QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.setLayout(layout)

        self.widgets = widgets
        self.variable_name = variable_name
        self.variable_type = variable_type

        style = """
.expression-variable-container-header {
    background: rgba(0, 0, 0, 40);
}

.expression-variable-container-content {
    background: rgba(0, 0, 0, 15);
}
"""
        self.setStyleSheet(style)

        self.header = QtWidgets.QWidget()
        self.header.setAutoFillBackground(True)
        self.header.setProperty("class", "expression-variable-container-header")
        header_layout = QtWidgets.QHBoxLayout()
        header_layout.setContentsMargins(1, 1, 1, 1)
        header_layout.setSpacing(2)
        self.header.setLayout(header_layout)
        self.header.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Fixed)

        label = QtWidgets.QLabel(
            '<b>%s <font color="#5f5f5f">%s</font></b>' % (variable_name, variable_type)
        )
        label.setAlignment(QtCore.Qt.AlignLeft | QtCore.Qt.AlignVCenter)
        header_layout.addSpacing(6)
        header_layout.addWidget(label)
        header_layout.addStretch()
        self.delete_button = QtWidgets.QToolButton()
        self.delete_button.setAutoRaise(True)
        self.delete_button.setToolTip("Delete")
        self.delete_button.setIcon(QtGui.QIcon(":/icons/small_garbage"))
        header_layout.addWidget(self.delete_button)

        self.content = QtWidgets.QWidget()
        self.content.setAutoFillBackground(True)
        self.content.setProperty("class", "expression-variable-container-content")
        self.connect_layout = QtWidgets.QVBoxLayout()
        self.content.setLayout(self.connect_layout)

        for widget in widgets:
            self.connect_layout.addWidget(widget)

        layout.addWidget(self.header)
        layout.addWidget(self.content)
        layout.setAlignment(QtCore.Qt.AlignTop)

    def get_variable(self):
        if self.variable_type == "string":
            return self.variable_name, self.widgets[0].text()
        elif self.variable_type == "int":
            return self.variable_name, self.widgets[0].value()
        elif self.variable_type == "bool":
            return self.variable_name, self.widgets[0].isChecked()
        elif self.variable_type in ["string[]", "int[]", "bool[]"]:
            return self.variable_name, self.widgets[0].get_data()

    def get_metadata(self):
        if self.variable_type == "string":
            data = self.widgets[1].get_data()
            if len(data) == 0:
                return None
            return self.variable_name, data
        return None


class ExpressionVariablesEditor(QtWidgets.QDialog):
    """Editor for expression variables"""

    def __init__(self, variables, metadata, parent=None):
        QtWidgets.QDialog.__init__(self, parent)
        self.setWindowFlags(self.windowFlags() & ~QtCore.Qt.WindowContextHelpButtonHint)
        self.setWindowTitle(i18n("expression_variables.editor", "Expression Variables Editor"))
        self.setObjectName("Expression Variables Editor")
        self.setAttribute(QtCore.Qt.WA_DeleteOnClose)

        self.variables = copy.copy(variables)
        self.metadata = copy.copy(metadata)
        self.containers = []

        self.layout = QtWidgets.QVBoxLayout()
        self.layout.setContentsMargins(4, 4, 4, 4)
        self.layout.setSpacing(4)

        header_layout = QtWidgets.QHBoxLayout()

        header_layout.addWidget(QtWidgets.QLabel("Type:"))
        self.variable_type = QtWidgets.QComboBox()
        self.variable_type.addItem("string")
        self.variable_type.addItem("int")
        self.variable_type.addItem("bool")
        self.variable_type.addItem("string[]")
        self.variable_type.addItem("int[]")
        self.variable_type.addItem("bool[]")
        header_layout.addWidget(self.variable_type)
        header_layout.addSpacing(6)
        header_layout.addWidget(QtWidgets.QLabel("Name:"))
        self.variable_name = QtWidgets.QLineEdit()
        header_layout.addWidget(self.variable_name)
        self.plus_button = QtWidgets.QToolButton()
        self.plus_button.setToolTip("Add Variable")
        self.plus_button.setIcon(QtGui.QIcon(":/icons/plus"))
        self.plus_button.clicked.connect(self.add_variable)
        header_layout.addWidget(self.plus_button)
        self.clear_all_button = QtWidgets.QToolButton()
        self.clear_all_button.setText("Clear All")
        self.clear_all_button.setToolButtonStyle(QtCore.Qt.ToolButtonTextBesideIcon)
        self.clear_all_button.setIcon(QtGui.QIcon(":/icons/small_garbage"))
        self.clear_all_button.clicked.connect(self.clear_all)
        header_layout.addWidget(self.clear_all_button)

        self.layout.addLayout(header_layout)

        scroll_area = QtWidgets.QScrollArea()
        scroll_area.setWidgetResizable(True)
        scroll_area_widget = QtWidgets.QWidget(self)
        self.scroll_area_widget_layout = QtWidgets.QVBoxLayout()
        self.scroll_area_widget_layout.addStretch()
        scroll_area_widget.setLayout(self.scroll_area_widget_layout)
        scroll_area.setWidget(scroll_area_widget)
        self.layout.addWidget(scroll_area)

        buttons_layout = QtWidgets.QHBoxLayout()
        set_button = QtWidgets.QPushButton(i18n("expression_variables.editor", "Set"))
        cancel_button = QtWidgets.QPushButton(i18n("expression_variables.editor", "Cancel"))
        buttons_layout.addStretch()
        buttons_layout.addWidget(set_button)
        buttons_layout.addWidget(cancel_button)
        self.layout.addLayout(buttons_layout)

        self.setLayout(self.layout)

        cancel_button.clicked.connect(self.close)
        set_button.clicked.connect(self.set)

        self.build()

        self.resize(380, 420)

    def add_variable(self):
        variable_name = self.variable_name.text().strip()
        if variable_name == "":
            return
        selected_type = self.variable_type.currentText()

        variables, metadata = self.get_data()

        if selected_type == "string":
            variables[variable_name] = ""
        elif selected_type == "int":
            variables[variable_name] = 0
        elif selected_type == "bool":
            variables[variable_name] = False
        elif selected_type == "string[]":
            variables[variable_name] = Vt.StringArray([""])
        elif selected_type == "int[]":
            variables[variable_name] = Vt.IntArray([0])
        elif selected_type == "bool[]":
            variables[variable_name] = Vt.BoolArray([True])

        self.variables = variables
        self.metadata = metadata

        self.build()

    def clear_all(self):
        self.variables = {}
        self.metadata = {}
        self.clear()

    def clear(self):
        for container in self.containers:
            container.deleteLater()
        self.containers = []

    def build(self):

        self.clear()

        for variable in sorted(self.variables):
            value = self.variables[variable]
            widgets = []
            if isinstance(value, Vt.StringArray):
                type_string = "string[]"
                array_widget = ExpressionVariableArray(None, "string", value)
                widgets.append(array_widget)
            elif isinstance(value, Vt.IntArray):
                type_string = "int[]"
                array_widget = ExpressionVariableArray(None, "int", value)
                widgets.append(array_widget)
            elif isinstance(value, Vt.BoolArray):
                type_string = "bool[]"
                array_widget = ExpressionVariableArray(None, "bool", value)
                widgets.append(array_widget)
            elif isinstance(value, str):
                type_string = "string"
                line_edit = QtWidgets.QLineEdit()
                line_edit.setText(value)
                widgets.append(line_edit)
                data = []
                if variable in self.metadata:
                    data = self.metadata[variable]["options"]
                array_widget = ExpressionVariableArray("Options", "string", data, True)
                widgets.append(array_widget)
            elif isinstance(value, bool):
                type_string = "bool"
                line_edit = QtWidgets.QCheckBox()
                line_edit.setChecked(value)
                widgets.append(line_edit)
            elif isinstance(value, int):
                type_string = "int"
                spin_box = QtWidgets.QSpinBox()
                spin_box.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
                spin_box.setMaximum(2147483647)
                spin_box.setMinimum(-2147483647)
                spin_box.setValue(value)
                widgets.append(spin_box)

            container = ExpressionVariableContainer(variable, type_string, widgets)
            self.scroll_area_widget_layout.insertWidget(
                self.scroll_area_widget_layout.count() - 1, container
            )
            self.containers.append(container)
            container.delete_button.clicked.connect(
                lambda garbage=None, c=container: self.delete_container(c)
            )

    def delete_container(self, container):
        self.containers.remove(container)
        container.deleteLater()

    def get_data(self):
        variables = {}
        metadata = {}

        for container in self.containers:
            variable = container.get_variable()
            variables[variable[0]] = variable[1]
            meta = container.get_metadata()
            if meta:
                metadata[meta[0]] = {"options": meta[1]}

        return variables, metadata

    def set(self):
        variables, metadata = self.get_data()

        app = dcc_core.Application.instance()
        stage = app.get_session().get_current_stage()
        stage.SetMetadata("expressionVariablesMetadata", metadata)
        layer = stage.GetRootLayer()
        layer.expressionVariables = variables
        self.close()


class ExpressionVariablesToolbar:
    def __init__(self):
        self.widgets = []
        self.variables = {}
        self.variablesMetadata = {}

        self.toolbar = QtWidgets.QToolBar(
            i18n("expression_variables_toolbar", "Expression Variables")
        )
        self.toolbar.setObjectName("expression_variables_toolbar")
        self.toolbar.setProperty("opendcc_toolbar", True)
        self.toolbar.setProperty("opendcc_toolbar_side", "top")
        self.toolbar.setProperty("opendcc_toolbar_row", 0)
        self.toolbar.setProperty("opendcc_toolbar_index", 6)

        self.toolbar.setIconSize(QtCore.QSize(20, 20))

        self.button = QtWidgets.QToolButton()
        self.button.setIcon(QtGui.QIcon(":icons/function"))
        self.button.clicked.connect(self.editor)

        self.toolbar.addWidget(self.button)

        main_win = dcc_core.Application.instance().get_main_window()
        main_win.addToolBar(QtCore.Qt.TopToolBarArea, self.toolbar)

        self.app = dcc_core.Application.instance()
        self.stage = self.app.get_session().get_current_stage()
        self.add_callbacks()
        self._listeners = None

        self.build()

    def editor(self):
        editor = ExpressionVariablesEditor(self.variables, self.variablesMetadata)
        editor.exec_()

    def add_callbacks(self):
        self.stage_changed_callback_id = self.app.register_event_callback(
            dcc_core.Application.EventType.CURRENT_STAGE_CHANGED, self.build
        )

    def remove_callbacks(self):
        if self.stage_changed_callback_id:
            self.app.unregister_event_callback(
                dcc_core.Application.EventType.CURRENT_STAGE_CHANGED, self.stage_changed_callback_id
            )
            self.stage_changed_callback_id = None

    def set_values(self):
        variables = {}
        variablesMetadata = copy.copy(self.variablesMetadata)

        for widget in self.widgets:
            variable_type = widget.property("expression_variables_type")

            if not variable_type:
                continue

            variable_name = widget.property("expression_variables_variable")
            if variable_type == "string":
                variables[variable_name] = widget.text()
            elif variable_type == "bool":
                variables[variable_name] = widget.isChecked()
            elif variable_type == "int":
                variables[variable_name] = widget.value()
            elif variable_type == "string_options":
                variable_value = widget.currentText()
                variables[variable_name] = variable_value
                if variable_name in variablesMetadata:
                    options_data = variablesMetadata[variable_name]["options"]
                    options_list = list(options_data)
                    if variable_value not in options_list:
                        options_list.append(variable_value)
                        variablesMetadata[variable_name]["options"] = Vt.StringArray(options_list)
            elif variable_type == "string_array":
                variables[variable_name] = widget.property("expression_variables_value")
            elif variable_type == "int_array":
                variables[variable_name] = widget.property("expression_variables_value")
            elif variable_type == "bool_array":
                variables[variable_name] = widget.property("expression_variables_value")

        layer = self.stage.GetRootLayer()
        if layer.expressionVariables != variables:
            layer.expressionVariables = variables

        if variablesMetadata != self.variablesMetadata:
            self.stage.SetMetadata("expressionVariablesMetadata", variablesMetadata)

    def add_spacer(self):
        spacer = QtWidgets.QWidget()
        spacer.setFixedWidth(8)
        self.add_widget(spacer)

    def build(self):
        self.stage = self.app.get_session().get_current_stage()

        if self._listeners:
            self._listeners.Revoke()
            self._listeners = None

        if not self.stage:
            self.clear()
            self.button.setEnabled(False)
            return

        self.button.setEnabled(True)

        variablesMetadata = self.stage.GetMetadata("expressionVariablesMetadata")
        layer = self.stage.GetRootLayer()

        if (
            self.variables != layer.expressionVariables
            or self.variablesMetadata != variablesMetadata
        ):
            self.clear()

            self.variables = layer.expressionVariables
            self.variablesMetadata = variablesMetadata
            metrics = QtGui.QFontMetrics(self.toolbar.font())

            for variable in sorted(self.variables):
                value = self.variables[variable]
                widget = QtWidgets.QLabel(variable + ":")
                self.add_widget(widget)

                if isinstance(value, Vt.StringArray):
                    value_widget = QtWidgets.QLabel()
                    value_widget.setStyleSheet("QLabel { color: white }")
                    value_widget.setText(metrics.elidedText(str(value), QtCore.Qt.ElideRight, 80))
                    value_widget.setProperty("expression_variables_type", "string_array")
                    value_widget.setProperty("expression_variables_variable", variable)
                    value_widget.setProperty("expression_variables_value", value)
                    value_widget.setToolTip(
                        '<b>%s <font color="#5f5f5f">string[%d]</font></b><br>%s'
                        % (variable, len(value), pprint.pformat(list(value), width=40))
                    )
                    self.add_widget(value_widget)
                    self.add_spacer()
                elif isinstance(value, Vt.IntArray):
                    value_widget = QtWidgets.QLabel()
                    value_widget.setStyleSheet("QLabel { color: white }")
                    value_widget.setText(metrics.elidedText(str(value), QtCore.Qt.ElideRight, 80))
                    value_widget.setProperty("expression_variables_type", "int_array")
                    value_widget.setProperty("expression_variables_variable", variable)
                    value_widget.setProperty("expression_variables_value", value)
                    value_widget.setToolTip(
                        '<b>%s <font color="#5f5f5f">int[%d]</font></b><br>%s'
                        % (variable, len(value), pprint.pformat(list(value), width=40))
                    )
                    self.add_widget(value_widget)
                    self.add_spacer()
                elif isinstance(value, Vt.BoolArray):
                    value_widget = QtWidgets.QLabel()
                    value_widget.setStyleSheet("QLabel { color: white }")
                    value_widget.setText(metrics.elidedText(str(value), QtCore.Qt.ElideRight, 80))
                    value_widget.setProperty("expression_variables_type", "bool_array")
                    value_widget.setProperty("expression_variables_variable", variable)
                    value_widget.setProperty("expression_variables_value", value)
                    value_widget.setToolTip(
                        '<b>%s <font color="#5f5f5f">bool[%d]</font></b><br>%s'
                        % (variable, len(value), pprint.pformat(list(value), width=40))
                    )
                    self.add_widget(value_widget)
                    self.add_spacer()
                elif isinstance(value, str):
                    if variable in self.variablesMetadata:
                        value_widget = QtWidgets.QComboBox()
                        value_widget.setEditable(True)
                        for item in self.variablesMetadata[variable]["options"]:
                            value_widget.addItem(item)
                        value_widget.setCurrentText(value)
                        value_widget.setFixedWidth(max(metrics.horizontalAdvance(value) + 34, 80))

                        def check_value(widget, value):
                            if widget.currentText() != value:
                                self.set_values()

                        value_widget.lineEdit().editingFinished.connect(
                            lambda widget=value_widget, value=value: check_value(widget, value)
                        )
                        value_widget.currentIndexChanged.connect(
                            lambda index, widget=value_widget, value=value: check_value(
                                widget, value
                            )
                        )
                        value_widget.setProperty("expression_variables_type", "string_options")
                        value_widget.setProperty("expression_variables_variable", variable)
                        value_widget.setToolTip(
                            '<b>%s <font color="#5f5f5f">string</font></b><br>%s'
                            % (variable, value)
                        )
                        self.add_widget(value_widget)
                    else:
                        value_widget = QtWidgets.QLineEdit()
                        value_widget.setText(value)
                        value_widget.setFixedWidth(max(metrics.horizontalAdvance(value) + 12, 80))
                        value_widget.editingFinished.connect(self.set_values)
                        value_widget.setProperty("expression_variables_type", "string")
                        value_widget.setProperty("expression_variables_variable", variable)
                        value_widget.setToolTip(
                            '<b>%s <font color="#5f5f5f">string</font></b><br>%s'
                            % (variable, value)
                        )
                        self.add_widget(value_widget)
                    self.add_spacer()
                elif isinstance(value, bool):
                    value_widget = QtWidgets.QCheckBox()
                    value_widget.setChecked(value)
                    value_widget.stateChanged.connect(self.set_values)
                    value_widget.setProperty("expression_variables_type", "bool")
                    value_widget.setProperty("expression_variables_variable", variable)
                    value_widget.setToolTip(
                        '<b>%s <font color="#5f5f5f">bool</font></b><br>%s' % (variable, str(value))
                    )
                    self.add_widget(value_widget)
                    self.add_spacer()
                elif isinstance(value, int):
                    value_widget = QtWidgets.QSpinBox()
                    value_widget.setButtonSymbols(QtWidgets.QAbstractSpinBox.NoButtons)
                    value_widget.setFixedWidth(70)
                    value_widget.setMaximum(2147483647)
                    value_widget.setMinimum(-2147483647)
                    value_widget.setValue(value)
                    value_widget.valueChanged.connect(self.set_values)
                    value_widget.setProperty("expression_variables_type", "int")
                    value_widget.setProperty("expression_variables_variable", variable)
                    value_widget.setToolTip(
                        '<b>%s <font color="#5f5f5f">int</font></b><br>%s' % (variable, str(value))
                    )
                    self.add_widget(value_widget)
                    self.add_spacer()

        self._listeners = Tf.Notice.Register(
            Usd.Notice.ObjectsChanged, self._OnObjectsChanged, self.stage
        )

    def check_notice(self, notice):
        for path in notice.GetResyncedPaths():
            if path == Sdf.Path("/"):  # can't just check the fields it seems
                return True
        for path in notice.GetChangedInfoOnlyPaths():
            if path == Sdf.Path("/"):
                for token in notice.GetChangedFields(path):
                    if token in ["expressionVariables", "expressionVariablesMetadata"]:
                        return True
        return False

    def _OnObjectsChanged(self, notice, sender):
        if self.check_notice(notice):
            self.build()

    def add_widget(self, widget):
        self.toolbar.addWidget(widget)
        self.widgets.append(widget)

    def clear(self):
        for widget in self.widgets:
            widget.deleteLater()
        self.widgets = []
        self.variables = {}
        self.variablesMetadata = {}

    def cleanup(self):
        self.remove_callbacks()
        if self._listeners:
            self._listeners.Revoke()
            self._listeners = None
        self.toolbar.deleteLater()

    def free(self):
        self.clear()
        self.cleanup()
