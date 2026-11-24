// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_grid.h"
#if PXR_VERSION < 2108
#include <GL/glew.h>
#else
#include <pxr/imaging/garch/gl.h>
#endif
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include <pxr/usd/usdGeom/tokens.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

ViewportGrid::ViewportGrid(const PXR_NS::GfVec4f& lines_color, float min_step, bool enable /*= true*/,
                           const PXR_NS::TfToken& up_axis /*= PXR_NS::UsdGeomTokens->y*/)
    : m_grid_lines_color(lines_color)
    , m_min_step(min_step)
    , m_enable(enable)
{
    set_up_axis(up_axis);

    m_shader_id = glCreateProgram();
    static const std::array<const char*, 2> shader_src = {
        R"(#version 330
uniform mat4 matProj;
uniform mat4 matView;
uniform mat4 matViewProj;
uniform vec3 vViewPosition;
uniform float gridSize;
uniform int plane_orient;
in vec2 in_pos;
out vec3 local_pos;

#define XY 0
#define XZ 1
#define YZ 2

void main(void)
{
   vec3 vert_pos;
   vec3 plane_offset;
   if (plane_orient == XY)
   {    
       vert_pos = vec3(in_pos.x, in_pos.y, 0);
       plane_offset = vec3(vViewPosition.x, vViewPosition.y, 0);
   }
   else if (plane_orient == YZ)
   {
       vert_pos = vec3(0, in_pos.x, in_pos.y);
       plane_offset = vec3(0, vViewPosition.y, vViewPosition.z);
   }
   else
   {
       vert_pos = vec3(in_pos.x, 0, in_pos.y);
       plane_offset = vec3(vViewPosition.x, 0, vViewPosition.z);
   }
   local_pos = vert_pos;
   gl_Position = matProj * matView * vec4(vert_pos * gridSize + plane_offset, 1);
}
)",

        R"(#version 330
uniform mat4 matProj;
uniform mat4 matView;
uniform mat4 matViewProj;
uniform mat4 matViewInverse;
uniform vec3 vViewPosition;
uniform vec4 grid_lines_color;
uniform float gridSize;
uniform float min_step;
uniform int plane_orient;
in vec3 local_pos;

out vec4 outColor;

#define M_1_SQRTPI 0.5641895835477563 /* 1/sqrt(pi) */

#define XY 0
#define XZ 1
#define YZ 2
#define DISC_RADIUS (M_1_SQRTPI * 1.05)
#define GRID_LINE_SMOOTH_START (0.5 - DISC_RADIUS)
#define GRID_LINE_SMOOTH_END (0.5 + DISC_RADIUS)

float get_grid(vec3 wPos, vec3 fwidthPos, float grid_size)
{
   float half_size = grid_size / 2;
   vec2 grid_domain;
   if (plane_orient == XY)
   {   
       grid_domain = abs(mod(wPos.xy + half_size, grid_size) - half_size);
       grid_domain /= fwidthPos.xy;
   }
   else if (plane_orient == YZ)
   {
      grid_domain = abs(mod(wPos.yz + half_size, grid_size) - half_size);
      grid_domain /= fwidthPos.yz;
   }
   else
   {
      grid_domain = abs(mod(wPos.xz + half_size, grid_size) - half_size);
      grid_domain /= fwidthPos.xz;
   }
   float line_dist = min(grid_domain.x, grid_domain.y);
   float lineKernel = 0;
   return 1.0 - smoothstep(GRID_LINE_SMOOTH_START, GRID_LINE_SMOOTH_END, line_dist - lineKernel); 
}

void main(void)
{
   vec3 fragPos3D = local_pos * gridSize;
   vec3 fwidthPos = fwidth(fragPos3D);
   if (plane_orient == XY)
       fragPos3D += vec3(vViewPosition.x, vViewPosition.y, 0);
   else if (plane_orient == YZ)
       fragPos3D += vec3(0, vViewPosition.y, vViewPosition.z);
   else
       fragPos3D += vec3(vViewPosition.x, 0, vViewPosition.z);
   
   float fade, dist;
   if (matProj[3][3] == 0.0) {
       vec3 viewvec = vViewPosition.xyz - fragPos3D;
       dist = length(viewvec);
       viewvec /= dist;
   
       float grid_distance = gridSize / 2;
       float angle;
       if (plane_orient == XY)
           angle = 1.0 - abs(viewvec.z);
       else if (plane_orient == YZ)
           angle = 1.0 - abs(viewvec.x);
       else
           angle = 1.0 - abs(viewvec.y);
       angle *= angle;
       fade = 1.0 - angle * angle;
       fade *= 1.0 - smoothstep(0.0, grid_distance, dist - grid_distance);  
       gl_FragDepth = gl_FragCoord.z; 
   }
   else
   {
// we use adjusted projection and view matrices for grid rendering,
// in order to resolve correct values in Zbuffer we evaluate real depth value here.
// We want the grid to be visible even if it is outside of the "real" ortho projection frustum,
// so we assign max depth value for these cases.
       float real_z = ((matViewProj * vec4(fragPos3D, 1)).z + 1) * 0.5;
       gl_FragDepth = mix(real_z, 0.0f, real_z < 0 || real_z > 1);
       dist = gl_FragCoord.z * 2 - 1;
// if you want to remove fade in camera mode replace to this: clamp(dist, 0.0, 1.0);
       dist = abs(dist); 

       fade = 1.0 - smoothstep(0.0, 0.5, dist - 0.5);
       float angle;
       if (plane_orient == XY)
       {
           angle = 1.0 - abs(matViewInverse[2].z);
           angle *= angle;
           fade *= 1.0 - angle * angle;
       }
       else if (plane_orient == YZ)
       {
           angle = 1.0 - abs(matViewInverse[2].x);
           angle *= angle;
           fade *= 1.0 - angle * angle;
       }
       else
       {
           angle = 1.0 - abs(matViewInverse[2].y);
           angle *= angle;
           fade *= 1.0 - angle * angle;
       }

   }
   float grid_res = max(dot(dFdx(fragPos3D), matViewInverse[0].xyz), dot(dFdy(fragPos3D), matViewInverse[1].xyz));
   grid_res *= 4;
   vec4 scale;
   int step_id = 0;
   scale[0] = 0.0;
   scale[1] = min_step;
   
   while (scale[1] < grid_res && step_id != 7)
   {
      scale[0] = scale[1];
      scale[1] = scale[0] * 10;
      step_id++;
   }
   scale[2] = scale[1] * 10;
   scale[3] = scale[2] * 10;

   float blend = 1.0 - clamp((grid_res - scale[0]) / abs(scale[1] - scale[0]), 0.0, 1.0);
   blend = blend * blend * blend;
   
   float gridA = get_grid(fragPos3D, fwidthPos, scale[1]);
   float gridB = get_grid(fragPos3D, fwidthPos, scale[2]);
   float gridC = get_grid(fragPos3D, fwidthPos, scale[3]);

   vec4 subdiv_lines_color = grid_lines_color * 0.85;
   outColor = subdiv_lines_color;
   outColor.a *= gridA * blend;
   outColor = mix(outColor, mix(subdiv_lines_color, grid_lines_color, blend), gridB);
   outColor = mix(outColor, grid_lines_color, gridC);
   
   outColor.a *= fade;
   if (outColor.a <= 0)
      discard;
}
)"
    };

    std::vector<GLuint> shaders;
    for (auto shader_type : { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER })
    {
        GLuint shader_module_id = glCreateShader(shader_type);
        glShaderSource(shader_module_id, 1, &shader_src[shaders.size()], nullptr);
        glCompileShader(shader_module_id);

        glAttachShader(m_shader_id, shader_module_id);
        shaders.push_back(shader_module_id);
    }
    glLinkProgram(m_shader_id);
    glUseProgram(m_shader_id);

    m_proj = glGetUniformLocation(m_shader_id, "matProj");
    m_view = glGetUniformLocation(m_shader_id, "matView");
    m_vp = glGetUniformLocation(m_shader_id, "matViewProj");
    m_inv_view = glGetUniformLocation(m_shader_id, "matViewInverse");
    m_view_pos = glGetUniformLocation(m_shader_id, "vViewPosition");
    m_grid_size = glGetUniformLocation(m_shader_id, "gridSize");
    m_min_step_loc = glGetUniformLocation(m_shader_id, "min_step");
    m_grid_lines_color_loc = glGetUniformLocation(m_shader_id, "grid_lines_color");
    m_plane_orient_loc = glGetUniformLocation(m_shader_id, "plane_orient");
    for (auto shader_id : shaders)
        glDeleteShader(shader_id);

    const int num = 8;
    std::vector<GfVec2f> plane_mesh;
    plane_mesh.reserve((num + 1) * (num + 1));
    std::vector<GLuint> plane_indices;
    plane_indices.resize(2 * num * num + 3 * num + 1);
    size_t i = 0;
    for (int h = 0; h <= num; h++)
    {
        for (int v = 0; v <= num; v++)
        {
            plane_mesh.emplace_back(v * 0.25f - 1, h * 0.25f - 1);

            if (h != num)
            {
                plane_indices[i++] = v + (num + 1) * h;
                plane_indices[i++] = v + (num + 1) * (h + 1);
            }
        }
        plane_indices[i++] = plane_indices.size();
    }

    m_plane_indices_size = plane_indices.size();

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, plane_mesh.size() * sizeof(GfVec2f), plane_mesh.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, plane_indices.size() * sizeof(GLuint), plane_indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GfVec2f), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

