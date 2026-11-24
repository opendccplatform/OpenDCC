/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include <QString>

OPENDCC_NAMESPACE_OPEN

namespace details
{
    OPENDCC_API QString get_view_preset_label(ViewportCameraController::ViewPreset preset);
}

OPENDCC_NAMESPACE_CLOSE
