/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/packaging/package_parser.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    class OPENDCC_BASE_PACKAGING_API TOMLParser final : public PackageParser
    {
    public:
        PackageData parse(const std::string& path) override;
    };

}

OPENDCC_NAMESPACE_CLOSE
