// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/oiio_thumbnail_cache.h"
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagecache.h>
#include <QRunnable>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QByteArray>
#include <QThreadPool>

OPENDCC_NAMESPACE_OPEN

namespace
{
    // https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/blob/1c10440bdfa104c7d205b6aec27c24ba199d5eb4/src/osltoy/qtutils.h
    QImage ImageBuf_to_QImage(const OIIO::ImageBuf& ib)
    {
        using namespace OIIO;
        if (ib.storage() == ImageBuf::UNINITIALIZED)
            return {};

        const ImageSpec& spec(ib.spec());
        QImage::Format format = QImage::Format_Invalid;
        if (spec.format == TypeDesc::UINT8)
        {
            if (spec.nchannels == 1)
                format = QImage::Format_Grayscale8;
            else if (spec.nchannels == 3)
                format = QImage::Format_RGB888;
            else if (spec.nchannels == 4)
                format = QImage::Format_RGBA8888;
        }
        if (format == QImage::Format_Invalid)
            return {};

        if (ib.cachedpixels())
            const_cast<ImageBuf*>(&ib)->make_writeable(true);
        return QImage((const uchar*)ib.localpixels(), spec.width, spec.height, (int)spec.scanline_bytes(), format);
    }

    // inspired by https://github.com/PixarAnimationStudios/USD/blob/release/pxr/usdImaging/usdImaging/textureUtils.cpp
    std::vector<std::tuple<int, QString>> get_udim_tiles(const QString& base_path, int tile_limit)
    {
        const auto pos = base_path.indexOf("<UDIM>");
        if (pos == -1)
            return {};

        auto formatString = base_path;
        formatString.replace(pos, 6, "%i");

        constexpr int startTile = 1001;
        const int endTile = startTile + tile_limit;
        std::vector<std::tuple<int, QString>> ret;
        ret.reserve(tile_limit);
        for (int t = startTile; t <= endTile; ++t)
        {
            QString path;
            path = QString::asprintf(formatString.toLocal8Bit().data(), t);
            if (QFileInfo::exists(path))
                ret.emplace_back(t - startTile, path);
        }
        ret.shrink_to_fit();
        return ret;
    }

    class ThumbnailLoaderTask : public QRunnable
    {
    public:
        ThumbnailLoaderTask(OIIO::ImageCache* oiio_cache, OIIOThumbnailCache* cache, const QString& file_path)
        {
            m_cache = cache;
            m_file_path = file_path;
            m_oiio_cache = oiio_cache;
        }

        void run() override
        {
            auto image_path = m_file_path;
            const auto pos = m_file_path.indexOf("<UDIM>");
            if (pos != -1)
            {
                auto result = get_udim_tiles(image_path, 50);
                if (result.size() > 0)
                    image_path = std::get<1>(result[0]);
                else
                    return;
            }

            OIIO::ImageBuf img_buf_src(image_path.toLocal8Bit().toStdString(), m_oiio_cache);

            const bool read_status = img_buf_src.read(0, 0, true, OIIO::TypeDesc::UINT8);
            if (!read_status)
                return;

            QImage image = ImageBuf_to_QImage(img_buf_src);

            if (!image.isNull())
            {
                QImage* image_scaled = new QImage(image.scaled(QSize(256, 256), Qt::KeepAspectRatio));
                m_cache->insert_image(m_file_path, image_scaled);
            }
        }

        OIIOThumbnailCache* m_cache = nullptr;
        QString m_file_path;
        OIIO::ImageCache* m_oiio_cache = nullptr;
    };

};

OIIOThumbnailCache::OIIOThumbnailCache(QObject* parent)
    : ThumbnailCache(parent)
{
    m_cache.setMaxCost(500);
    m_oiio_cache = OIIO::ImageCache::create(false);
}

OIIOThumbnailCache::~OIIOThumbnailCache()
{
    m_cache.clear();
    OIIO::ImageCache::destroy(m_oiio_cache);
}

bool OIIOThumbnailCache::has_image(const QString& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.contains(path);
}

void OIIOThumbnailCache::read_image_async(const QString& path)
{
    auto task = new ThumbnailLoaderTask(m_oiio_cache, this, path);
    QThreadPool::globalInstance()->start(task);
}

void OIIOThumbnailCache::insert_image(const QString& path, QImage* image)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cache.insert(path, image))
        Q_EMIT image_read(path);
}

QImage* OIIOThumbnailCache::read_image(const QString& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.object(path);
}

OPENDCC_NAMESPACE_CLOSE
