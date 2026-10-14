/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/vendor/tl/expected.hpp"

OPENDCC_NAMESPACE_OPEN

// Keep backend selection project-wide; changing it requires rebuilding native consumers.
template <class T, class E>
using Expected = tl::expected<T, E>;
template <class E>
using Unexpected = tl::unexpected<E>;

OPENDCC_NAMESPACE_CLOSE
