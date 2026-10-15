// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/entry_point.h"

#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/usd_editor/bezier_tool/bezier_tool_context.h"

#include <pxr/base/tf/type.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(BezierToolEntryPoint);

void BezierToolEntryPoint::initialize(const Package& package)
{
    ViewportToolContextRegistry::register_tool_context(TfToken("USD"), TfToken("BezierCurveTool"), []() { return new BezierToolContext; });
}

void BezierToolEntryPoint::uninitialize(const Package& package)
{
    ViewportToolContextRegistry::unregister_tool_context(TfToken("USD"), TfToken("BezierCurveTool"));
}

OPENDCC_NAMESPACE_CLOSE
