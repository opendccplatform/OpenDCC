/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/packaging/package.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    struct PackageData
    {
        std::string name;
        std::string path;
        PackageAttributeList raw_attributes;

        bool is_valid() const { return !name.empty(); }
        operator bool() const { return is_valid(); }
    };

    using PackageDataList = std::vector<PackageData>;

    class OPENDCC_BASE_PACKAGING_API PackageParser
    {
    public:
        virtual ~PackageParser() = default;
        virtual PackageData parse(const std::string& path) = 0;
    };

}

OPENDCC_NAMESPACE_CLOSE
