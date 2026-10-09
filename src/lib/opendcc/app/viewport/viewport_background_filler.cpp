// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/pxr.h>
#if PXR_VERSION < 2108
#include <GL/glew.h>
#else
#include <pxr/imaging/garch/gl.h>
#endif
#include "opendcc/app/viewport/viewport_background_filler.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include <pxr/base/gf/gamma.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

const std::string SolidBackgroundFiller::s_background_color_key = "viewport.background.color";
const std::string GradientBackgroundFiller::s_gradient_top_key = "viewport.background.gradient_top";
const std::string GradientBackgroundFiller::s_gradient_bottom_key = "viewport.background.gradient_bottom";

ViewportBackgroundFiller::~ViewportBackgroundFiller()
{
    if (m_display_shader == 0 && m_display_vao == 0 && m_display_vbo == 0)
        return;

    if (m_gl_widget)
        m_gl_widget->makeCurrent();
    if (m_display_vao)
        glDeleteVertexArrays(1, &m_display_vao);
    if (m_display_vbo)
        glDeleteBuffers(1, &m_display_vbo);
    if (m_display_shader)
        glDeleteProgram(m_display_shader);
}

void ViewportBackgroundFiller::init_display_resources()
{
    if (m_display_shader != 0)
        return;

    static const std::array<const char*, 2> shader_src = {
        R"#(#version 330
in vec2 in_pos;
// colors[0] -- top, colors[1] -- bottom
uniform vec3 colors[2];
out vec3 out_color;
void main()
{
    gl_Position = vec4(in_pos, 1.0, 1.0);
    out_color = colors[gl_VertexID >> 1];
}
)#",
        R"#(#version 330
in vec3 out_color;
out vec4 outColor;
void main()
{
    outColor = vec4(out_color, 1.0);
}
)#"
    };

    m_display_shader = glCreateProgram();
    std::vector<GLuint> shaders;
    for (auto shader_type : { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER })
    {
        GLuint shader_module_id = glCreateShader(shader_type);
        glShaderSource(shader_module_id, 1, &shader_src[shaders.size()], nullptr);
        glCompileShader(shader_module_id);
        glAttachShader(m_display_shader, shader_module_id);
        shaders.push_back(shader_module_id);
    }
    glLinkProgram(m_display_shader);
    m_display_colors_loc = glGetUniformLocation(m_display_shader, "colors");
    for (auto shader_id : shaders)
        glDeleteShader(shader_id);

    static const std::array<GfVec2f, 4> quad_vertices = { GfVec2f(1, 1), GfVec2f(-1, 1), GfVec2f(1, -1), GfVec2f(-1, -1) };
    glGenVertexArrays(1, &m_display_vao);
    glGenBuffers(1, &m_display_vbo);
    glBindVertexArray(m_display_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_display_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GfVec2f), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void ViewportBackgroundFiller::draw_display_quad(const GfVec3f& top_color, const GfVec3f& bottom_color)
{
    init_display_resources();

    const GLboolean depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean multisample_enabled = glIsEnabled(GL_MULTISAMPLE);
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    GLboolean depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    GLint depth_func = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &depth_func);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    // The far-plane quad passes only where the scene left the cleared depth.
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glUseProgram(m_display_shader);
    const std::array<GfVec3f, 2> colors = { top_color, bottom_color };
    glUniform3fv(m_display_colors_loc, static_cast<GLsizei>(colors.size()), colors.data()->data());
    glBindVertexArray(m_display_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);

    if (!depth_test_enabled)
        glDisable(GL_DEPTH_TEST);
    if (!multisample_enabled)
        glDisable(GL_MULTISAMPLE);
    if (blend_enabled)
        glEnable(GL_BLEND);
    glDepthFunc(depth_func);
    glDepthMask(depth_mask);
}

SolidBackgroundFiller::SolidBackgroundFiller(ViewportGLWidget* widget)
    : ViewportBackgroundFiller(widget)
{
    auto settings = Application::instance().get_settings();
    m_cid = settings->register_setting_changed(s_background_color_key, [this](const std::string&, const Settings::Value& val, Settings::ChangeType) {
        GfVec3f color;
        if (!TF_VERIFY(val.try_get<GfVec3f>(&color), "Failed to extract GfVec3f from \"viewport.background.color\" setting."))
            return;

        update(color);
    });
    const auto& config = Application::instance().get_app_config();
    const auto default_color_array = config.get_array<double>("settings.viewport.background.default_color", std::vector<double> { 0.3, 0.3, 0.3 });
    update(settings->get(s_background_color_key, GfVec3f(default_color_array[0], default_color_array[1], default_color_array[2])));
}

