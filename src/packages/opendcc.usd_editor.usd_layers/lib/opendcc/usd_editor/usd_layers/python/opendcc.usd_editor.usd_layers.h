/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <opendcc/base/qt_python.h>

// Make "signals:", "slots:" visible as access specifiers to the binding parser.
#define QT_ANNOTATE_ACCESS_SPECIFIER(a) __attribute__((annotate(#a)))

#include <QMenu>
#include <QWidget>

#include "opendcc/usd_editor/usd_layers/usd_layers_model.h"
#include "opendcc/usd_editor/usd_layers/usd_layers_view.h"

using namespace OPENDCC_NAMESPACE;
