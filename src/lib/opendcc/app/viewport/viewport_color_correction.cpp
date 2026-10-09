// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/pxr.h>
#if PXR_VERSION < 2108
#include <GL/glew.h>
#else
#include <pxr/imaging/garch/gl.h>
#include "opendcc/base/logging/logger.h"
#endif
#include "opendcc/app/viewport/viewport_color_correction.h"
#include <OpenColorIO/OpenColorIO.h>
#include <QtMath>

OPENDCC_NAMESPACE_OPEN

namespace OCIO = OCIO_NAMESPACE;
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    const char* vertex_src = R"(#version 140
in vec4 inPos;
in vec2 inUV;

out vec2 uv;
void main(void)
{
    gl_Position = inPos;
    uv = inUV;
}
)";
#if PXR_VERSION < 2108
    const char* base_fragment_src = R"(in vec2 uv;
uniform sampler2D inColor;
uniform sampler3D inLUT3d;
out vec4 outColor;

#define texture3D texture

vec3 FloatToSRGB(vec3 val)
{
    val = mix((val * 12.92),
              (1.055 * pow(val, vec3(1.0/2.4)) - 0.055),
              step(0.0031308, val));
    return val;
}

#if defined(USE_OCIO)
vec4 OCIODisplay(in vec4 inPixel, const sampler3D lut3d);
#endif

void main(void)
{
    vec4 color = texture(inColor, uv);
#if defined(USE_OCIO)
    color = OCIODisplay(color, inLUT3d);
#else
    color.rgb = FloatToSRGB(color.rgb);
#endif
    outColor = color;
}
)";
#else
    const char* base_fragment_src = R"(in vec2 uv;
uniform sampler2D inColor;

out vec4 outColor;

#define texture3D texture

vec3 FloatToSRGB(vec3 val)
{
    val = mix((val * 12.92),
              (1.055 * pow(val, vec3(1.0/2.4)) - 0.055),
              step(0.0031308, val));
    return val;
}

#if defined(USE_OCIO)
vec4 OCIODisplay(vec4 inPixel);
#endif

void main(void)
{
    vec4 color = texture(inColor, uv);
#if defined(USE_OCIO)
    color = OCIODisplay(color);
#else
    color.rgb = FloatToSRGB(color.rgb);
#endif
    outColor = color;
}
)";
#endif
};

ViewportColorCorrection::ViewportColorCorrection(ColorCorrectionMode mode, const std::string& ocio_view, const std::string& input_color_space,
                                                 float gamma, float exposure)
    : m_mode(mode)
    , m_view(ocio_view)
    , m_input_color_space(input_color_space)
    , m_gamma(gamma)
    , m_exposure(exposure)
{
}

ViewportColorCorrection::~ViewportColorCorrection()
{
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (m_intermediate_texture)
        glDeleteTextures(1, &m_intermediate_texture);
    if (m_texture_3d_lut)
        glDeleteTextures(1, &m_texture_3d_lut);
    if (m_vertex_buffer)
        glDeleteBuffers(1, &m_vertex_buffer);
    if (m_shader_program)
        glDeleteProgram(m_shader_program);
    if (m_intermediate_framebuffer)
        glDeleteFramebuffers(1, &m_intermediate_framebuffer);
}

void ViewportColorCorrection::apply(ViewportViewPtr viewport_view)
{
    if (m_mode == ColorCorrectionMode::DISABLED)
        return;

    initialize(viewport_view);
    blit_intermediate();
    apply_correction();
}

void ViewportColorCorrection::apply(int width, int height)
{
    if (m_mode == ColorCorrectionMode::DISABLED)
        return;

    initialize(width, height);
    blit_intermediate();
    apply_correction();
}

void ViewportColorCorrection::set_gamma(float gamma)
{
    m_gamma = gamma;
    if (m_shader_program)
    {
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
    }
}

void ViewportColorCorrection::set_exposure(float exposure)
{
    if (m_exposure == exposure)
        return;

    m_exposure = exposure;
    if (m_shader_program)
    {
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
    }
}

