/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// USD 25.08 folded Ndr into Sdr, renaming the plugin bases, macros and typedefs. Alias the Sdr
// spellings onto the Ndr originals so the sources can be written once. SdrShaderNodeUniquePtr and
// SdrShaderPropertyUniquePtr are excluded -- they exist pre-2508 holding different types.

#include <pxr/pxr.h>

#if PXR_VERSION >= 2508

#include <pxr/usd/sdr/declare.h>
#include <pxr/usd/sdr/discoveryPlugin.h>
#include <pxr/usd/sdr/parserPlugin.h>
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>

#else

#include <pxr/usd/ndr/declare.h>
#include <pxr/usd/ndr/discoveryPlugin.h>
#include <pxr/usd/ndr/parserPlugin.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>

PXR_NAMESPACE_OPEN_SCOPE
using SdrDiscoveryPlugin = NdrDiscoveryPlugin;
using SdrDiscoveryPluginContext = NdrDiscoveryPluginContext;
using SdrParserPlugin = NdrParserPlugin;
using SdrShaderNodeDiscoveryResult = NdrNodeDiscoveryResult;
using SdrShaderNodeDiscoveryResultVec = NdrNodeDiscoveryResultVec;
using SdrShaderPropertyUniquePtrVec = NdrPropertyUniquePtrVec;
using SdrIdentifier = NdrIdentifier;
using SdrVersion = NdrVersion;
using SdrStringVec = NdrStringVec;
using SdrTokenVec = NdrTokenVec;
PXR_NAMESPACE_CLOSE_SCOPE

#define SDR_REGISTER_DISCOVERY_PLUGIN(PluginClass) NDR_REGISTER_DISCOVERY_PLUGIN(PluginClass)
#define SDR_REGISTER_PARSER_PLUGIN(PluginClass) NDR_REGISTER_PARSER_PLUGIN(PluginClass)

#endif
