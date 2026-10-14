/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include <unordered_map>
#include "opendcc/base/packaging/package_attributes.h"

OPENDCC_NAMESPACE_OPEN

OPENDCC_PACKAGING_API extern std::unordered_map<std::string, std::any> s_core_property_defaults;

OPENDCC_NAMESPACE_CLOSE
