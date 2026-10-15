/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/packaging/package.h"
#include <memory>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    class OPENDCC_BASE_PACKAGING_API PackageEntryPoint
    {
    public:
        PackageEntryPoint();
        virtual ~PackageEntryPoint() = default;

        virtual void initialize(const Package& package) {};
        virtual void uninitialize(const Package& package) {};
    };

#define OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(class_name)                                                                 \
    extern "C" OPENDCC_API_EXPORT OPENDCC_NAMESPACE::base::packaging::PackageEntryPoint* opendcc_package_entry_point() \
    {                                                                                                                  \
        return new class_name;                                                                                         \
    }

}

OPENDCC_NAMESPACE_CLOSE