void ViewportColorCorrection::set_mode(ColorCorrectionMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    if (m_shader_program)
    {
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
    }
}

ViewportColorCorrection::ColorCorrectionMode ViewportColorCorrection::get_mode() const
{
    return m_mode;
}

void ViewportColorCorrection::set_ocio_view(const std::string& view)
{
    if (m_view == view)
        return;

    m_view = view;
    if (m_shader_program)
    {
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
    }
}

void ViewportColorCorrection::set_color_space(const std::string& color_space)
{
    if (m_input_color_space == color_space)
        return;

    m_input_color_space = color_space;
    if (m_shader_program)
    {
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
    }
}

void ViewportColorCorrection::initialize(ViewportViewPtr viewport_view)
{
    init_vertex_buffer();
    init_shader();
    init_framebuffer(viewport_view);
}

void ViewportColorCorrection::initialize(int width, int height)
{
    init_vertex_buffer();
    init_shader();
    init_framebuffer(width, height);
}

void ViewportColorCorrection::init_vertex_buffer()
{
    if (m_vertex_buffer != 0)
        return;
    //     pos             uv
    static const float vertices[] = { -1, 3, -1, 1, 0, 2, -1, -1, -1, 1, 0, 0, 3, -1, -1, 1, 2, 0 };
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vertex_buffer);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices[0], GL_STATIC_DRAW);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 6, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 6, reinterpret_cast<void*>(sizeof(float) * 4));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ViewportColorCorrection::init_shader()
{
    if (m_shader_program != 0)
        return;

    const auto use_OCIO = m_mode == ColorCorrectionMode::OCIO;
    std::string frag_src = "#version 140\n";
    if (use_OCIO)
    {
        frag_src += "#define USE_OCIO\n";
        frag_src += base_fragment_src + get_ocio_shader_text();
    }
    else
    {
        frag_src += base_fragment_src;
    }

    m_shader_program = glCreateProgram();

    std::vector<GLuint> shaders;
    const char* sources[] = { vertex_src, frag_src.c_str() };

    auto fail_cleanup = [this, &shaders] {
        glDeleteProgram(m_shader_program);
        m_shader_program = 0;
        for (auto id : shaders)
        {
            glDeleteShader(id);
        }
        set_mode(ColorCorrectionMode::DISABLED);
    };

    for (auto type : { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER })
    {
        const auto id = glCreateShader(type);
        glShaderSource(id, 1, &sources[shaders.size()], nullptr);
        glCompileShader(id);
        if (verify_shader_compilation(id))
        {
            glAttachShader(m_shader_program, id);
            shaders.push_back(id);
        }
        else
        {
            fail_cleanup();
            return;
        }
    }
    glLinkProgram(m_shader_program);
    if (!verify_shader_program_link(m_shader_program))
    {
        fail_cleanup();
        return;
    }
    m_color_in_loc = glGetUniformLocation(m_shader_program, "inColor");
#if OCIO_VERSION_MAJOR < 2
    m_lut3d_in_loc = glGetUniformLocation(m_shader_program, "inLUT3d");
#else
    m_lut3d_in_loc = glGetUniformLocation(m_shader_program, "ocio_lut3d_0Sampler");
#endif

    for (auto id : shaders)
        glDeleteShader(id);
}

