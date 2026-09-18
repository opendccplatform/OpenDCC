// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "audio_decoder.h"

#include <QDebug>
#include <QUrl>
#include <climits>
#include <cmath>

AudioDecoder::AudioDecoder(QObject* parent /*= nullptr*/)
{
    m_audio_decoder = new QAudioDecoder(this);
    connect(m_audio_decoder, &QAudioDecoder::finished, this, &AudioDecoder::finish);

    connect(m_audio_decoder, &QAudioDecoder::bufferReady, this, &AudioDecoder::process_buffer);
}

void AudioDecoder::set_source_filename(const QString& path)
{
    m_levels.clear();
    m_ready = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audio_decoder->setSource(QUrl::fromLocalFile(path));
#else
    m_audio_decoder->setSourceFilename(path);
#endif
    m_audio_decoder->start();
}

const QVector<qreal>& AudioDecoder::get_levels() const
{
    return m_levels;
}

const bool AudioDecoder::is_ready() const
{
    return m_ready;
}

inline double compute_duration(const double frames, const double fps)
{
    return (std::abs(frames) / fps) * 1000000.0;
}

void AudioDecoder::compute_wave(QVector<qreal>& wave, const int num_pixels, const double start_frame, const double end_frame, const double fps,
                                const double play_frame)
{
    wave.clear();
    wave.reserve(num_pixels * 2);
    const double duration = compute_duration(end_frame - start_frame, fps);
    const qint64 pixel_duration = qint64(duration) / num_pixels;

    auto format = m_audio_buffer.format();
    int begging_shift = format.framesForDuration(compute_duration(start_frame, fps));
    if (start_frame < 0)
        begging_shift *= -1;
    int play_shift = format.framesForDuration(compute_duration(play_frame, fps));
    if (play_frame < 0)
        play_shift *= -1;
    begging_shift -= play_shift;

    const int pixel_frames = format.framesForDuration(pixel_duration);

    for (int i = 0; i < num_pixels; i++)
    {
        const int frame = pixel_frames * i + begging_shift;

        qreal wave_max = 0.0;
        qreal wave_min = 0.0;

        for (int j = 0; j < pixel_frames; j++)
        {
            const int current_frame = frame + j;
            if (current_frame >= 0 && current_frame < m_levels.size())
            {
                const qreal level = m_levels[current_frame];
                wave_max = std::max(wave_max, level);
                wave_min = std::min(wave_min, level);
            }
        }

        wave.append(wave_max);
        wave.append(wave_min);
    }
}

void AudioDecoder::clear()
{
    m_ready = false;
}

inline qreal get_peak_value(const QAudioFormat& format)
{
    if (!format.isValid())
        return qreal(0);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6 dropped codec()/sampleType()/sampleSize(): QAudioDecoder only ever emits PCM and the
    // layout is described by a single sampleFormat() enum.
    switch (format.sampleFormat())
    {
    case QAudioFormat::UInt8:
        return qreal(UCHAR_MAX);
    case QAudioFormat::Int16:
        return qreal(SHRT_MAX);
    case QAudioFormat::Int32:
        return qreal(INT_MAX);
    case QAudioFormat::Float:
        return qreal(1.00003);
    default:
        break;
    }
    return qreal(0);
#else
    if (format.codec() != "audio/pcm")
        return qreal(0);

    switch (format.sampleType())
    {
    case QAudioFormat::Unknown:
        break;
    case QAudioFormat::Float:
        if (format.sampleSize() != 32)
            return qreal(0);
        return qreal(1.00003);
    case QAudioFormat::SignedInt:
        if (format.sampleSize() == 32)
            return qreal(INT_MAX);
        if (format.sampleSize() == 16)
            return qreal(SHRT_MAX);
        if (format.sampleSize() == 8)
            return qreal(CHAR_MAX);
        break;
    case QAudioFormat::UnSignedInt:
        if (format.sampleSize() == 32)
            return qreal(UINT_MAX);
        if (format.sampleSize() == 16)
            return qreal(USHRT_MAX);
        if (format.sampleSize() == 8)
            return qreal(UCHAR_MAX);
        break;
    }

    return qreal(0);
#endif
}

template <class T>
void get_buffer_levels(const T* buffer, int frames, int channels, QVector<qreal>& levels)
{
    for (int i = 0; i < frames; ++i)
    {
        levels.append(qreal(buffer[i * channels])); // only one channel
    }
}