ViewportGrid::~ViewportGrid()
{
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteProgram(m_shader_id);
}

void OPENDCC_NAMESPACE::ViewportGrid::set_grid_color(const PXR_NS::GfVec4f& color)
{
    m_grid_lines_color = color;
}

void ViewportGrid::set_up_axis(const TfToken& up_axis)
{
    if (up_axis == UsdGeomTokens->z)
        m_plane_orientation = 0;
    else
        m_plane_orientation = 1;
}

void ViewportGrid::set_plane(Plane plane)
{
    m_plane_orientation = static_cast<int>(plane);
}

void ViewportGrid::draw(const GfFrustum& frustum)
{
    if (!m_enable)
        return;

    glPushDebugGroup(GL_DEBUG_SOURCE_THIRD_PARTY, 0, -1, "ViewportGrid");

    GfMatrix4f proj;
    GfMatrix4f view;

    // We would like to avoid grid clipping that might occur in orthographic projection when we increase
    // orthographic size. We adjust viewing frustum in such a way that its position becomes the point of interest and
    // near-far range becomes [-current_far/2; current_far/2]. It allows us to draw a grid behind the camera continuously.
    if (frustum.GetProjectionType() == GfFrustum::ProjectionType::Orthographic)
    {
        GfFrustum offsetted_frustum = frustum;
        const auto new_max = frustum.GetNearFar().GetMax() * 0.5;
        offsetted_frustum.SetNearFar(GfRange1d(-new_max, new_max));
        offsetted_frustum.SetPosition(frustum.ComputeLookAtPoint());
        proj = GfMatrix4f(offsetted_frustum.ComputeProjectionMatrix());
        view = GfMatrix4f(offsetted_frustum.ComputeViewMatrix());
    }
    else
    {
        proj = GfMatrix4f(frustum.ComputeProjectionMatrix());
        view = GfMatrix4f(frustum.ComputeViewMatrix());
    }

    const auto view_proj = GfMatrix4f(frustum.ComputeViewMatrix() * frustum.ComputeProjectionMatrix());
    const auto view_inv = view.GetInverse();
    const auto cam_pos = view_inv.GetRow3(3);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PRIMITIVE_RESTART);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glUseProgram(m_shader_id);

    glUniform3f(m_view_pos, cam_pos[0], cam_pos[1], cam_pos[2]);
    glUniformMatrix4fv(m_vp, 1, GL_FALSE, &view_proj[0][0]);
    glUniformMatrix4fv(m_proj, 1, GL_FALSE, &proj[0][0]);
    glUniformMatrix4fv(m_view, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(m_inv_view, 1, GL_FALSE, &view_inv[0][0]);
    if (frustum.GetProjectionType() == GfFrustum::ProjectionType::Perspective)
        glUniform1f(m_grid_size, frustum.GetNearFar().GetMax());
    else
        glUniform1f(m_grid_size, (1.0 / std::min(std::abs(proj[0][0]), std::abs(proj[1][1]))) * frustum.GetNearFar().GetMax());
    glUniform4f(m_grid_lines_color_loc, m_grid_lines_color[0], m_grid_lines_color[1], m_grid_lines_color[2], m_grid_lines_color[3]);
    glUniform1f(m_min_step_loc, m_min_step);
    glUniform1i(m_plane_orient_loc, m_plane_orientation);
    glPrimitiveRestartIndex(m_plane_indices_size);
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLE_STRIP, m_plane_indices_size, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_PRIMITIVE_RESTART);

    glPopDebugGroup();
}

void OPENDCC_NAMESPACE::ViewportGrid::set_min_step(double min_step)
{
    m_min_step = min_step;
}

void OPENDCC_NAMESPACE::ViewportGrid::set_enabled(bool enable)
{
    m_enable = enable;
}

OPENDCC_NAMESPACE_CLOSE
