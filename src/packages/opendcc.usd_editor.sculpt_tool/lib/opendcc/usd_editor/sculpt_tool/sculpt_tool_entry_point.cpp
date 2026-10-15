// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/sculpt_tool/sculpt_tool_entry_point.h"
#include <iostream>
#include "opendcc/usd_editor/sculpt_tool/sculpt_tool_context.h"
#include "opendcc/usd_editor/sculpt_tool/sculpt_tool_settings.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(SculptToolEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

SculptToolEntryPoint::SculptToolEntryPoint() {}

SculptToolEntryPoint::~SculptToolEntryPoint() {}

void SculptToolEntryPoint::initialize(const Package& package)
{
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("Sculpt"), []() { return new SculptToolContext; });
}

void SculptToolEntryPoint::uninitialize(const Package& package)
{
    ViewportToolContextRegistry::unregister_tool_context(TfToken("USD"), TfToken("Sculpt"));
}

OPENDCC_NAMESPACE_CLOSE
