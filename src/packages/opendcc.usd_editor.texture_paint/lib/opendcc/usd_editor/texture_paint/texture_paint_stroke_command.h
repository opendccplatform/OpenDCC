/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2i.h>
#include "OpenImageIO/imagebuf.h"
#include "OpenImageIO/typedesc.h"

OPENDCC_NAMESPACE_OPEN
class TextureData;
class TexturePainter;

class TexturePaintStrokeCommand
    : public base::commands::UndoCommand
    , public base::commands::ToolCommand
{
public:
    TexturePaintStrokeCommand(const TextureData& texture_data, const TexturePainter& painter, bool write_to_files);
    void undo() override;
    void redo() override;

    base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;
    base::commands::CommandArgs make_args() const override;

private:
    void exec(bool undo) const;

    struct PixelInfo
    {
        PXR_NS::GfVec4f orig_color;
        PXR_NS::GfVec4f new_color;
        int tile_id = 0;
        int x = 0;
        int y = 0;
    };
    struct TextureBuffer
    {
        std::weak_ptr<OIIO::ImageBuf> buf;
        std::string file;
    };

    std::vector<PixelInfo> m_pixels;
    std::unordered_map<int, TextureBuffer> m_texture_buffers;
    OIIO::ImageSpec m_img_spec;
    PXR_NS::GfVec2i m_dims;
    OIIO::TypeDesc m_src_descr;
    int m_src_channels = 0;
    bool m_write_to_files = false;
};

OPENDCC_NAMESPACE_CLOSE
