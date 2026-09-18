// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/render_view/image_view/key_sequence_edit.h"

#include <QEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QStyleOption>
#include <QPainter>
#include <QStyle>

#include <opendcc/render_view/image_view/app.h>

KeySequenceEdit::KeySequenceEdit(QWidget *parent, int sequence_length)
    : QWidget(parent)
    , m_current_index(0)
    , m_sequence_length(sequence_length)
{
    m_line_edit = new QLineEdit(this);
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(m_line_edit);
    layout->setContentsMargins(0, 0, 0, 0);
    m_line_edit->installEventFilter(this);
    m_line_edit->setReadOnly(true);
    m_line_edit->setFocusProxy(this);
    setFocusPolicy(m_line_edit->focusPolicy());
    setAttribute(Qt::WA_InputMethodEnabled);
}

bool KeySequenceEdit::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_line_edit && event->type() == QEvent::ContextMenu)
    {
        auto *context_event = static_cast<QContextMenuEvent *>(event);
        QMenu *menu = m_line_edit->createStandardContextMenu();

        const QList<QAction *> actions = menu->actions();
        for (QAction *action : actions)
        {
            action->setShortcut(QKeySequence());
            QString text = action->text();
            int tab_pos = text.lastIndexOf(QLatin1Char('\t'));
            if (tab_pos > 0)
            {
                text.remove(tab_pos, text.length() - tab_pos);
            }
            action->setText(text);
        }

        QAction *first_action = actions.isEmpty() ? nullptr : actions.first();
        QAction *clear_action = new QAction(OPENDCC_NAMESPACE::i18n("render_view.preferences.hotkeys.context_menu", "Clear Shortcut"), menu);
        menu->insertAction(first_action, clear_action);
        menu->insertSeparator(first_action);
        clear_action->setEnabled(!m_current_sequence.isEmpty());

        connect(clear_action, &QAction::triggered, this, &KeySequenceEdit::clear_shortcut);

        menu->exec(context_event->globalPos());
        delete menu;
        event->accept();
        return true;
    }

    return QWidget::eventFilter(object, event);
}

void KeySequenceEdit::clear_shortcut()
{
    if (m_current_sequence.isEmpty())
        return;
    set_key_sequence(QKeySequence());
    emit key_sequence_changed(m_current_sequence);
}

void KeySequenceEdit::handle_key_event(QKeyEvent *event)
{
    int key_code = event->key();
    if (key_code == Qt::Key_Control || key_code == Qt::Key_Shift || key_code == Qt::Key_Meta || key_code == Qt::Key_Alt ||
        key_code == Qt::Key_Super_L || key_code == Qt::Key_AltGr)
    {
        return;
    }

    key_code |= translate_modifiers(event->modifiers(), event->text());

    int key_0 = m_current_sequence[0];
    int key_1 = m_current_sequence[1];
    int key_2 = m_current_sequence[2];
    int key_3 = m_current_sequence[3];

    switch (m_current_index)
    {
    case 0:
        key_0 = key_code;
        key_1 = key_2 = key_3 = 0;
        break;
    case 1:
        key_1 = key_code;
        key_2 = key_3 = 0;
        break;
    case 2:
        key_2 = key_code;
        key_3 = 0;
        break;
    case 3:
        key_3 = key_code;
        break;
    default:
        break;
    }

    ++m_current_index;
    if (m_current_index > m_sequence_length)
    {
        m_current_index = 0;
    }

    m_current_sequence = QKeySequence(key_0, key_1, key_2, key_3);
    m_line_edit->setText(m_current_sequence.toString(QKeySequence::NativeText));
    event->accept();
    emit key_sequence_changed(m_current_sequence);
}

void KeySequenceEdit::set_key_sequence(const QKeySequence &sequence)
{
    if (sequence == m_current_sequence)
        return;
    m_current_index = 0;
    m_current_sequence = sequence;
    m_line_edit->setText(m_current_sequence.toString(QKeySequence::NativeText));
    emit key_sequence_changed(m_current_sequence);
}

QKeySequence KeySequenceEdit::key_sequence() const
{
    return m_current_sequence;
}

int KeySequenceEdit::translate_modifiers(Qt::KeyboardModifiers modifiers, const QString &text) const
{
    int result = 0;
    if ((modifiers & Qt::ShiftModifier) && (text.isEmpty() || !text.at(0).isPrint() || text.at(0).isLetter() || text.at(0).isSpace()))
    {
        result |= Qt::SHIFT;
    }
    if (modifiers & Qt::ControlModifier)
    {
        result |= Qt::CTRL;
    }
    if (modifiers & Qt::MetaModifier)
    {
        result |= Qt::META;
    }
    if (modifiers & Qt::AltModifier)
    {
        result |= Qt::ALT;
    }
    return result;
}

void KeySequenceEdit::focusInEvent(QFocusEvent *event)
{
    m_line_edit->event(event);
    m_line_edit->selectAll();
    QWidget::focusInEvent(event);
}

void KeySequenceEdit::focusOutEvent(QFocusEvent *event)
{
    m_current_index = 0;
    m_line_edit->event(event);
    QWidget::focusOutEvent(event);
}

void KeySequenceEdit::keyPressEvent(QKeyEvent *event)
{
    handle_key_event(event);
    event->accept();
}

void KeySequenceEdit::keyReleaseEvent(QKeyEvent *event)
{
    m_line_edit->event(event);
}

void KeySequenceEdit::paintEvent(QPaintEvent *)
{
    QStyleOption style_option;
    style_option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &style_option, &painter, this);
}

bool KeySequenceEdit::event(QEvent *event)
{
    if (event->type() == QEvent::Shortcut || event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyRelease)
    {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void KeySequenceEdit::set_sequence_length(int length)
{
    m_sequence_length = length;
}

void KeySequenceEdit::set_is_error(bool is_error)
{
    if (is_error)
    {
        m_line_edit->setStyleSheet("background: rgb(60, 40, 40)");
    }
    else
    {
        m_line_edit->setStyleSheet("background: rgb(40, 60, 40)");
    }
}
