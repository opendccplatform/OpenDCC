/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opendcc/base/export.h>

#ifdef OPENDCC_BASE_UTILS_EXPORT
#define OPENDCC_BASE_UTILS_API OPENDCC_API_EXPORT
#else
#define OPENDCC_BASE_UTILS_API OPENDCC_API_IMPORT
#endif
