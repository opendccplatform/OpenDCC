# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from Qt import QtCore, QtGui, QtWidgets
import keyword
import ast
import re
import opendcc.core as dcc_core, pxr
import sys

from opendcc.pygments_utils.pygmentshighlighter import PygmentsHighlighter
from opendcc.pygments_utils.style import OpenDCCStyle, OpenDCCLightStyle
from opendcc.pygments_utils.usd_lexer import UsdLexer

usd_lexer = UsdLexer()
from pygments.lexers import get_lexer_by_name

from Qt import QtCore

from . import osl_lexer
from . import expand_vars_lexer

from opendcc.color_theme import get_color_theme, ColorTheme

from opendcc.i18n import i18n


class LineNumberArea(QtWidgets.QWidget):
    def __init__(self, editor):
        super(LineNumberArea, self).__init__(editor)
        self.codeEditor = editor

    def sizeHint(self):
        return QtCore.QSize(self.codeEditor.lineNumberAreaWidth(), 0)

    def paintEvent(self, event):
        """It Invokes the draw method(lineNumberAreaPaintEvent) in CodeEditor"""
        self.codeEditor.lineNumberAreaPaintEvent(event)


class FindReplaceWidget(QtWidgets.QWidget):
    def __init__(self, editor):
        super(FindReplaceWidget, self).__init__(editor)
        self.codeEditor = editor
        layout = QtWidgets.QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        first_row = QtWidgets.QHBoxLayout()
        first_row.setContentsMargins(0, 0, 0, 0)
        first_row.setSpacing(0)
        second_row = QtWidgets.QHBoxLayout()
        second_row.setContentsMargins(0, 0, 0, 0)
        second_row.setSpacing(0)
        self.find = QtWidgets.QLineEdit(self)
        self.find.setPlaceholderText("Find...")
        self.find.returnPressed.connect(self.do_find)
        self.replace = QtWidgets.QLineEdit(self)
        self.replace.setPlaceholderText("Replace...")
        self.replace.returnPressed.connect(self.do_replace)

        first_row.addWidget(self.find)
        first_row_toolbar = QtWidgets.QToolBar()
        # first_row_toolbar.setFixedHeight(28)
        first_row_toolbar.setIconSize(QtCore.QSize(16, 16))
        first_row_toolbar.addAction(QtGui.QIcon(":/icons/small_search"), "Find", self.do_find)
        first_row_toolbar.addAction(QtGui.QIcon(":/icons/close_dock"), "Close", self.close)
        first_row.addWidget(first_row_toolbar)

        second_row.addWidget(self.replace)
        second_row_toolbar = QtWidgets.QToolBar()
        # second_row_toolbar.setFixedHeight(28)
        second_row_toolbar.setIconSize(QtCore.QSize(16, 16))
        self.match_case = QtWidgets.QAction(QtGui.QIcon(":/icons/match_case"), "Match Case", self)
        self.match_case.setCheckable(True)
        self.match_case.setChecked(True)
        second_row_toolbar.addAction(self.match_case)

        self.re = QtWidgets.QAction(QtGui.QIcon(":/icons/re"), "Use Regex", self)
        self.re.setCheckable(True)
        self.re.setChecked(False)
        second_row_toolbar.addAction(self.re)
        second_row_toolbar.addSeparator()
        second_row_toolbar.addAction(QtGui.QIcon(":/icons/replace"), "Replace", self.do_replace)
        second_row_toolbar.addAction(
            QtGui.QIcon(":/icons/replace_all"), "Replace All", self.do_replace_all
        )
        second_row.addWidget(second_row_toolbar)

        layout.addLayout(first_row)
        layout.addLayout(second_row)

        self.setLayout(layout)

        self.hide_sc = QtWidgets.QShortcut(QtGui.QKeySequence("ctrl+f"), self.find)
        self.hide_sc.setContext(QtCore.Qt.WidgetShortcut)
        self.hide_sc.activated.connect(self.hide_action)

        dcc_core.Application.instance().register_event_callback(
            "ui_escape_key_action", self.hide_action
        )

    def hide_action(self):
        self.hide()
        self.parent().setFocus()

    def close(self):
        self.parent().hideFindReplace()

    def do_find(self):
        code_editor = self.parent().code_editor
        match_case = self.match_case.isChecked()
        regex = self.re.isChecked()
        find = self.find.text()
        flags = QtGui.QTextDocument.FindFlags()
        if match_case:
            flags = flags | QtGui.QTextDocument.FindCaseSensitively
        search_object = QtCore.QRegExp(find) if regex else find
        found = code_editor.find(search_object, flags)
        if found:
            return found
        # it needs to be searched again from the start of the document
        old_cursor = code_editor.textCursor()
        new_cursor = code_editor.textCursor()
        new_cursor.movePosition(QtGui.QTextCursor.Start, QtGui.QTextCursor.MoveAnchor)
        code_editor.setTextCursor(new_cursor)
        found = code_editor.find(search_object, flags)
        if found:
            return found
        else:
            code_editor.setTextCursor(old_cursor)
            return found

    def do_replace(self):
        if self.codeEditor.isReadOnly():
            return

        code_editor = self.parent().code_editor
        find = self.find.text()
        replace = self.replace.text()

        def pretty_insert(text):
            cursor = code_editor.textCursor()
            oldAnchor = cursor.anchor()
            oldPosition = cursor.position()
            cursor.insertText(text)
            if oldAnchor < oldPosition:
                newAnchor = oldAnchor
                newPosition = cursor.position()
            else:
                newAnchor = cursor.position()
                newPosition = oldPosition
            cursor.setPosition(newAnchor, QtGui.QTextCursor.MoveAnchor)
            cursor.setPosition(newPosition, QtGui.QTextCursor.KeepAnchor)
            code_editor.setTextCursor(cursor)

        cursor = code_editor.textCursor()
        if cursor.hasSelection():
            selected = cursor.selectedText()
            if selected == find and selected != replace:
                pretty_insert(replace)
                self.do_find()  # pretty insert turned out to be not that pretty after all
                return

        if self.do_find():
            pretty_insert(replace)

    def do_replace_all(self):
        # such is life
        if self.codeEditor.isReadOnly():
            return

        code_editor = self.parent().code_editor
        match_case = self.match_case.isChecked()
        regex = self.re.isChecked()
        find = self.find.text()
        replace = self.replace.text()

        code = code_editor.toPlainText()
        if regex:
            flags = []
            if not match_case:
                flags.append(re.IGNORECASE)
            pattern = re.compile(find, *flags)
            code = pattern.sub(replace, code)
        else:
            if match_case:
                code = code.replace(find, replace)
            else:  # yep...
                idx = 0
                while idx < len(code):
                    index_l = code.lower().find(find.lower(), idx)
                    if index_l == -1:
                        return code
                    code = code[:index_l] + replace + code[index_l + len(find) :]
                    idx = index_l + len(replace)

        # code_editor.setPlainText(code) # it breaks undo
        code_editor.selectAll()
        cursor = code_editor.textCursor()
        cursor.beginEditBlock()  # undo still looks horrible but works
        scrolled = code_editor.verticalScrollBar().value()
        cursor.insertText(code)
        code_editor.verticalScrollBar().setValue(scrolled)
        cursor.endEditBlock()


