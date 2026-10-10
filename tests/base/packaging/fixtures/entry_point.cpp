// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/export.h"
#include "opendcc/base/packaging/package_entry_point.h"

OPENDCC_NAMESPACE_OPEN

enum EntryPointChecker
{
    kInitial = 0,
    kInitialized = 1,
    kUninitialized = 2
};

extern "C" OPENDCC_API_EXPORT EntryPointChecker s_entry_point_checker;
EntryPointChecker s_entry_point_checker = EntryPointChecker::kInitial;

class EntryPoint1 : public PackageEntryPoint
{
public:
    void initialize(const Package& package) override
    {
        s_entry_point_checker = EntryPointChecker::kInitialized;
        return;
    }
    void uninitialize(const Package& package) override
    {
        s_entry_point_checker = EntryPointChecker::kUninitialized;
        return;
    }
};

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(EntryPoint1);

OPENDCC_NAMESPACE_CLOSE