std::string ViewportColorCorrection::get_ocio_shader_text()
{
    auto config = OCIO::GetCurrentConfig();
    if (!config)
    {
        OPENDCC_ERROR("Failed to find OCIO config.");
        return "";
    }

    auto display = config->getDefaultDisplay();

    bool found_view_name = false;

    if (!m_view.empty())
    {
        for (int i = 0; i < config->getNumViews(display); i++)
        {
            if (m_view == config->getView(display, i))
            {
                found_view_name = true;
                break;
            }
        }
    }

    auto view = (m_view.empty() || !found_view_name) ? config->getDefaultView(display) : m_view.c_str();
    auto color_space = m_input_color_space;
    if (color_space.empty())
    {
        auto cs = config->getColorSpace("default");
        color_space = cs ? cs->getName() : OCIO::ROLE_SCENE_LINEAR;
    }
    else
    {
        auto cs = config->getColorSpace(color_space.c_str());
        if (!cs)
        {
            color_space = config->getColorSpaceNameByIndex(0);
        }
    }
#if OCIO_VERSION_HEX >= 0x02000000
    auto transform = OCIO::DisplayViewTransform::Create();
    transform->setDisplay(display);
    transform->setView(view);
    transform->setSrc(color_space.c_str());
    float gain = powf(2.0f, m_exposure);
    const double slope4d[] = { gain, gain, gain, 1.0 };
    double m44[16];
    double offset4[4];
    OCIO::MatrixTransform::Scale(m44, offset4, slope4d);
    OCIO::MatrixTransformRcPtr mtx = OCIO::MatrixTransform::Create();
    mtx->setMatrix(m44);
    mtx->setOffset(offset4);

    auto vpt = OCIO::LegacyViewingPipeline::Create();
    vpt->setDisplayViewTransform(transform);
    vpt->setLinearCC(mtx);

    // gamma

    double exponent = qBound(0.01, 1.0 / m_gamma, 100.0);
    const double exponent4d[] = { exponent, exponent, exponent, 1.0 };
    OCIO::ExponentTransformRcPtr cc = OCIO::ExponentTransform::Create();
    cc->setValue(exponent4d);
    vpt->setDisplayCC(cc);

    OCIO::ConstProcessorRcPtr processor;
    try
    {
        processor = vpt->getProcessor(config);
    }
    catch (OCIO::Exception& e)
    {
        OPENDCC_ERROR(e.what());
        return "";
    }
#if OCIO_VERSION_MINOR >= 1
    OCIO::GpuShaderDescRcPtr desc = OCIO::GpuShaderDesc::CreateShaderDesc();
    const auto gpu = processor->getOptimizedLegacyGPUProcessor(OCIO::OPTIMIZATION_DEFAULT, m_lut3d_size);
#else
    OCIO::GpuShaderDescRcPtr desc = OCIO::GpuShaderDesc::CreateLegacyShaderDesc(m_lut3d_size);
    const auto gpu = processor->getDefaultGPUProcessor();
#endif
    desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_3);
    desc->setFunctionName("OCIODisplay");
    gpu->extractGpuShaderInfo(desc);

    // An optimized GPUProcessor instance emulates the OCIO v1 GPU path. This approach
    // bakes some of the ops into a single Lut3D and so is less accurate than the current GPU
    // processing methods.
    // OCIO v2 assumes you'll use the OglApp helpers
    // these are unusable from a Qt backend, because they rely on GLUT/GLFW
    // ociodisplay original pipeline:
    // https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/508b3f4a0618435aeed2f45058208bdfa99e0887/src/apps/ociodisplay/main.cpp
    // ociodisplay new pipeline is a single call:
    // https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/ffddc3341f5775c7866fe2c93275e1d5e0b0540f/src/apps/ociodisplay/main.cpp#L427
    // we need to replicate this loop:
    // https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/dd59baf555656e09f52c3838e85ccf154497ec1d/src/libutils/oglapphelpers/oglapp.cpp#L191-L223
    // calls functions from here:
    // https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/dd59baf555656e09f52c3838e85ccf154497ec1d/src/libutils/oglapphelpers/glsl.cpp

    if (m_texture_3d_lut != 0)
        glDeleteTextures(1, &m_texture_3d_lut);
    m_texture_3d_lut = 0;

    // Process the 3D LUT first.

    // We expect only one 3D texture and nothing else.
    if (desc->getNum3DTextures() == 1)
    {
        // 1. Get the information of the 3D LUT.

        const char* textureName = nullptr;
        const char* samplerName = nullptr;
        unsigned edgelen = 0;
        OCIO::Interpolation interpolation = OCIO::INTERP_LINEAR;
        desc->get3DTexture(0, textureName, samplerName, edgelen, interpolation);

        if (!textureName || !*textureName || !samplerName || !*samplerName || edgelen == 0)
        {
            return "";
        }

        const float* values = nullptr;
        desc->get3DTextureValues(0, values);
        if (!values)
        {
            return "";
        }

        // 2. Allocate the 3D LUT.

        unsigned texId = 0;
        {
            if (values == nullptr)
            {
                return "";
            }

            glGenTextures(1, &texId);

            glActiveTexture(GL_TEXTURE0);

            glBindTexture(GL_TEXTURE_3D, texId);

            {
                if (interpolation == OCIO::INTERP_NEAREST)
                {
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                else
                {
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                }

                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            }

            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F_ARB, edgelen, edgelen, edgelen, 0, GL_RGB, GL_FLOAT, values);
        }

        // 3. Keep the texture id for the later enabling.
        m_texture_3d_lut = texId;
    }
    return std::string(desc->getShaderText());
