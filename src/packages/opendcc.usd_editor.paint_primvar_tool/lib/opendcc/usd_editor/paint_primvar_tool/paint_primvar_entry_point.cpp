// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/paint_primvar_tool/paint_primvar_entry_point.h"
#include <iostream>
#include "opendcc/usd_editor/paint_primvar_tool/paint_primvar_tool_context.h"
#include "opendcc/usd_editor/paint_primvar_tool/paint_primvar_tool_settings.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(PaintPrimvarToolEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

PaintPrimvarToolEntryPoint::PaintPrimvarToolEntryPoint() {}

PaintPrimvarToolEntryPoint::~PaintPrimvarToolEntryPoint() {}

void PaintPrimvarToolEntryPoint::initialize(const Package& package)
{
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("PaintPrimvar"), []() { return new PaintPrimvarToolContext; });
}

void PaintPrimvarToolEntryPoint::uninitialize(const Package& package)
{
    ViewportToolContextRegistry::unregister_tool_context(TfToken("USD"), TfToken("PaintPrimvar"));
}

OPENDCC_NAMESPACE_CLOSE
