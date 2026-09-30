// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AudioDecoder.h"

#include <audio/AudioLogging.h>
#include <audio/Errors.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace linernotes::audio {

namespace {

struct AvFormatContextDeleter {
    void operator()(AVFormatContext *ctx) const
    {
        if (ctx != nullptr) {
            avformat_close_input(&ctx);
        }
    }
};

struct AvCodecContextDeleter {
    void operator()(AVCodecContext *ctx) const
    {
        if (ctx != nullptr) {
            avcodec_free_context(&ctx);
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext *ctx) const
    {
        if (ctx != nullptr) {
            swr_free(&ctx);
        }
    }
};

struct AvPacketDeleter {
    void operator()(AVPacket *pkt) const
    {
        if (pkt != nullptr) {
            av_packet_free(&pkt);
        }
    }
};

struct AvFrameDeleter {
    void operator()(AVFrame *frame) const
    {
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

struct ChannelLayoutGuard {
    explicit ChannelLayoutGuard(AVChannelLayout *l)
        : layout(l)
    {
    }
    Q_DISABLE_COPY_MOVE(ChannelLayoutGuard)
    AVChannelLayout *layout = nullptr;
    ~ChannelLayoutGuard()
    {
        if (layout != nullptr) {
            av_channel_layout_uninit(layout);
        }
    }
};

QString ffmpegErrorString(int errnum)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> errBuf = { };
    av_strerror(errnum, errBuf.data(), errBuf.size());
    return QString::fromUtf8(errBuf.data());
}

struct StreamInfo {
    FormatContextPtr formatCtx;
    int audioStreamIndex = -1;
    const AVCodec *decoder = nullptr;
};

core::Result<StreamInfo> openAudioStream(const QString &path)
{
    AVFormatContext *formatCtxRaw = nullptr;
    const QByteArray pathUtf8 = path.toUtf8();
    int ret = avformat_open_input(&formatCtxRaw, pathUtf8.constData(), nullptr, nullptr);
    FormatContextPtr formatCtx(formatCtxRaw);

    if (ret < 0 || !formatCtx) {
        return core::Error {
            .code = QString(errc::kAudioOpenFailed),
            .message
            = QStringLiteral("Failed to open file '%1': %2").arg(path, ffmpegErrorString(ret)),
            .detail = path,
        };
    }

    ret = avformat_find_stream_info(formatCtx.get(), nullptr);
    if (ret < 0) {
        return core::Error {
            .code = QString(errc::kAudioOpenFailed),
            .message = QStringLiteral("Failed to find stream info for '%1': %2")
                .arg(path, ffmpegErrorString(ret)),
            .detail = path,
        };
    }

    const AVCodec *decoder = nullptr;
    const int audioStreamIndex
        = av_find_best_stream(formatCtx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (audioStreamIndex < 0 || decoder == nullptr) {
        return core::Error {
            .code = QString(errc::kAudioNoAudioStream),
            .message = QStringLiteral("No audio stream found in '%1'").arg(path),
            .detail = path,
        };
    }

    return StreamInfo {
        .formatCtx = std::move(formatCtx),
        .audioStreamIndex = audioStreamIndex,
        .decoder = decoder,
    };
}

core::Result<CodecContextPtr> openCodecContext(const StreamInfo &info, const QString &path)
{
    CodecContextPtr codecCtx(avcodec_alloc_context3(info.decoder));
    if (!codecCtx) {
        return core::Error {
            .code = QString(errc::kAudioDecodeFailed),
            .message = QStringLiteral("Failed to allocate codec context for '%1'").arg(path),
            .detail = path,
        };
    }

    const auto *stream
        = *std::next(std::span(info.formatCtx->streams, info.formatCtx->nb_streams).begin(),
            info.audioStreamIndex);
    int ret = avcodec_parameters_to_context(codecCtx.get(), stream->codecpar);
    if (ret < 0) {
        return core::Error {
            .code = QString(errc::kAudioDecodeFailed),
            .message = QStringLiteral("Failed to copy codec parameters for '%1': %2")
                .arg(path, ffmpegErrorString(ret)),
            .detail = path,
        };
    }

    ret = avcodec_open2(codecCtx.get(), info.decoder, nullptr);
    if (ret < 0) {
        return core::Error {
            .code = QString(errc::kAudioOpenFailed),
            .message = QStringLiteral("Failed to open audio codec for '%1': %2")
                .arg(path, ffmpegErrorString(ret)),
            .detail = path,
        };
    }

    return codecCtx;
}

core::Result<SwrContextPtr> createResampler(
    const AVCodecContext *codecCtx, int targetChannels, int targetSampleRate, const QString &path)
{
    const int srcSampleRate = codecCtx->sample_rate > 0 ? codecCtx->sample_rate : 44100;

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, targetChannels);
    const ChannelLayoutGuard layoutGuard { &outLayout };

    SwrContext *swrRaw = nullptr;
    const int ret = swr_alloc_set_opts2(&swrRaw, &outLayout, AV_SAMPLE_FMT_S16, targetSampleRate,
        &codecCtx->ch_layout, codecCtx->sample_fmt, srcSampleRate, 0, nullptr);
    SwrContextPtr swrCtx(swrRaw);
    if (ret < 0 || !swrCtx || swr_init(swrCtx.get()) < 0) {
        return core::Error {
            .code = QString(errc::kAudioDecodeFailed),
            .message = QStringLiteral("Failed to initialize resampler for '%1'").arg(path),
            .detail = path,
        };
    }

    return swrCtx;
}

bool convertAndAppendFrame(SwrContext *swrCtx, const AVFrame *inFrame, int targetChannels,
    qint64 maxFrames, qint64 &totalFrames, QList<qint16> &pcmSamples)
{
    const uint8_t *const *inData = (inFrame != nullptr) ? inFrame->data : nullptr;

    const int inCount = (inFrame != nullptr) ? inFrame->nb_samples : 0;
    const int outCount = swr_get_out_samples(swrCtx, inCount);
    if (outCount <= 0) {
        return true;
    }

    uint8_t *outData = nullptr;
    int linesize = 0;
    if (av_samples_alloc(&outData, &linesize, targetChannels, outCount, AV_SAMPLE_FMT_S16, 0) < 0) {
        return false;
    }

    const int converted = swr_convert(swrCtx, &outData, outCount, inData, inCount);
    if (converted > 0) {
        // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) - FFmpeg outData contains raw
        // S16 PCM bytes
        const auto *s16 = reinterpret_cast<const qint16 *>(outData);
        // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
        const std::span<const qint16> sampleSpan(
            s16, static_cast<size_t>(converted) * static_cast<size_t>(targetChannels));
        for (const qint16 sample : sampleSpan) {
            pcmSamples.append(sample);
        }
        totalFrames += converted;
    }
    av_freep(static_cast<void *>(&outData));

    return maxFrames <= 0 || totalFrames < maxFrames;
}

bool drainFrames(AVCodecContext *codecCtx, SwrContext *swrCtx, AVFrame *frame, int targetChannels,
    qint64 maxFrames, qint64 &totalFrames, QList<qint16> &pcmSamples)
{
    while (avcodec_receive_frame(codecCtx, frame) >= 0) {
        if (!convertAndAppendFrame(
                swrCtx, frame, targetChannels, maxFrames, totalFrames, pcmSamples)) {
            return false;
        }
    }
    return true;
}

core::Result<void> decodePackets(AVFormatContext *formatCtx, int audioStreamIndex,
    AVCodecContext *codecCtx, SwrContext *swrCtx, int targetChannels, qint64 maxFrames,
    const QString &path, QList<qint16> &pcmSamples)
{
    PacketPtr packet(av_packet_alloc());
    const FramePtr frame(av_frame_alloc());
    if (!packet || !frame) {
        return core::Error {
            .code = QString(errc::kAudioDecodeFailed),
            .message = QStringLiteral("Failed to allocate packet or frame for '%1'").arg(path),
            .detail = path,
        };
    }

    qint64 totalFrames = 0;
    bool continueDecoding = true;

    while (continueDecoding && av_read_frame(formatCtx, packet.get()) >= 0) {
        if (packet->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(codecCtx, packet.get()) >= 0) {
                continueDecoding = drainFrames(codecCtx, swrCtx, frame.get(), targetChannels,
                    maxFrames, totalFrames, pcmSamples);
            }
        }
        av_packet_unref(packet.get());
    }

