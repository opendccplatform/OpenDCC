// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/common_widgets/qt_compat.h"

#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QBoxLayout>

#include "opendcc/ui/common_widgets/popup_window.h"

OPENDCC_NAMESPACE_OPEN

PopupWindow::PopupWindow(QWidget* parent /*= 0*/)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::CustomizeWindowHint)
{
    const auto name = "PopupWindow";

    installEventFilter(this);
    auto check_parent = parent;
    while (check_parent)
    {
        if (check_parent->objectName() == "CustomPopup")
            installEventFilter(check_parent);

        check_parent = check_parent->parentWidget();
    }

    setMinimumSize(200, 350);
    setFocus(Qt::MouseFocusReason);
    setObjectName(name);
}

void PopupWindow::show()
{
    QRect rec = primary_screen_geometry();
    auto h = rec.height();
    auto w = rec.width();
    int x = QCursor::pos().x() - width() / 2;
    int y = QCursor::pos().y();

    if (x < 0)
        x = 0;
    if (y > h - height())
        y = h - height();

    move(x, y);

    QWidget::show();
    activateWindow();
}

void PopupWindow::show(int x, int y)
{
    QRect rec = primary_screen_geometry();
    auto h = rec.height();
    auto w = rec.width();

    if (x < 0)
        x = 0;
    if (y > h - height())
        y = h - height();

    move(x, y);

    QWidget::show();
    activateWindow();
}

bool PopupWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::WindowDeactivate)
    {
        auto parent = QApplication::activeWindow();
        while (parent)
        {
            if (parent == this)
                return false;

            parent = parent->parentWidget();
        }

        hide();
    }
    return false;
}

void PopupWindow::set_widget(QWidget* wid)
{
    QHBoxLayout* lay;
    setLayout(lay = new QHBoxLayout);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(wid);
    wid->installEventFilter(this);
}

OPENDCC_NAMESPACE_CLOSE