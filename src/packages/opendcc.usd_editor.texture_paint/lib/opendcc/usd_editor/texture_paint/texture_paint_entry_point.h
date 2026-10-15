/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/base/packaging/package_entry_point.h"

OPENDCC_NAMESPACE_OPEN

class TexturePaintEntryPoint : public base::packaging::PackageEntryPoint
{
public:
    void initialize(const base::packaging::Package& package) override;
    void uninitialize(const base::packaging::Package& package) override;
};

OPENDCC_NAMESPACE_CLOSE