SolidBackgroundFiller::~SolidBackgroundFiller()
{
    Application::instance().get_settings()->unregister_setting_changed(s_background_color_key, m_cid);
}

void SolidBackgroundFiller::draw()
{
    const auto linear = GfConvertDisplayToLinear(m_display_color);
    glClearColor(linear[0], linear[1], linear[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SolidBackgroundFiller::draw_display()
{
    draw_display_quad(m_display_color, m_display_color);
}

void SolidBackgroundFiller::update(const GfVec3f& color)
{
    if (GfIsClose(m_display_color, color, 0.0001))
        return;

    m_display_color = color;
    m_gl_widget->update();
}

GradientBackgroundFiller::GradientBackgroundFiller(ViewportGLWidget* widget)
    : ViewportBackgroundFiller(widget)
{
    m_gl_widget->makeCurrent();
    m_gradient_top_cid = Application::instance().get_settings()->register_setting_changed(
        s_gradient_top_key, [this](const std::string&, const Settings::Value& val, Settings::ChangeType) {
            m_gl_widget->makeCurrent();
            GfVec3f color;
            if (!TF_VERIFY(val.try_get<GfVec3f>(&color), "Failed to extract GfVec3f from \"viewport.background.gradient_top\" setting."))
                return;

            if (GfIsClose(m_gradient_colors[0], color, 0.0001))
                return;

            m_gradient_colors[0] = color;
            m_gl_widget->update();
        });
    m_gradient_bottom_cid = Application::instance().get_settings()->register_setting_changed(
        s_gradient_bottom_key, [this](const std::string&, const Settings::Value& val, Settings::ChangeType) {
            m_gl_widget->makeCurrent();
            GfVec3f color;
            if (!TF_VERIFY(val.try_get<GfVec3f>(&color), "Failed to extract GfVec3f from \"viewport.background.gradient_bottom\" setting."))
                return;

            if (GfIsClose(m_gradient_colors[1], color, 0.0001))
                return;

            m_gradient_colors[1] = color;
            m_gl_widget->update();
        });

    m_shader_id = glCreateProgram();
    static const std::array<const char*, 2> shader_src = {
        R"#(#version 330
	in vec2 in_pos;
	// gradient_colors[0] -- top color
	// gradient_colors[1] -- bottom color
	uniform vec3 gradient_colors[2];

	out vec3 out_color;

	void main()
	{
		gl_Position = vec4(in_pos, 0, 1);
		out_color = gradient_colors[gl_VertexID >> 1];
	}
	)#",

        R"#(#version 330
	in vec3 out_color;
	out vec4 outColor;

	void main()
	{
		outColor = vec4(out_color, 1);
	}
	)#"
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
    m_gradient_colors_location = glGetUniformLocation(m_shader_id, "gradient_colors");

    for (auto shader_id : shaders)
        glDeleteShader(shader_id);

    static const std::array<GfVec2f, 4> quad_vertices = { GfVec2f(1, 1), GfVec2f(-1, 1), GfVec2f(1, -1), GfVec2f(-1, -1) };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GfVec2f), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    m_gradient_colors[0] = Application::instance().get_settings()->get(s_gradient_top_key, GfVec3f(1, 1, 1));
    m_gradient_colors[1] = Application::instance().get_settings()->get(s_gradient_bottom_key, GfVec3f(0, 0, 0));
}

GradientBackgroundFiller::~GradientBackgroundFiller()
{
    m_gl_widget->makeCurrent();
    Application::instance().get_settings()->unregister_setting_changed(s_gradient_top_key, m_gradient_top_cid);
    Application::instance().get_settings()->unregister_setting_changed(s_gradient_bottom_key, m_gradient_bottom_cid);

    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteProgram(m_shader_id);
}

void GradientBackgroundFiller::draw()
{
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(m_shader_id);
    const std::array<GfVec3f, 2> linear = { GfConvertDisplayToLinear(m_gradient_colors[0]), GfConvertDisplayToLinear(m_gradient_colors[1]) };
    glUniform3fv(m_gradient_colors_location, static_cast<GLsizei>(linear.size()), linear.data()->data());
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    glUseProgram(0);
}

void GradientBackgroundFiller::draw_display()
{
    draw_display_quad(m_gradient_colors[0], m_gradient_colors[1]);
}

OPENDCC_NAMESPACE_CLOSE
