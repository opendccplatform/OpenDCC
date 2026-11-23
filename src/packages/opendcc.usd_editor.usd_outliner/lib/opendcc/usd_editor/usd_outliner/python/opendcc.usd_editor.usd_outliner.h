/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/base/defines.h"
#if defined(OPENDCC_OS_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _MT
// Shiboken parses without /MD; classic TBB still requires the runtime selection macro.
#define _MT
#endif
#endif

#include "opendcc/base/qt_python.h"
#include "opendcc/usd_editor/usd_outliner/usd_stage_data_source.h"
#include "opendcc/usd_editor/usd_outliner/usd_selection_context.h"
#include "opendcc/usd_editor/usd_outliner/usd_item_id.h"
using namespace OPENDCC_NAMESPACE;