    if (continueDecoding) {
        avcodec_send_packet(codecCtx, nullptr);
        continueDecoding = drainFrames(
            codecCtx, swrCtx, frame.get(), targetChannels, maxFrames, totalFrames, pcmSamples);
    }

    if (continueDecoding) {
        convertAndAppendFrame(swrCtx, nullptr, targetChannels, maxFrames, totalFrames, pcmSamples);
    }

    if (maxFrames > 0 && totalFrames > maxFrames) {
        pcmSamples.resize(static_cast<qsizetype>(maxFrames * targetChannels));
    }

    return { };
}

} // namespace

qint64 PcmBuffer::durationMs() const
{
    if (sampleRate <= 0 || channels <= 0 || samples.isEmpty()) {
        return 0;
    }
    const qint64 frameCount = samples.size() / channels;
    return (frameCount * 1000) / sampleRate;
}

core::Result<PcmBuffer> AudioDecoder::decode(const QString &path, const DecodeOptions &options)
{
    auto streamInfoRes = openAudioStream(path);
    if (!streamInfoRes.ok()) {
        return streamInfoRes.error();
    }
    auto &streamInfo = streamInfoRes.value();

    auto codecCtxRes = openCodecContext(streamInfo, path);
    if (!codecCtxRes.ok()) {
        return codecCtxRes.error();
    }
    const auto &codecCtx = codecCtxRes.value();

    const int srcChannels
        = codecCtx->ch_layout.nb_channels > 0 ? codecCtx->ch_layout.nb_channels : 1;
    const int srcSampleRate = codecCtx->sample_rate > 0 ? codecCtx->sample_rate : 44100;
    const int targetChannels
        = options.channels > 0 ? std::max(1, std::min(options.channels, srcChannels)) : srcChannels;
    const int targetSampleRate = options.sampleRate > 0 ? options.sampleRate : srcSampleRate;

    auto swrCtxRes = createResampler(codecCtx.get(), targetChannels, targetSampleRate, path);
    if (!swrCtxRes.ok()) {
        return swrCtxRes.error();
    }
    const auto &swrCtx = swrCtxRes.value();

    const qint64 maxFrames
        = (options.maxDurationMs > 0) ? (options.maxDurationMs * targetSampleRate / 1000) : 0;

    QList<qint16> pcmSamples;
    if (maxFrames > 0) {
        // 一次预留到上限；逐帧按精确大小 reserve 会让 QList 放弃倍增、每帧重新分配并复制（96 kHz
        // 长曲目慢几十倍）
        pcmSamples.reserve(static_cast<qsizetype>(maxFrames) * targetChannels);
    }
    const auto decodeRes = decodePackets(streamInfo.formatCtx.get(), streamInfo.audioStreamIndex,
        codecCtx.get(), swrCtx.get(), targetChannels, maxFrames, path, pcmSamples);
    if (!decodeRes.ok()) {
        return decodeRes.error();
    }

    return PcmBuffer {
        .sampleRate = targetSampleRate,
        .channels = targetChannels,
        .samples = std::move(pcmSamples),
    };
}

} // namespace linernotes::audio