#else
    auto transform = OCIO::DisplayTransform::Create();
    transform->setDisplay(display);
    transform->setView(view);
    transform->setInputColorSpaceName(color_space.c_str());
    transform->setLooksOverrideEnabled(false);

    float gain = powf(2.0f, m_exposure);
    const float slope4f[] = { gain, gain, gain, 1.0 };
    float m44[16];
    float offset4[4];
    OCIO::MatrixTransform::Scale(m44, offset4, slope4f);
    OCIO::MatrixTransformRcPtr mtx = OCIO::MatrixTransform::Create();
    mtx->setValue(m44, offset4);
    transform->setLinearCC(mtx);

    // gamma

    float exponent = 1.0f / std::max(1e-6f, m_gamma);
    const float exponent4f[] = { exponent, exponent, exponent, 1.0 };
    OCIO::ExponentTransformRcPtr cc = OCIO::ExponentTransform::Create();
    cc->setValue(exponent4f);
    transform->setDisplayCC(cc);
    auto processor = config->getProcessor(transform);
    OCIO::GpuShaderDesc desc;
    desc.setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_3);
    desc.setFunctionName("OCIODisplay");
    desc.setLut3DEdgeLen(m_lut3d_size);
    int lut_dim = 3 * m_lut3d_size * m_lut3d_size * m_lut3d_size;
    std::vector<float> lut3d(lut_dim);
    processor->getGpuLut3D(&lut3d[0], desc);

    if (m_texture_3d_lut != 0)
        glDeleteTextures(1, &m_texture_3d_lut);

    GLint restore_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &restore_texture);
    glGenTextures(1, &m_texture_3d_lut);
    glBindTexture(GL_TEXTURE_3D, m_texture_3d_lut);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, m_lut3d_size, m_lut3d_size, m_lut3d_size, 0, GL_RGB, GL_FLOAT, &lut3d[0]);
    glBindTexture(GL_TEXTURE_3D, restore_texture);

    return std::string(processor->getGpuShaderText(desc));
#endif
}

void ViewportColorCorrection::init_framebuffer(ViewportViewPtr viewport_view)
{
    if (!viewport_view)
        return;

    const auto dim = viewport_view->get_viewport_dimensions();
    init_framebuffer(dim.width, dim.height);
}

void ViewportColorCorrection::init_framebuffer(int width, int height)
{
    const auto fbo_size = GfVec2i(width, height);
    const auto update_texture = fbo_size != m_framebuffer_size || m_intermediate_texture == 0;
    if (update_texture)
    {
        if (m_intermediate_texture != 0)
        {
            glDeleteTextures(1, &m_intermediate_texture);
            m_intermediate_texture = 0;
        }

        m_framebuffer_size = fbo_size;
        GLint restore_texture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restore_texture);

        glGenTextures(1, &m_intermediate_texture);
        glBindTexture(GL_TEXTURE_2D, m_intermediate_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_framebuffer_size[0], m_framebuffer_size[1], 0, GL_RGBA, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_2D, restore_texture);
    }

    const auto update_fbo = m_intermediate_framebuffer == 0;
    if (update_fbo)
        glGenFramebuffers(1, &m_intermediate_framebuffer);

    if (update_texture || update_fbo)
    {
        GLint restore_read_fb, restore_draw_fb;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restore_read_fb);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restore_draw_fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_intermediate_framebuffer);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_intermediate_texture, 0);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, restore_read_fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restore_draw_fb);
    }
}

