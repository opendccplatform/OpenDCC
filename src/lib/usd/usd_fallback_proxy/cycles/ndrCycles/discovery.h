/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "sdr_compat.h"

OPENDCC_NAMESPACE_OPEN

class NdrCyclesDiscoveryPlugin : public PXR_NS::SdrDiscoveryPlugin
{
public:
    using Context = PXR_NS::SdrDiscoveryPluginContext;

    NdrCyclesDiscoveryPlugin();
    ~NdrCyclesDiscoveryPlugin();

#if PXR_VERSION >= 2508
    PXR_NS::SdrShaderNodeDiscoveryResultVec DiscoverShaderNodes(const Context& context) override;
#else
    PXR_NS::SdrShaderNodeDiscoveryResultVec DiscoverNodes(const Context& context) override;
#endif

    const PXR_NS::SdrStringVec& GetSearchURIs() const override;
};

OPENDCC_NAMESPACE_CLOSE
