// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <QScreen>
#include <QGuiApplication>
#include "opendcc/ui/common_widgets/help_button_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QScrollArea>
#include <QApplication>
#include <QMouseEvent>
#include <QTextBrowser>
#include <QRegularExpression>

// #include <QDebug>

#include <memory>
#include <string>

#include "maddy/parser.h"

OPENDCC_NAMESPACE_OPEN

HelpDialog::HelpDialog(QString title_text, QString help_text, bool markdown, QWidget* parent /*= nullptr*/)
    : QFrame(parent, Qt::Popup)
{
    setWindowTitle(title_text);
    setFrameStyle(QFrame::Box);
    setLayout(new QVBoxLayout());
    layout()->setContentsMargins(2, 2, 2, 2);

    auto toolbar = new QFrame();
    auto toolbar_layout = new QHBoxLayout();
    toolbar->setLayout(toolbar_layout);

    auto title = new QLabel(windowTitle(), toolbar);
    title->setStyleSheet("font-weight: bold;");
    title->setContentsMargins(5, 2, 5, 2);

    auto close_btn = new QToolButton(toolbar);
    close_btn->setAutoRaise(true);
    close_btn->setIcon(QIcon(":icons/close_tab"));
    close_btn->setIconSize(QSize(12, 12));
    close_btn->setToolTip("Close Help");
    connect(close_btn, &QToolButton::clicked, this, &HelpDialog::close);

    toolbar_layout->addWidget(title);
    toolbar_layout->setStretchFactor(title, 10);
    toolbar_layout->addWidget(close_btn);
    toolbar_layout->setContentsMargins(0, 0, 0, 0);

    auto doc_scroll_area = new QScrollArea(this);
    auto doc_browser = new QTextBrowser(this);
    doc_browser->setOpenExternalLinks(true);

    if (markdown)
    {
        QRegularExpression code_re("\\\\code(.*)\\\\endcode", QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator code_iterator = code_re.globalMatch(help_text);
        while (code_iterator.hasNext())
        {
            QRegularExpressionMatch match = code_iterator.next();
            if (match.hasMatch())
            {
                // help_text.replace(match.captured(0), QString("<br><pre><code>%1</code></pre>").arg(match.captured(1))); // too fugly to look at T_T
                // help_text.replace(match.captured(0), QString("<br><b>Please look it up on a web site or something because it's too cumbersome to
                // display it here.</b>").arg(match.captured(1))); // nah...
                help_text.replace(match.captured(0), QString("\n```\n%1\n```\n").arg(match.captured(1)));
            }
        }

        QRegularExpression note_re("\\\\note");
        help_text.replace(note_re, "<b>Note:</b> ");

        QRegularExpression garbage_re("    ");
        help_text.replace(garbage_re, "");

        QRegularExpression anchor_re("\\\\anchor.*\n");
        help_text.replace(anchor_re, "");

        std::stringstream markdownInput(help_text.toStdString().c_str());

        // config is optional
        std::shared_ptr<maddy::ParserConfig> config = std::make_shared<maddy::ParserConfig>();
        config->isEmphasizedParserEnabled = true; // default
        config->isHTMLWrappedInParagraph = true; // default

        std::shared_ptr<maddy::Parser> parser = std::make_shared<maddy::Parser>(config);
        std::string htmlOutput = parser->Parse(markdownInput);

        help_text = htmlOutput.c_str();
    }

    doc_browser->setHtml(help_text);
    doc_browser->setAlignment(Qt::AlignTop);
    doc_browser->setContentsMargins(2, 2, 2, 2);
    doc_scroll_area->setWidget(doc_browser);
    doc_scroll_area->setWidgetResizable(true);
    doc_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    layout()->addWidget(toolbar);
    layout()->addWidget(doc_scroll_area);
    setAttribute(Qt::WA_DeleteOnClose);
}

HelpButtonWidget::HelpButtonWidget(QWidget* parent /*= nullptr*/)
    : QPushButton(parent)
{
    setIcon(QIcon(":icons/question"));
    setFixedSize(15, 15);
    setIconSize(QSize(12, 12));
    setFlat(true);
}

void HelpButtonWidget::set_docs(QString title, QString docs)
{
    m_title = title;
    m_docs = docs;
}

void HelpButtonWidget::mouseReleaseEvent(QMouseEvent* e)
{
    auto dialog = new HelpDialog(m_title, m_docs, m_markdown, this);

    dialog->move(e->globalPos());
    dialog->adjustSize();
    auto geom = dialog->frameGeometry();
    // QDesktopWidget is gone in Qt6; QWidget::screen() is Qt 5.14+
    const auto* widget_screen = screen() ? screen() : QGuiApplication::primaryScreen();
    auto screen_rect = widget_screen->geometry();

    if (geom.left() < screen_rect.left())
        geom.translate(screen_rect.left() - geom.left(), 0);
    if (geom.right() > screen_rect.right())
        geom.translate(screen_rect.right() - geom.right(), 0);
    if (geom.top() < screen_rect.top())
        geom.translate(0, screen_rect.top() - geom.top());
    if (geom.bottom() > screen_rect.bottom())
        geom.translate(0, screen_rect.bottom() - geom.bottom());

    dialog->setGeometry(geom);
    dialog->show();
}

OPENDCC_NAMESPACE_CLOSE
