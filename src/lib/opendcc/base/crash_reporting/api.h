/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opendcc/base/export.h>

#ifdef OPENDCC_BASE_CRASH_REPORTING_EXPORT
#define OPENDCC_BASE_CRASH_REPORTING_API OPENDCC_API_EXPORT
#else
#define OPENDCC_BASE_CRASH_REPORTING_API OPENDCC_API_IMPORT
#endif
