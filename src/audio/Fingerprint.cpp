// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Fingerprint.h"

#include <QDataStream>
#include <QIODevice>

#include <audio/AudioDecoder.h>
#include <audio/AudioLogging.h>
#include <audio/Errors.h>

#include <algorithm>
#include <bit>
#include <memory>
#include <span>
#include <utility>

extern "C" {
#include <chromaprint.h>
}

namespace linernotes::audio {

namespace {

struct ChromaprintDeleter {
    void operator()(ChromaprintContext *ctx) const
    {
        if (ctx != nullptr) {
            chromaprint_free(ctx);
        }
    }
};

struct RawFpDeleter {
    void operator()(void *ptr) const
    {
        if (ptr != nullptr) {
            chromaprint_dealloc(ptr);
        }
    }
};

} // namespace

core::Result<RawFingerprint> Fingerprinter::computeFile(const QString &path)
{
    const DecodeOptions options {
        .sampleRate = 0,
        .channels = 1,
        .maxDurationMs = kFingerprintMaxDurationMs,
    };
    const auto pcmRes = AudioDecoder::decode(path, options);
    if (!pcmRes.ok()) {
        return pcmRes.error();
    }
    return compute(pcmRes.value());
}

core::Result<RawFingerprint> Fingerprinter::compute(const PcmBuffer &pcm)
{
    if (pcm.sampleRate <= 0 || pcm.channels <= 0 || pcm.samples.isEmpty()) {
        return core::Error {
            .code = QString(errc::kAudioFingerprintFailed),
            .message
            = QStringLiteral("Cannot compute fingerprint from empty or invalid PCM buffer"),
            .detail = QString(),
        };
    }

    const std::unique_ptr<ChromaprintContext, ChromaprintDeleter> ctx(
        chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT));
    if (!ctx) {
        return core::Error {
            .code = QString(errc::kAudioFingerprintFailed),
            .message = QStringLiteral("Failed to create Chromaprint context"),
            .detail = QString(),
        };
    }

    if (chromaprint_start(ctx.get(), pcm.sampleRate, pcm.channels) != 1) {
        return core::Error {
            .code = QString(errc::kAudioFingerprintFailed),
            .message = QStringLiteral("Failed to start Chromaprint computation"),
            .detail = QString(),
        };
    }

    if (chromaprint_feed(ctx.get(), pcm.samples.constData(), static_cast<int>(pcm.samples.size()))
        != 1) {
        return core::Error {
            .code = QString(errc::kAudioFingerprintFailed),
            .message = QStringLiteral("Failed to feed PCM samples to Chromaprint"),
            .detail = QString(),
        };
    }

    if (chromaprint_finish(ctx.get()) != 1) {
        return core::Error {
            .code = QString(errc::kAudioFingerprintFailed),
            .message = QStringLiteral("Failed to finish Chromaprint computation"),
            .detail = QString(),
        };
    }

    uint32_t *rawFp = nullptr;
    int rawSize = 0;
    if (chromaprint_get_raw_fingerprint(ctx.get(), &rawFp, &rawSize) != 1 || rawFp == nullptr) {
        return core::Error {
            .code = QString(errc::kAudioFingerprintFailed),
            .message = QStringLiteral("Failed to retrieve raw fingerprint from Chromaprint"),
            .detail = QString(),
        };
    }

    const std::unique_ptr<void, RawFpDeleter> rawFpGuard(rawFp);

    const std::span<const uint32_t> fpSpan(rawFp, static_cast<size_t>(rawSize));
    QList<quint32> items;
    items.reserve(static_cast<qsizetype>(rawSize));
    for (const uint32_t val : fpSpan) {
        items.append(val);
    }

    const int algo = chromaprint_get_algorithm(ctx.get());
    return RawFingerprint {
        .algorithm = algo,
        .items = std::move(items),
    };
}

double fingerprintSimilarity(const RawFingerprint &a, const RawFingerprint &b)
{
    if (a.algorithm != b.algorithm || a.items.isEmpty() || b.items.isEmpty()) {
        return 0.0;
    }

    const int aSize = static_cast<int>(a.items.size());
    const int bSize = static_cast<int>(b.items.size());

    double maxSimilarity = 0.0;
    bool hasValidAlignment = false;

    for (int offset = -kMaxOffset; offset <= kMaxOffset; ++offset) {
        const int aStart = std::max(0, -offset);
        const int bStart = std::max(0, offset);
        const int overlap = std::min(aSize - aStart, bSize - bStart);

        if (overlap < kMinOverlap) {
            continue;
        }

        hasValidAlignment = true;
        uint64_t totalBitErrors = 0;
        for (int i = 0; i < overlap; ++i) {
            const quint32 valA = a.items.at(aStart + i);
            const quint32 valB = b.items.at(bStart + i);
            totalBitErrors += std::popcount(valA ^ valB);
        }

        const double ber
            = static_cast<double>(totalBitErrors) / (32.0 * static_cast<double>(overlap));
        const double similarity = 1.0 - ber;
        maxSimilarity = std::max(maxSimilarity, similarity);
    }

    if (!hasValidAlignment) {
        return 0.0;
    }

    return std::clamp(maxSimilarity, 0.0, 1.0);
}

QByteArray toBlob(const RawFingerprint &fingerprint)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (const quint32 item : fingerprint.items) {
        stream << item;
    }
    return blob;
}

std::optional<QList<quint32>> itemsFromBlob(const QByteArray &blob)
{
    if (blob.size() % static_cast<qsizetype>(sizeof(quint32)) != 0) {
        return std::nullopt;
    }
    const qsizetype count = blob.size() / static_cast<qsizetype>(sizeof(quint32));
    QList<quint32> items;
    items.reserve(count);
    QDataStream stream(blob);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (qsizetype i = 0; i < count; ++i) {
        quint32 item = 0;
        stream >> item;
        items.append(item);
    }
    return items;
}

} // namespace linernotes::audio
