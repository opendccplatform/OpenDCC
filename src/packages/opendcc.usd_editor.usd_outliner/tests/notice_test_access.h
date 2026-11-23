/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/usd_editor/usd_outliner/usd_stage_data_source.h"
#include <functional>

OPENDCC_NAMESPACE_OPEN

class UsdOutlinerNoticeTestAccess
{
public:
    static void set_callbacks(UsdStageDataSource& source, std::function<void()> before_post, std::function<void()> after_detach);
};

OPENDCC_NAMESPACE_CLOSE
