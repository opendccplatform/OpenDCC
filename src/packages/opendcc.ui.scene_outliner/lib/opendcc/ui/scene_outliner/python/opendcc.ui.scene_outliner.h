/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <opendcc/base/qt_python.h>

// Make "signals:", "slots:" visible as access specifiers to the binding parser.
#define QT_ANNOTATE_ACCESS_SPECIFIER(a) __attribute__((annotate(#a)))

#include <QMenu>
#include <QMenuBar>
#include <QWidget>

#include "opendcc/ui/scene_outliner/item_id.h"
#include "opendcc/ui/scene_outliner/tree_data_source.h"
#include "opendcc/ui/scene_outliner/outliner_widget.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/outliner_proxy_model.h"
#include "opendcc/ui/scene_outliner/outliner_tree_view.h"

using namespace OPENDCC_NAMESPACE;
