/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32) && !defined(_MT)
// Shiboken parses without /MD; TBB headers still require the runtime selection macro.
#define _MT
#endif

#include "opendcc/base/qt_python.h"
#ifndef QT_NOT_GEN
#define QT_ANNOTATE_ACCESS_SPECIFIER(a) __attribute__((annotate(#a)))
#endif

#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_view.h"
