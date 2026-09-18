// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "frames_per_second_widget.h"
#include <QDoubleValidator>
#include <QPainter>
#include <QDebug>

#include "opendcc/app/ui/application_ui.h"

OPENDCC_NAMESPACE_OPEN

FramesPerSecondWidget::FramesPerSecondWidget(QWidget* parent)
    : QComboBox(parent)
{
    setFixedWidth(70);
    setEditable(true);

    addItem("2");
    addItem("5");
    addItem("12");
    addItem("24");
    addItem("25");
    addItem("29.97");
    addItem("30");
    addItem("50");
    addItem("60");
    addItem("120");
    addItem("240");

    setCurrentText("24");

    m_line_edit = new FramesPerSecondLineEdit;
    setLineEdit(m_line_edit);

    m_validator = new QDoubleValidator(0.0001, 9001.0, 4);
    m_validator->setLocale(QLocale(QLocale::Hawaiian, QLocale::UnitedStates));
    setValidator(m_validator);

    connect(m_line_edit, SIGNAL(editingFinished()), this, SLOT(handle_editingFinished()));
    connect(this, (void(FramesPerSecondWidget::*)(int)) & FramesPerSecondWidget::currentIndexChanged,
            [this](int) { this->handle_editingFinished(); });
}

FramesPerSecondWidget::~FramesPerSecondWidget() {}

void FramesPerSecondWidget::setValue(double value)
{
    setCurrentText(QString::number(value));
}

double FramesPerSecondWidget::getValue() const
{
    return m_value;
}

void FramesPerSecondWidget::handle_editingFinished()
{
    auto text = currentText();
    if (!text.isEmpty())
    {
        m_value = text.toDouble();
        emit valueChanged(m_value);
    }
}

FramesPerSecondLineEdit::FramesPerSecondLineEdit(QWidget* parent /*= nullptr*/)
    : QLineEdit(parent)
{
    connect(this, SIGNAL(textChanged(QString)), this, SLOT(handle_textChanged(QString)));
}

FramesPerSecondLineEdit::~FramesPerSecondLineEdit() {}

void FramesPerSecondLineEdit::paintEvent(QPaintEvent* event)
{
    QLineEdit::paintEvent(event);

    // if (hasFocus()) return; // I dunno about that
    ensurePolished(); // ensure font() is up to date

    QStyleOptionFrame panel;
    initStyleOption(&panel);
    QRect r = style()->subElementRect(QStyle::SE_LineEditContents, &panel, this);

    QFontMetrics fm(font());
    r.adjust(fm.horizontalAdvance(text()) + 5, 0, 0, 0);

    QPainter p(this);

    if (hasAcceptableInput())
    {
        p.setPen(QPen(QColor(90, 90, 90), 1));
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, i18n("toolbars.timeline_slider", "fps"));
    }
    else
    {
        p.drawPixmap(r.topLeft(), m_warning);
    }
}

void FramesPerSecondLineEdit::handle_textChanged(const QString& text)
{
    if (hasAcceptableInput())
    {
        setToolTip("");
    }
    else
    {
        setToolTip("Unacceptable input");
    }
}
OPENDCC_NAMESPACE_CLOSE
