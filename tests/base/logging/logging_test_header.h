/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "opendcc/base/logging/logger.h"
OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.logging_test.header");
inline void log_from_test_header()
{
    OPENDCC_ERROR("header message");
}
OPENDCC_NAMESPACE_CLOSE
