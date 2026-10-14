/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/base/defines.h"

#if defined(OPENDCC_OS_LINUX)
#include <limits.h>

#define OPENDCC_MAX_PATH PATH_MAX
#elif defined(OPENDCC_OS_MAC)
#include <limits.h>

#define OPENDCC_MAX_PATH PATH_MAX
#elif defined(OPENDCC_OS_WINDOWS)
#include <Windows.h>

#define OPENDCC_MAX_PATH MAX_PATH
#endif

#if defined(OPENDCC_OS_WINDOWS)
#define OPENDCC_PATH_LIST_SEPARATOR ";"
#else
#define OPENDCC_PATH_LIST_SEPARATOR ":"
#endif
