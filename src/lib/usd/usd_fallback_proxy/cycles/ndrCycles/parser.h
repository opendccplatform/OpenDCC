/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "api.h"
#include "sdr_compat.h"
#include "opendcc/opendcc.h"

OPENDCC_NAMESPACE_OPEN

class NdrCyclesParserPlugin : public PXR_NS::SdrParserPlugin
{
public:
    NdrCyclesParserPlugin();
    ~NdrCyclesParserPlugin() override;

#if PXR_VERSION >= 2508
    PXR_NS::SdrShaderNodeUniquePtr ParseShaderNode(const PXR_NS::SdrShaderNodeDiscoveryResult& discoveryResult) override;
#else
    PXR_NS::NdrNodeUniquePtr Parse(const PXR_NS::SdrShaderNodeDiscoveryResult& discoveryResult) override;
#endif

    const PXR_NS::SdrTokenVec& GetDiscoveryTypes() const override;
    const PXR_NS::TfToken& GetSourceType() const override;
};

OPENDCC_NAMESPACE_CLOSE
