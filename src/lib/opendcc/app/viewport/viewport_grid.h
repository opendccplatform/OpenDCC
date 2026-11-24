/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include <pxr/base/gf/frustum.h>
#include <pxr/usd/usdGeom/tokens.h>

OPENDCC_NAMESPACE_OPEN

class ViewportUiDrawManager;

class OPENDCC_API ViewportGrid
{
public:
    ViewportGrid(const PXR_NS::GfVec4f& grid_color, float min_step, bool enable = true, const PXR_NS::TfToken& up_axis = PXR_NS::UsdGeomTokens->y);
    ~ViewportGrid();
    void draw(const PXR_NS::GfFrustum& frustum);
    void set_enabled(bool enable);
    void set_min_step(double min_step);
    void set_grid_color(const PXR_NS::GfVec4f& color);
    enum class Plane
    {
        XY, ///< Grid in the XY plane.
        XZ, ///< Grid in the XZ plane.
        YZ ///< Grid in the YZ plane.
    };
    void set_plane(Plane plane);
    void set_up_axis(const PXR_NS::TfToken& up_axis);

private:
    PXR_NS::GfVec4f m_grid_lines_color = PXR_NS::GfVec4f(0.251);
    bool m_enable = true;
    double m_min_step = 0.01;
    int m_step_count = 10;

    uint32_t m_plane_indices_size = 0;
    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ebo = 0;
    uint32_t m_shader_id = 0;
    int32_t m_proj = -1;
    int32_t m_view = -1;
    int32_t m_vp = -1;
    int32_t m_inv_view = -1;
    int32_t m_view_pos = -1;
    int32_t m_grid_size = -1;
    int32_t m_min_step_loc = -1;
    int32_t m_grid_lines_color_loc = -1;
    int32_t m_plane_orient_loc = -1;
    int m_plane_orientation = 1;
};

OPENDCC_NAMESPACE_CLOSE