void AudioDecoder::process_buffer()
{
    m_audio_buffer = m_audio_decoder->read();

    if (!m_audio_buffer.format().isValid())
    {
        m_ready = false;
        return;
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt6 has no byteOrder()/codec(): decoded PCM is always host byte order.
    if (m_audio_buffer.format().byteOrder() != QAudioFormat::LittleEndian || m_audio_buffer.format().codec() != "audio/pcm")
    {
        m_ready = false;
        return;
    }
#endif

    int channel_count = m_audio_buffer.format().channelCount();
    int levels_start = m_levels.size();
    int frames = m_audio_buffer.frameCount();
    m_levels.reserve(frames);

    qreal peak_value = get_peak_value(m_audio_buffer.format());
    if (qFuzzyCompare(peak_value, qreal(0)))
    {
        m_ready = true;
        emit finish_decoding();
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6: one sampleFormat() enum replaces the sampleType()/sampleSize() pair.
    switch (m_audio_buffer.format().sampleFormat())
    {
    case QAudioFormat::UInt8:
        get_buffer_levels(m_audio_buffer.constData<quint8>(), frames, channel_count, m_levels);
        for (int i = 0; i < frames; ++i)
            m_levels[levels_start + i] = qAbs(m_levels.at(levels_start + i) - peak_value / 2) / (peak_value / 2);
        break;
    case QAudioFormat::Int16:
        get_buffer_levels(m_audio_buffer.constData<qint16>(), frames, channel_count, m_levels);
        for (int i = 0; i < frames; ++i)
            m_levels[levels_start + i] /= peak_value;
        break;
    case QAudioFormat::Int32:
        get_buffer_levels(m_audio_buffer.constData<qint32>(), frames, channel_count, m_levels);
        for (int i = 0; i < frames; ++i)
            m_levels[levels_start + i] /= peak_value;
        break;
    case QAudioFormat::Float:
        get_buffer_levels(m_audio_buffer.constData<float>(), frames, channel_count, m_levels);
        for (int i = 0; i < frames; ++i)
            m_levels[levels_start + i] /= peak_value;
        break;
    default:
        break;
    }
#else
    switch (m_audio_buffer.format().sampleType())
    {
    case QAudioFormat::Unknown:
    case QAudioFormat::UnSignedInt:
        if (m_audio_buffer.format().sampleSize() == 32)
            get_buffer_levels(m_audio_buffer.constData<quint32>(), m_audio_buffer.frameCount(), channel_count, m_levels);
        if (m_audio_buffer.format().sampleSize() == 16)
            get_buffer_levels(m_audio_buffer.constData<quint16>(), m_audio_buffer.frameCount(), channel_count, m_levels);
        if (m_audio_buffer.format().sampleSize() == 8)
            get_buffer_levels(m_audio_buffer.constData<quint8>(), m_audio_buffer.frameCount(), channel_count, m_levels);
        for (int i = 0; i < frames; ++i)
            m_levels[levels_start + i] = qAbs(m_levels.at(levels_start + i) - peak_value / 2) / (peak_value / 2);
        break;
    case QAudioFormat::Float:
        if (m_audio_buffer.format().sampleSize() == 32)
        {
            get_buffer_levels(m_audio_buffer.constData<float>(), m_audio_buffer.frameCount(), channel_count, m_levels);
            for (int i = 0; i < frames; ++i)
                m_levels[levels_start + i] /= peak_value;
        }
        break;
    case QAudioFormat::SignedInt:
        if (m_audio_buffer.format().sampleSize() == 32)
            get_buffer_levels(m_audio_buffer.constData<qint32>(), m_audio_buffer.frameCount(), channel_count, m_levels);
        if (m_audio_buffer.format().sampleSize() == 16)
            get_buffer_levels(m_audio_buffer.constData<qint16>(), m_audio_buffer.frameCount(), channel_count, m_levels);
        if (m_audio_buffer.format().sampleSize() == 8)
            get_buffer_levels(m_audio_buffer.constData<qint8>(), m_audio_buffer.frameCount(), channel_count, m_levels);
        for (int i = 0; i < frames; ++i)
            m_levels[levels_start + i] /= peak_value;
        break;
    }
#endif
}

void AudioDecoder::finish()
{
    m_ready = true;
    emit finish_decoding();
}

void AudioDecoder::handle_error(QAudioDecoder::Error error)
{
    m_ready = false;
    qDebug() << error;
}
