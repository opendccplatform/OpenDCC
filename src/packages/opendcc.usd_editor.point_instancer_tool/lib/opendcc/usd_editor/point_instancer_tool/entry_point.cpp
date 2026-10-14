// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/point_instancer_tool/entry_point.h"
#include <iostream>
#include "opendcc/usd_editor/point_instancer_tool/tool_context.h"
#include "opendcc/usd_editor/point_instancer_tool/tool_settings.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(PointInstancerToolEntryPoint);

PXR_NAMESPACE_USING_DIRECTIVE

void PointInstancerToolEntryPoint::initialize(const Package& package)
{
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("PointInstancer"), []() { return new PointInstancerToolContext; });
}

void PointInstancerToolEntryPoint::uninitialize(const Package& package)
{
    ViewportToolContextRegistry::unregister_tool_context(TfToken("USD"), TfToken("PointInstancer"));
}

OPENDCC_NAMESPACE_CLOSE
