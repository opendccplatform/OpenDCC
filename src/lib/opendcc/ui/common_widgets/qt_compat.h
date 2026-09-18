// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Small Qt5/Qt6 compatibility shims shared by the widget libraries.

#include "opendcc/opendcc.h"

#include <QColor>
#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QtGlobal>

OPENDCC_NAMESPACE_OPEN

// Qt6 takes float* where Qt5 took qreal*. Pointers get no implicit conversion, so the call sites
// cannot simply pass doubles.
inline void get_rgb_f(const QColor& color, double* r, double* g, double* b, double* a = nullptr)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    float fr = 0.0f, fg = 0.0f, fb = 0.0f, fa = 0.0f;
    color.getRgbF(&fr, &fg, &fb, a ? &fa : nullptr);
    *r = fr;
    *g = fg;
    *b = fb;
    if (a)
        *a = fa;
#else
    color.getRgbF(r, g, b, a);
#endif
}

inline void get_hsv_f(const QColor& color, double* h, double* s, double* v, double* a = nullptr)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    float fh = 0.0f, fs = 0.0f, fv = 0.0f, fa = 0.0f;
    color.getHsvF(&fh, &fs, &fv, a ? &fa : nullptr);
    *h = fh;
    *s = fs;
    *v = fv;
    if (a)
        *a = fa;
#else
    color.getHsvF(h, s, v, a);
#endif
}

// QDesktopWidget is gone in Qt6. primaryScreen() can be null when no screen is attached, which
// QApplication::desktop() never was, so every replacement goes through these.
inline QRect primary_screen_geometry()
{
    const auto* screen = QGuiApplication::primaryScreen();
    return screen ? screen->geometry() : QRect();
}

inline QRect screen_geometry_at(const QPoint& global_pos)
{
    const auto* screen = QGuiApplication::screenAt(global_pos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->geometry() : QRect();
}

inline QRect available_screen_geometry_at(const QPoint& global_pos)
{
    const auto* screen = QGuiApplication::screenAt(global_pos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect();
}

OPENDCC_NAMESPACE_CLOSE