void ViewportColorCorrection::blit_intermediate()
{
    GLint restore_read_fb, restore_draw_fb;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restore_read_fb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restore_draw_fb);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, restore_draw_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_intermediate_framebuffer);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, m_framebuffer_size[0], m_framebuffer_size[1], 0, 0, m_framebuffer_size[0], m_framebuffer_size[1], GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, restore_read_fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restore_draw_fb);
}

void ViewportColorCorrection::apply_correction()
{
    glUseProgram(m_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_intermediate_texture);
    glUniform1i(m_color_in_loc, 0);
    if (m_mode == ColorCorrectionMode::OCIO && m_texture_3d_lut)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, m_texture_3d_lut);
        glUniform1i(m_lut3d_in_loc, 1);
    }

    glBindVertexArray(m_vao);

    GLboolean restore_depth_write_mask, restore_stencil_write_mask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &restore_depth_write_mask);
    glGetBooleanv(GL_STENCIL_WRITEMASK, &restore_stencil_write_mask);
    glDepthMask(GL_FALSE);
    glStencilMask(GL_FALSE);

    GLint restore_depth_func;
    glGetIntegerv(GL_DEPTH_FUNC, &restore_depth_func);
    glDepthFunc(GL_ALWAYS);

    GLint restore_viewport[4] = { 0 };
    glGetIntegerv(GL_VIEWPORT, restore_viewport);
    glViewport(0, 0, m_framebuffer_size[0], m_framebuffer_size[1]);

    GLboolean restore_blend_enabled;
    glGetBooleanv(GL_BLEND, &restore_blend_enabled);
    glDisable(GL_BLEND);

    GLboolean restore_alpha_to_coverage;
    glGetBooleanv(GL_SAMPLE_ALPHA_TO_COVERAGE, &restore_alpha_to_coverage);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (restore_alpha_to_coverage)
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    if (restore_blend_enabled)
        glEnable(GL_BLEND);

    glViewport(restore_viewport[0], restore_viewport[1], restore_viewport[2], restore_viewport[3]);
    glDepthFunc(restore_depth_func);
    glDepthMask(restore_depth_write_mask);
    glStencilMask(restore_stencil_write_mask);

    glBindVertexArray(0);

    glUseProgram(0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (m_mode == ColorCorrectionMode::OCIO && m_texture_3d_lut)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
}

bool ViewportColorCorrection::verify_shader_compilation(GLuint shader_id) const
{
    GLint is_compiled = 0;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &is_compiled);
    if (is_compiled == GL_FALSE)
    {
        GLint max_len = 0;
        glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &max_len);

        // The maxLength includes the NULL character
        std::string error_log;
        error_log.resize(max_len);
        glGetShaderInfoLog(shader_id, max_len, &max_len, error_log.data());

        OPENDCC_ERROR("Failed to compile color correction shader: {}", error_log);
        return false;
    }
    return true;
}

bool ViewportColorCorrection::verify_shader_program_link(GLuint shader_program_id) const
{
    GLint is_linked = 0;
    glGetProgramiv(shader_program_id, GL_LINK_STATUS, &is_linked);
    if (is_linked == GL_FALSE)
    {
        GLint max_len = 0;
        glGetProgramiv(shader_program_id, GL_INFO_LOG_LENGTH, &max_len);

        // The maxLength includes the NULL character
        std::string error_log;
        error_log.resize(max_len);
        glGetProgramInfoLog(shader_program_id, max_len, &max_len, error_log.data());

        OPENDCC_ERROR("Failed to link color correction shader: {}", error_log);

        // Provide the infolog in whatever manner you deem best.
        // Exit with failure.
        return false;
    }
    return true;
}

OPENDCC_NAMESPACE_CLOSE
