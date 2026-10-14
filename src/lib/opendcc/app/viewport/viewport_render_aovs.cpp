// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/pxr.h>
#if PXR_VERSION < 2108
#include <pxr/imaging/glf/glew.h>
#else
#include <pxr/imaging/garch/glApi.h>
#endif
#include "opendcc/app/viewport/viewport_render_aovs.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/viewport/viewport_engine_proxy.h"
#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hdx/hgiConversions.h>

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.render");

static size_t get_data_size_of_format(HgiFormat format)
{
#if PXR_VERSION < 2108
    return HgiDataSizeOfFormat(format);
#else
    return HgiGetDataSizeOfFormat(format);
#endif
}

void ViewportRenderAOVs::post_frame(const ViewportHydraEngineParams& params, std::shared_ptr<ViewportEngineProxy> engine)
{
    m_aovs.clear();
    for (const auto& aov_name : engine->get_renderer_aovs())
    {
        auto texture_buffer = engine->get_aov_texture(aov_name);
        if (!texture_buffer)
        {
            OPENDCC_ERROR("Failed to get texture handle of '{}' aov.", aov_name.GetText());
            continue;
        }
        AOV aov;
        aov.name = aov_name;

        auto resource = texture_buffer->GetResource(false);
        if (!resource.IsEmpty() && resource.IsHolding<HgiTextureHandle>())
        {
            auto tex_handle = resource.UncheckedGet<HgiTextureHandle>();
            aov.desc = tex_handle->GetDescriptor();
            aov.data = read_texture(tex_handle);
            if (m_flip)
            {
                const auto stride = HdDataSizeOfFormat(texture_buffer->GetFormat()) * aov.desc.dimensions[0];
                size_t offset = (aov.desc.dimensions[1] - 1) * stride;
                auto tmp = new uint8_t[stride];
                for (int y = 0; y < aov.desc.dimensions[1] / 2; y++, offset -= stride)
                {
                    std::memcpy(tmp, aov.data.data() + stride * y, stride);
                    std::memcpy(aov.data.data() + stride * y, aov.data.data() + offset, stride);
                    std::memcpy(aov.data.data() + offset, tmp, stride);
                }
                delete[] tmp;
            }
        }
        else
        {
            GfVec3i dim((int)texture_buffer->GetWidth(), texture_buffer->GetHeight(), texture_buffer->GetDepth());
            HgiTextureDesc tex_desc;
            tex_desc.dimensions = dim;
            tex_desc.initialData = nullptr;
            tex_desc.format = HdxHgiConversions::GetHgiFormat(texture_buffer->GetFormat());
            tex_desc.layerCount = 1;
            tex_desc.mipLevels = 1;
            tex_desc.pixelsByteSize = dim[0] * dim[1] * dim[2] * HdDataSizeOfFormat(texture_buffer->GetFormat());
            tex_desc.sampleCount = HgiSampleCount1;
            tex_desc.usage = HgiTextureUsageBitsShaderWrite;
            aov.desc = tex_desc;
            aov.data.resize(tex_desc.pixelsByteSize);

            uint8_t* buffer = reinterpret_cast<uint8_t*>(texture_buffer->Map());
            if (m_flip)
            {
                const auto stride = HdDataSizeOfFormat(texture_buffer->GetFormat()) * aov.desc.dimensions[0];
                size_t offset = (aov.desc.dimensions[1] - 1) * stride;
                for (int y = 0; y < aov.desc.dimensions[1]; y++, offset -= stride)
                    std::memcpy(aov.data.data() + stride * y, buffer + offset, stride);
            }
            else
            {
                memcpy(aov.data.data(), texture_buffer->Map(), tex_desc.pixelsByteSize);
            }
            texture_buffer->Unmap();
        }

        m_aovs.emplace_back(std::move(aov));
    }
}

bool OPENDCC_NAMESPACE::ViewportRenderAOVs::is_flipped() const
{
    return m_flip;
}

void OPENDCC_NAMESPACE::ViewportRenderAOVs::flip(bool flip)
{
    m_flip = flip;
}

const std::vector<OPENDCC_NAMESPACE::ViewportRenderAOVs::AOV>& OPENDCC_NAMESPACE::ViewportRenderAOVs::get_aovs() const
{
    return m_aovs;
}

std::vector<uint8_t> ViewportRenderAOVs::read_texture(const HgiTextureHandle& handle) const
{
    auto hgi = ViewportHydraEngine::get_hgi();
    const HgiTextureDesc& texture_desc = handle.Get()->GetDescriptor();
    const size_t format_byte_size = get_data_size_of_format(texture_desc.format);

    const size_t w = texture_desc.dimensions[0];
    const size_t h = texture_desc.dimensions[1];
    const size_t data_byte_size = w * h * format_byte_size;

    // For Metal the CPU buffer has to be rounded up to multiple of 4096 bytes.
    constexpr size_t bit_mask = 4096 - 1;
    const size_t aligned_byte_size = (data_byte_size + bit_mask) & (~bit_mask);

    std::vector<uint8_t> result(aligned_byte_size);
    const auto blit = hgi->CreateBlitCmds();
    HgiTextureGpuToCpuOp copy_op;
    copy_op.gpuSourceTexture = handle;
    copy_op.sourceTexelOffset = GfVec3i(0);
    copy_op.mipLevel = 0;
#if PXR_VERSION < 2108
    copy_op.startLayer = 0;
    copy_op.numLayers = 1;
#endif
    copy_op.cpuDestinationBuffer = result.data();
    copy_op.destinationByteOffset = 0;
    copy_op.destinationBufferByteSize = aligned_byte_size;
    blit->CopyTextureGpuToCpu(copy_op);
    hgi->SubmitCmds(blit.get());
    return result;
}

OPENDCC_NAMESPACE_CLOSE
