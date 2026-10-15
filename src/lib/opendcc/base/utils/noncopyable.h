/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    struct Noncopyable
    {
        constexpr Noncopyable() = default;
        ~Noncopyable() = default;
        Noncopyable(const Noncopyable&) = delete;
        Noncopyable& operator=(const Noncopyable&) = delete;
    };

}

OPENDCC_NAMESPACE_CLOSE