class CodeEditor(QtWidgets.QWidget):
    """
    Full Code Editor with Find/Replace things
    """

    __enable_autocomplete = (
        dcc_core.Application.instance()
        .get_settings()
        .get_bool("scripteditor.enable_autocomplete", True)
    )

    @staticmethod
    def getEnableAutocomplete():
        return CodeEditor.__enable_autocomplete

    @staticmethod
    def setEnableAutocomplete(value):
        CodeEditor.__enable_autocomplete = value

    def __init__(self, parent=None, *args, **kwargs):
        QtWidgets.QWidget.__init__(self, parent=parent)
        layout = QtWidgets.QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.code_editor = CodeEditorWidget(self, *args, **kwargs)
        self.find_replace = FindReplaceWidget(self.code_editor)
        self.find_replace.hide()
        layout.addWidget(self.code_editor)
        layout.addWidget(self.find_replace)
        self.setLayout(layout)

    def showFindReplace(self, text):
        # if self.find_replace.find.text() == text and not self.find_replace.isHidden():
        #      self.find_replace.hide()
        #      return # nah
        self.find_replace.find.setText(text)
        self.find_replace.show()
        self.find_replace.find.setFocus()

    def hideFindReplace(self):
        self.find_replace.hide()


class CodeEditorWidget(QtWidgets.QPlainTextEdit):
    """
    Code editor
    """

    enter_pressed = QtCore.Signal()
    editing_finished = QtCore.Signal()

    def __init__(
        self,
        parent=None,
        lang=None,
        highlight_line=False,
        highlight_selected=True,
        line_numbers=False,
        word_wrap=False,
    ):
        QtWidgets.QPlainTextEdit.__init__(self, parent=parent)

        if get_color_theme() == ColorTheme.LIGHT:
            self.style = OpenDCCLightStyle
        else:
            self.style = OpenDCCStyle

        self._changed = False
        self.textChanged.connect(self._handle_text_changed)

        self.TAB_SEQ = " " * 4
        self.prefix = ""
        self.keywords = set([word for word in keyword.kwlist if len(word) > 3])
        self.all_words = set()
        self.imported_names = {}
        self.define_completer()
        self.setWordWrap(word_wrap)
        self.locals = []

        self.lang = lang
        self.highlight_line = highlight_line
        self.highlight_selected = highlight_selected
        self.line_numbers = line_numbers
        self.comment = None

        self.palette = QtWidgets.QApplication.palette()
        self.base_color = self.palette.color(QtGui.QPalette.Base)

        if highlight_line:
            self.cursorPositionChanged.connect(self.highlightCurrentLine)
            self.highlightCurrentLine()

        if line_numbers:
            self.lineNumberArea = LineNumberArea(self)
            self.blockCountChanged.connect(self.updateLineNumberAreaWidth)
            self.updateRequest.connect(self.updateLineNumberArea)
            self.updateLineNumberAreaWidth()

        font = QtGui.QFont()
        font.setFamily("Courier")
        font.setFixedPitch(True)
        font.setPointSize(10)
        self.setFont(font)

        if lang == "python":
            self.comment = "#"
            self.highlighter = PygmentsHighlighter(self.document())
            self.highlighter.set_style(self.style)

            self.dir_sc = QtWidgets.QShortcut(QtGui.QKeySequence("F1"), self)
            self.dir_sc.setContext(QtCore.Qt.WidgetShortcut)
            self.dir_sc.activated.connect(self.print_dir)
        elif lang == "usd":
            self.comment = "#"
            self.highlighter = PygmentsHighlighter(self.document(), usd_lexer)
            self.highlighter.set_style(self.style)
        elif lang == "lua":
            self.comment = "--"
            self.TAB_SEQ = " " * 2
            self.highlighter = PygmentsHighlighter(self.document(), get_lexer_by_name(lang))
            self.highlighter.set_style(self.style)
        elif lang == "osl":
            self.comment = "//"
            self.highlighter = PygmentsHighlighter(self.document(), osl_lexer.OslLexer())
            self.highlighter.set_style(self.style)
        elif lang == "expand_vars":
            self.comment = "#"
            self.TAB_SEQ = " " * 4  # what
            self.highlighter = PygmentsHighlighter(
                self.document(), expand_vars_lexer.ExpandVarsLexer()
            )
            self.highlighter.set_style(self.style)

        self.findSc = QtWidgets.QShortcut(QtGui.QKeySequence("ctrl+f"), self)
        self.findSc.setContext(QtCore.Qt.WidgetShortcut)
        self.findSc.activated.connect(self.show_find)

        self.symbol_expression = re.compile(
            r"[\w/\.]+"
        )  # these symbols will fire autocomplete, I hope
        self.word_expression = re.compile(r"\w+$")
        self.code_expression = re.compile(r"(\w+\.)+(\w+)?$")
        self.sdf_expression = re.compile(r'[\'"<](/|(/\w+)+(/|\.|(\.\w+))?)$')
        self.sdf_attr_expression = re.compile(r"\.(\w*)$")

        self.complete_sc = QtWidgets.QShortcut(QtGui.QKeySequence("ctrl+space"), self)
        self.complete_sc.setContext(QtCore.Qt.WidgetShortcut)
        self.complete_sc.activated.connect(lambda: self.run_completer(None, force=True))

        self.execute_sc = QtWidgets.QShortcut(QtGui.QKeySequence("ctrl+return"), self)
        self.execute_sc.setContext(QtCore.Qt.WidgetShortcut)
        self.execute_sc.activated.connect(lambda: self.enter_pressed.emit())

        self.autocomment_sc = QtWidgets.QShortcut(QtGui.QKeySequence("ctrl+/"), self)
        self.autocomment_sc.setContext(QtCore.Qt.WidgetShortcut)
        self.autocomment_sc.activated.connect(self.autocomment)

    def focusOutEvent(self, event):
        if self._changed:
            self.editing_finished.emit()
        super(CodeEditorWidget, self).focusOutEvent(event)

    def _handle_text_changed(self):
        self._changed = True

    def setTextChanged(self, state=True):
        self._changed = state

    def autocomment(self):
        if self.comment and self.lineWrapMode() == QtWidgets.QPlainTextEdit.NoWrap:
            cursor = self.textCursor()
            cursor.beginEditBlock()

            if cursor.hasSelection():
                start = cursor.selectionStart()
                end = cursor.selectionEnd()
                cursor.setPosition(start)
                first_line = cursor.blockNumber()
                cursor.setPosition(end, QtGui.QTextCursor.KeepAnchor)
                last_line = cursor.blockNumber()
                line_numbers = []
                for line_number in range(first_line, last_line + 1):
                    block = self.document().findBlockByLineNumber(line_number)
                    if block.position() == end:
                        continue
                    text = block.text().strip()
                    if len(text):
                        line_numbers.append(line_number)

                already_commented = True
                for line_number in line_numbers:
                    block = self.document().findBlockByLineNumber(line_number)
                    if not re.search(r"^\s*" + self.comment, block.text()):
                        already_commented = False

                character_count = self.document().characterCount()
                first_char = character_count
                for line_number in line_numbers:
                    block = self.document().findBlockByLineNumber(line_number)
                    match = re.search(r"^\s*([^ \t\r\n\v\f])", block.text())
                    if match:
                        span = match.span(1)
                        if span[0] < first_char:
                            first_char = span[0]

                if first_char == character_count:
                    return  # just in case

                for line_number in line_numbers:
                    block = self.document().findBlockByLineNumber(line_number)
                    if not already_commented:
                        cursor.setPosition(block.position() + first_char)
                        cursor.insertText(self.comment + " ")
                    else:
                        cursor.setPosition(block.position())
                        cursor.select(QtGui.QTextCursor.LineUnderCursor)
                        text = cursor.selectedText()
                        match = re.search(r"^\s*(%s\s?)" % self.comment, text)
                        if match:
                            span = match.span(1)
                            text = text[: span[0]] + text[span[1] :]
                            cursor.insertText(text)

                start_block = self.document().findBlockByLineNumber(line_numbers[0])
                last_line = line_numbers[-1] + 1
                line_count = self.document().lineCount() - 1
                if last_line > line_count:
                    last_line = line_count
                end_block = self.document().findBlockByLineNumber(last_line)
                cursor.setPosition(start_block.position())
                cursor.setPosition(end_block.position(), QtGui.QTextCursor.KeepAnchor)
                self.setTextCursor(cursor)
            else:
                start = cursor.position()
                cursor.select(QtGui.QTextCursor.LineUnderCursor)

                text = cursor.selectedText()
                if re.search(r"^\s*" + self.comment, text):
                    match = re.search(r"^\s*(%s\s?)" % self.comment, text)
                    if match:
                        span = match.span(1)
                        text = text[: span[0]] + text[span[1] :]
                        cursor.insertText(text)
                        cursor.setPosition(start - len(match.group(1)))
                        self.setTextCursor(cursor)
                else:
                    match = re.search(r"^\s*(\w)", text)
                    if match:
                        span = match.span(1)
                        text = text[: span[0]] + self.comment + " " + text[span[0] :]
                        cursor.insertText(text)
                        cursor.setPosition(start + len(self.comment) + 1)
                        self.setTextCursor(cursor)

            cursor.endEditBlock()

    def setWordWrap(self, word_wrap):
        if word_wrap:
            self.setLineWrapMode(QtWidgets.QPlainTextEdit.WidgetWidth)
            self.setWordWrapMode(QtGui.QTextOption.WordWrap)
        else:
            self.setLineWrapMode(QtWidgets.QPlainTextEdit.NoWrap)

    def setReadOnly(self, ro):
        super(CodeEditorWidget, self).setReadOnly(ro)
        self.cursorPositionChanged.emit()

    def print_dir(self):
        cursor = self.textCursor()
        if cursor.hasSelection():
            from opendcc.ui.script_editor.script_editor import ScriptEditor

            ScriptEditor.getInterpreter().runsource(
                "import pprint; pprint.pprint(dir(%s))" % cursor.selectedText(),
                symbol="exec",
            )

    def show_find(self):
        cursor = self.textCursor()
        if cursor.hasSelection():
            self.parent().showFindReplace(cursor.selectedText())
        else:
            self.parent().showFindReplace("")

    def contextMenuEvent(self, event):
        menu = self.createStandardContextMenu()
        actions = menu.actions()

        find_action = QtWidgets.QAction(i18n("code_editor", "&Find/Replace"))
        find_action.setShortcut(QtGui.QKeySequence("ctrl+f"))
        find_action.triggered.connect(self.show_find)

        autocommment_action = QtWidgets.QAction(i18n("code_editor", "&Autocomment"))
        autocommment_action.setShortcut(QtGui.QKeySequence("ctrl+/"))
        autocommment_action.triggered.connect(self.autocomment)
        if self.lineWrapMode() != QtWidgets.QPlainTextEdit.NoWrap:  # todo: fix that
            autocommment_action.setEnabled(False)

        word_wrap_action = QtWidgets.QAction(i18n("code_editor", "Word Wrap"))
        word_wrap_action.setCheckable(True)
        word_wrap_action.setChecked(self.lineWrapMode() != QtWidgets.QPlainTextEdit.NoWrap)
        word_wrap_action.toggled.connect(self.setWordWrap)

        separator = QtWidgets.QAction()
        separator.setSeparator(True)

        if self.isReadOnly():
            menu.insertAction(actions[0], find_action)
            menu.insertAction(actions[0], word_wrap_action)
            menu.insertAction(actions[0], separator)
        else:
            menu.insertAction(actions[2], separator)
            menu.insertAction(actions[2], find_action)
            menu.insertAction(actions[2], word_wrap_action)
            menu.insertAction(actions[2], autocommment_action)

        menu.exec_(event.globalPos())

    def lineNumberAreaWidth(self):
        """The lineNumberAreaWidth is the quantity of decimal places in blockCount"""
        digits = 1
        max_ = max(1, self.blockCount())
        while max_ >= 10:
            max_ /= 10
            digits += 1

        if digits < 3:
            digits = 3

        space = 3 + self.fontMetrics().horizontalAdvance("9") * digits

        return space

    def resizeEvent(self, event):
        super(CodeEditorWidget, self).resizeEvent(event)

        if self.line_numbers:
            qRect = self.contentsRect()
            self.lineNumberArea.setGeometry(
                QtCore.QRect(
                    qRect.left(),
                    qRect.top(),
                    self.lineNumberAreaWidth(),
                    qRect.height(),
                )
            )

    def updateLineNumberAreaWidth(self):
        self.setViewportMargins(self.lineNumberAreaWidth(), 0, 0, 0)

    def updateLineNumberArea(self, rect, dy):
        """This slot is invoked when the editors viewport has been scrolled"""

        if dy:
            self.lineNumberArea.scroll(0, dy)
        else:
            self.lineNumberArea.update(0, rect.y(), self.lineNumberArea.width(), rect.height())

        if rect.contains(self.viewport().rect()):
            self.updateLineNumberAreaWidth()

    def lineNumberAreaPaintEvent(self, event):
        """This method draws the current lineNumberArea for while"""
        painter = QtGui.QPainter(self.lineNumberArea)

        if get_color_theme() == ColorTheme.LIGHT:
            painter.fillRect(event.rect(), self.palette.window().color())
        else:
            painter.fillRect(event.rect(), self.base_color.lighter(120))

        block = self.firstVisibleBlock()
        blockNumber = block.blockNumber()
        top = int(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        bottom = top + int(self.blockBoundingRect(block).height())

        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                number = str(blockNumber + 1)

                if get_color_theme() == ColorTheme.LIGHT:
                    painter.setPen(self.palette.dark().color())
                else:
                    painter.setPen(QtGui.QColor(150, 150, 150))

                painter.drawText(
                    -2,
                    top,
                    self.lineNumberArea.width(),
                    self.fontMetrics().height(),
                    QtCore.Qt.AlignRight,
                    number,
                )

            block = block.next()
            top = bottom
            bottom = top + int(self.blockBoundingRect(block).height())
            blockNumber += 1

    def highlightCurrentLine(self):
        """Highlight current line under cursor"""
        result = []
        cursor = self.textCursor()
        text = self.toPlainText()
        if get_color_theme() == ColorTheme.LIGHT:
            highlight_color = QtGui.QColor(226, 226, 226)
        else:
            highlight_color = QtGui.QColor(51, 57, 63)

        if self.highlight_selected:  # too slow for large files
            if cursor.hasSelection():  # also highlight selected text
                selected = cursor.selectedText()
                if not re.match("\\s+", selected):  # ignore white space
                    selected_length = len(selected)  # it's probably faster this way, I dunno
                    start = text.find(selected)
                    while start >= 0:  # YOLO
                        multiCursor = QtWidgets.QTextEdit.ExtraSelection()
                        multiCursor.format.setBackground(highlight_color)
                        multiCursor.cursor = cursor
                        multiCursor.cursor.setPosition(start, QtGui.QTextCursor.MoveAnchor)
                        multiCursor.cursor.setPosition(
                            start + len(selected), QtGui.QTextCursor.KeepAnchor
                        )
                        result.append(multiCursor)
                        start = text.find(selected, start + selected_length)

        if not self.isReadOnly():
            currentSelection = QtWidgets.QTextEdit.ExtraSelection()

            if get_color_theme() == ColorTheme.LIGHT:
                lineColor = self.base_color.darker(110)
            else:
                lineColor = self.base_color.lighter(130)
            currentSelection.format.setBackground(lineColor)
            currentSelection.format.setProperty(QtGui.QTextFormat.FullWidthSelection, True)
            currentSelection.cursor = cursor
            currentSelection.cursor.clearSelection()
            result.append(currentSelection)

        self.setExtraSelections(result)

    def setPythonLocals(self, locals):
        self.locals = locals

    def define_completer(self):
        self.completer = QtWidgets.QCompleter()
        self.completer.setWidget(self)
        self.keyword_completer_model = QtCore.QStringListModel(list(self.keywords))
        self.completer.setModel(self.keyword_completer_model)
        self.completer.setCompletionMode(QtWidgets.QCompleter.PopupCompletion)
        self.completer.setCaseSensitivity(QtCore.Qt.CaseInsensitive)
        self.completer.setWrapAround(False)
        self.completer.activated.connect(self.insert_completion)

    def keyPressEvent(self, event):
        """Override method"""
        # if completer is visible ignore some key press in TextEdit

        # it must be done for unfocused key presses to work
        if event.key() == QtCore.Qt.Key_Control:
            event.accept()
            return

        if self.isReadOnly():
            return QtWidgets.QPlainTextEdit.keyPressEvent(self, event)

        if self.completer and self.completer.popup().isVisible():
            if event.key() in (
                QtCore.Qt.Key_Enter,
                QtCore.Qt.Key_Return,
                QtCore.Qt.Key_Tab,
                QtCore.Qt.Key_Backtab,
            ):
                event.ignore()
                return
            QtWidgets.QPlainTextEdit.keyPressEvent(self, event)
        else:
            if event.key() == QtCore.Qt.Key_Enter:
                self.enter_pressed.emit()
            elif event.key() == QtCore.Qt.Key_Tab:
                self.add_spaces_instead_tab()
            elif (
                event.key() == QtCore.Qt.Key_Backtab and event.modifiers() & QtCore.Qt.ShiftModifier
            ):
                self.reduce_spaces()
            elif event.key() == QtCore.Qt.Key_Return:
                self.press_return()
            else:
                QtWidgets.QPlainTextEdit.keyPressEvent(self, event)

        self.run_completer(event)

    def press_return(self):
        cursor = self.textCursor()
        cursor.beginEditBlock()
        if not cursor.hasSelection():
            new_cursor = self.textCursor()  # because I can
            new_cursor.select(QtGui.QTextCursor.LineUnderCursor)
            line = new_cursor.selectedText()
            s_count = 0
            match = re.search("[^ ]", line)
            if match:
                s_count = match.start()
            else:
                s_count = len(line)

            if self.lang == "python":
                new_cursor = self.textCursor()  # because I can... yep
                new_cursor.movePosition(QtGui.QTextCursor.Left, QtGui.QTextCursor.KeepAnchor, 1)
                char = new_cursor.selectedText()
                if char == ":":
                    s_count += len(self.TAB_SEQ)

            cursor.insertText("\n" + " " * s_count)
        else:
            cursor.insertText("\n")
        self.setTextCursor(cursor)
        cursor.endEditBlock()

    def add_spaces_instead_tab(self):
        cursor = self.textCursor()
        if not cursor.hasSelection():
            cursor.insertText(self.TAB_SEQ)
        else:
            cursor.beginEditBlock()
            text = self.toPlainText()
            start, end = cursor.selectionStart(), cursor.selectionEnd()
            # add tab_seq in selected text
            code_text = text[start:end].replace("\n", "\n" + self.TAB_SEQ)
            if code_text.endswith(self.TAB_SEQ):
                code_text = code_text[: -len(self.TAB_SEQ)]
            cursor.insertText(code_text)

            # add tab_seq before first selected line
            # even if it's partially selected
            expression = QtCore.QRegExp("\n")
            start_of_line = expression.lastIndexIn(text[0:start]) + 1
            cursor.setPosition(start_of_line)
            cursor.insertText(self.TAB_SEQ)
            cursor.setPosition(start_of_line, QtGui.QTextCursor.MoveAnchor)
            cursor.setPosition(
                start_of_line + len(code_text) + len(self.TAB_SEQ),
                QtGui.QTextCursor.KeepAnchor,
            )
            self.setTextCursor(cursor)
            cursor.endEditBlock()

    def reduce_spaces(self):
        cursor = self.textCursor()
        cursor.beginEditBlock()
        if not cursor.hasSelection():
            cursor.select(QtGui.QTextCursor.LineUnderCursor)
            spaces_expression = QtCore.QRegExp("^([ ]+)")
            text = cursor.selectedText()
            index = spaces_expression.indexIn(text)
            # only if there a spaces in the beginning of the line
            if index != -1:
                cursor.insertText(text.replace(" ", "", len(self.TAB_SEQ)))
        else:
            text = self.toPlainText()
            start, end = cursor.selectionStart(), cursor.selectionEnd()
            # reduce spaces in selected text
            reduced_text = re.sub("\n([ ]{1,%d})" % len(self.TAB_SEQ), "\n", text[start:end])
            cursor.insertText(reduced_text)

            # reduce spaces before first selected line
            # even if it's partially selected
            cursor.setPosition(start)
            cursor.select(QtGui.QTextCursor.LineUnderCursor)
            selectionStart = cursor.selectionStart()
            text = cursor.selectedText()
            cursor.insertText(re.sub("^([ ]{1,%d})" % len(self.TAB_SEQ), "", text))
            cursor.setPosition(selectionStart, QtGui.QTextCursor.MoveAnchor)
            cursor.setPosition(
                selectionStart + len(reduced_text) - len(self.TAB_SEQ),
                QtGui.QTextCursor.KeepAnchor,
            )
            self.setTextCursor(cursor)
        cursor.endEditBlock()

    def run_completer(self, event, force=False):
        if not CodeEditor.getEnableAutocomplete():
            return

        if not self.completer:
            return

        if not force:
            """Check if completer needed and run it or hide"""
            ctrl_or_shift = event.modifiers() & (
                QtCore.Qt.ControlModifier | QtCore.Qt.ShiftModifier
            )
            if ctrl_or_shift and len(event.text()) == 0:
                return

            if not re.match(self.symbol_expression, event.text()):
                self.completer.popup().hide()
                return

        if self.lang in ["python", "usd"]:
            if self.complete_sdf():
                return

        if self.lang == "python" and self.locals:
            if self.complete_code():
                return
            if self.complete_word():
                return
        else:
            if self.complete_word():
                return

    def complete_sdf(self):
        prefix = self.text_under_cursor("sdf")

        if not len(prefix):
            return False

        sdf_path_string = prefix
        attr = None

        get_parent = False
        if sdf_path_string != "/":
            if sdf_path_string.endswith("/"):
                sdf_path_string = sdf_path_string[:-1]
            else:
                match = re.search(self.sdf_attr_expression, sdf_path_string)
                if match:
                    attr = match.group(1)
                    sdf_path_string = sdf_path_string[: -len(match.group(0))]
                else:
                    get_parent = True

        if not pxr.Sdf.Path.IsValidPathString(sdf_path_string):
            return False

        sdf_path = pxr.Sdf.Path(sdf_path_string)
        if get_parent:
            sdf_path = sdf_path.GetParentPath()

        if not sdf_path.isEmpty:
            app = dcc_core.Application.instance()
            stage = app.get_session().get_current_stage()
            if not stage:
                return False
            prim = stage.GetPrimAtPath(sdf_path)
            if prim.IsValid():
                if attr is None:
                    names = [item.GetPath().pathString for item in prim.GetAllChildren()]
                else:
                    names = [item.GetPath().pathString for item in prim.GetProperties()]
                self.keyword_completer_model.setStringList(names)
                self.show_completer(prefix)
                return True

        return False

    def complete_word(self):
        prefix = self.text_under_cursor("word")

        if len(prefix) >= 3:
            self.make_words_set(prefix)
            self.keyword_completer_model.setStringList(list(self.all_words))
            self.show_completer(prefix)

    def complete_code(self):
        prefix = self.text_under_cursor("code")

        if len(prefix) > 0:

            self.completer.setCompletionPrefix(prefix)

            tokens = prefix.split(".")

            if len(tokens) > 1:
                unit_descr = tokens[0]
            else:
                return False

            # try to get object from locals, if can't - hide completer
            if unit_descr in self.locals:
                obj = self.locals[unit_descr]

                for token in tokens[1:-1]:
                    if token in obj.__dict__:
                        obj = obj.__dict__[token]
                    else:
                        return False

                module_attr_list = [
                    ".".join(tokens[0:-1]) + "." + attr
                    for attr in dir(obj)
                    if not attr.startswith("_")
                ]

                self.keyword_completer_model.setStringList(module_attr_list)
                self.show_completer(prefix)
                return True
        else:
            return False

    def show_completer(self, prefix):
        self.completer.setCompletionPrefix(prefix)
        self.completer.popup().setCurrentIndex(self.completer.completionModel().index(0, 0))

        cr = self.cursorRect()
        if self.line_numbers:
            cr.translate(self.lineNumberAreaWidth(), 0)

        cr.setWidth(
            self.completer.popup().sizeHintForColumn(0)
            + self.completer.popup().verticalScrollBar().sizeHint().width()
        )
        self.completer.complete(cr)

    def insert_completion(self, completion):
        if self.completer.widget() != self:
            return

        cursor = self.textCursor()
        len_pref = len(self.completer.completionPrefix())

        cursor.beginEditBlock()
        cursor.movePosition(QtGui.QTextCursor.Left, QtGui.QTextCursor.KeepAnchor, len_pref)
        self.setTextCursor(cursor)
        cursor.insertText(completion)
        cursor.endEditBlock()
        self.ensureCursorVisible()

        self.completer.popup().hide()

    def focusInEvent(self, event):
        """Override method"""
        if self.completer:
            self.completer.setWidget(self)
        QtWidgets.QPlainTextEdit.focusInEvent(self, event)

    def text_under_cursor(self, literal):
        """Return matched literals just before cursor"""
        cursor = self.textCursor()
        pos = cursor.position()
        cursor.movePosition(QtGui.QTextCursor.StartOfLine)
        start = cursor.position()
        pos = pos - start

        cursor.select(QtGui.QTextCursor.LineUnderCursor)
        text = cursor.selectedText()
        text = text[:pos]

        if literal == "word":
            match = re.search(self.word_expression, text)
            if match:
                return match.group(0)
        elif literal == "code":
            match = re.search(self.code_expression, text)
            if match:
                return match.group(0)
        elif literal == "sdf":
            match = re.search(self.sdf_expression, text)
            if match:
                return match.group(0)[1:]
        return ""

    def insertFromMimeData(self, source):
        text = source.text().replace("\t", self.TAB_SEQ)
        self.insertPlainText(text)

    def make_words_set(self, prefix):
        """Make set of all words longer than 3 symbols"""
        # TODO maybe rewrite this first solution to use ast
        text = self.toPlainText()
        words = [word for word in re.findall(r"\b\w+\b", text) if len(word) > 3 and word != prefix]

        self.all_words = self.keywords.union(words)

    def get_imports(self):
        """We search for all import statements before cursor,
        make a map of them and import them if it's possible"""
        cursor = self.textCursor()

        # we will search for import statements before cursor
        text = self.toPlainText()[: cursor.position() - 1]
        self.imported_names = {}

        try:
            syntax_tree = ast.parse(text)
        except SyntaxError:
            # if it's syntax error than probably user did something wrong
            # or just didn't finished writing his code
            # and we don't have to autocomplete such code
            return
        except Exception as e:
            print(e)

        for node in ast.walk(syntax_tree):
            if isinstance(node, ast.Import) or isinstance(node, ast.ImportFrom):
                for alias in node.names:
                    if alias.asname is None:
                        asname = alias.name
                    else:
                        asname = alias.asname
                    self.imported_names[asname] = alias.name

                    # if module, or it's attributes was not imported,
                    # import them for further getting their attributes
                    if asname not in globals():
                        new_tree = ast.Module(body=[node])
                        ast.fix_missing_locations(new_tree)
                        try:
                            exec(compile(new_tree, "<ast>", "exec"), globals())
                        except ImportError:
                            # I don't want to see such errors, but others may be
                            pass
                        except Exception as e:
                            print(e)
