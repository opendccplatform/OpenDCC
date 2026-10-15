/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/utils/api.h"
#include <functional>

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    inline void hash_combine(size_t& seed) {}

    template <class T, class... TOther>
    inline void hash_combine(size_t& seed, const T& value, TOther&&... other)
    {
        std::hash<T> hasher;
        seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        hash_combine(seed, other...);
    }

}

OPENDCC_NAMESPACE_CLOSE
