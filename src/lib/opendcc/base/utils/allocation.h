/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/utils/api.h"

#include <string>
#include <memory>
#include <functional>

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    OPENDCC_BASE_UTILS_API std::tuple<std::string, bool> dynamic_alloc_read(size_t init_size,
                                                                            const std::function<bool(char*, size_t&)>& alloc_callback);

}

OPENDCC_NAMESPACE_CLOSE
