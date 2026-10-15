// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/texture_paint/texture_paint_stroke_command.h"
#include "OpenImageIO/imagebuf.h"
#include "OpenImageIO/imageio.h"
#include "opendcc/usd_editor/texture_paint/texture_paint_tool_context.h"
#include "OpenImageIO/imagebufalgo.h"
#include "opendcc/app/ui/application_ui.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE
using base::commands::CommandArgs;
using base::commands::CommandResult;

using namespace OIIO;

TexturePaintStrokeCommand::TexturePaintStrokeCommand(const TextureData& texture_data, const TexturePainter& painter, bool write_to_files)
{
    m_command_name = "texture_paint_stroke";
    m_dims = texture_data.get_dimensions();
    m_write_to_files = write_to_files;
    for (const auto& tile : texture_data.get_image_data())
    {
        m_src_descr = tile.second->src_descr;
        m_src_channels = tile.second->src_channels;
        m_img_spec = tile.second->texture_buffer->spec();
        m_texture_buffers[tile.first] = { std::weak_ptr<OIIO::ImageBuf>(tile.second->texture_buffer), tile.second->texture_file };
    }
    for (const auto& b : painter.m_paint_buckets)
    {
        for (const auto& p : b.pixels)
        {
            const auto px_data = p.pixel_data;
            if (!px_data->touched)
                continue;
            TexturePaintStrokeCommand::PixelInfo pi;
            pi.orig_color = px_data->orig_color;
            px_data->img_data->texture_buffer->getpixel(px_data->x, px_data->y, pi.new_color.data());
            pi.tile_id = px_data->img_data->udim_index;
            pi.x = px_data->x;
            pi.y = px_data->y;
            m_pixels.emplace_back(pi);
        }
    }
}

void TexturePaintStrokeCommand::undo()
{
    exec(true);
}

void TexturePaintStrokeCommand::redo()
{
    exec(false);
}

CommandResult TexturePaintStrokeCommand::execute(const CommandArgs& args)
{
    return CommandResult(CommandResult::Status::SUCCESS);
}

CommandArgs TexturePaintStrokeCommand::make_args() const
{
    return CommandArgs();
}

void TexturePaintStrokeCommand::exec(bool undo) const
{
    bool dirty_viewport = false;

    struct FileWriter
    {
        TypeDesc desc;
        ImageSpec spec;
        ImageBuf img_buf;
        GfVec2i dims;
        int nchannels = 0;
    };
    std::unordered_map<std::string, FileWriter> file_buffers;
    if (m_write_to_files)
    {
        for (const auto& tex_buf : m_texture_buffers)
        {
            auto img_input = ImageInput::open(tex_buf.second.file);
            if (!img_input)
                continue;
            const auto& spec = img_input->spec();
            const auto desired_format = TypeDesc(TypeDesc::BASETYPE::UINT8, TypeDesc::AGGREGATE::SCALAR, 0);
            std::vector<uint8_t> pixels(spec.width * spec.height * spec.nchannels);
            const auto scanline_stride = spec.width * spec.nchannels * sizeof(uint8_t);
            if (!img_input->read_image(0, 0, 0, spec.nchannels, desired_format, pixels.data() + (spec.height - 1) * scanline_stride, OIIO::AutoStride,
                                       -scanline_stride))
            {
                continue;
            }

            ImageBuf src_img_buf(spec, pixels.data());
            FileWriter file_writer;
            file_writer.desc = src_img_buf.spec().format;
            file_writer.nchannels = spec.nchannels;
            if (file_writer.nchannels != 4)
            {
                int channel_order[4];
                float channel_values[] = { 0, 0, 0, 1 };
                std::string channel_names[] = { "R", "G", "B", "A" };
                for (int i = 0; i < 4; i++)
                    channel_order[i] = i < src_img_buf.spec().nchannels ? i : -1;

                file_writer.img_buf = ImageBufAlgo::channels(src_img_buf, 4, channel_order, channel_values, channel_names);
                if (!file_writer.img_buf.pixels_valid())
                    continue;
            }
            else
            {
                file_writer.img_buf.copy(src_img_buf); // = std::move(src_img_buf);
            }
            file_writer.dims = GfVec2i(spec.width, spec.height);
            file_writer.spec = spec;
            file_buffers[tex_buf.second.file] = std::move(file_writer);
        }
    }

    for (const auto& px : m_pixels)
    {
        const auto& tex_buf = m_texture_buffers.at(px.tile_id);
        if (!tex_buf.buf.expired())
        {
            tex_buf.buf.lock()->setpixel(px.x, px.y, undo ? px.orig_color.data() : px.new_color.data());
            dirty_viewport = true;
        }
        if (m_write_to_files)
        {
            auto& file_buf = file_buffers[tex_buf.file];
            file_buf.img_buf.setpixel(px.x, px.y, undo ? px.orig_color.data() : px.new_color.data());
        }
    }

    if (m_write_to_files)
    {
        for (const auto& file_buf : file_buffers)
        {
            if (auto out = ImageOutput::create(file_buf.first))
            {
                if (!out->open(file_buf.first, file_buf.second.spec))
                    continue;

                int scanline_size = file_buf.second.spec.scanline_bytes();
                if (file_buf.second.nchannels != file_buf.second.img_buf.nchannels())
                {
                    auto dst = ImageBufAlgo::channels(file_buf.second.img_buf, file_buf.second.nchannels, {});
                    out->write_image(file_buf.second.desc, (uint8_t*)dst.localpixels() + (file_buf.second.spec.height - 1) * scanline_size,
                                     AutoStride, -scanline_size);
                }
                else
                {
                    out->write_image(file_buf.second.desc,
                                     (uint8_t*)file_buf.second.img_buf.localpixels() + (file_buf.second.spec.height - 1) * scanline_size, AutoStride,
                                     -scanline_size);
                }
            }
        }
    }

    if (dirty_viewport)
    {
        if (auto ctx = dynamic_cast<TexturePaintToolContext*>(ApplicationUI::instance().get_current_viewport_tool()))
            ctx->update_material();
    }
}

OPENDCC_NAMESPACE_CLOSE
