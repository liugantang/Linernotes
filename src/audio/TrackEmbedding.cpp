// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackEmbedding.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <audio/AudioDecoder.h>
#include <audio/AudioEmbedder.h>
#include <audio/Errors.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace linernotes::audio {

QString defaultEmbeddingModelPath()
{
    const QString name
        = QString::fromLatin1(kEmbeddingModelFileName.data(), kEmbeddingModelFileName.size());
    const QString appDir = QCoreApplication::applicationDirPath();
    QString besideApp = appDir + QStringLiteral("/models/") + name;
    if (QFileInfo::exists(besideApp)) {
        return besideApp;
    }
    return QDir::cleanPath(appDir + QStringLiteral("/../share/linernotes/models/") + name);
}

QList<qint64> embedWindowStartsMs(qint64 durationMs)
{
    if (durationMs <= kEmbedWindowMs) {
        return { 0 };
    }

    const qint64 maxStart = durationMs - kEmbedWindowMs;
    const qint64 halfWindow = kEmbedWindowMs / 2;

    const qint64 c20 = durationMs * 20 / 100;
    const qint64 c50 = durationMs * 50 / 100;
    const qint64 c80 = durationMs * 80 / 100;

    const qint64 s20 = std::clamp<qint64>(c20 - halfWindow, 0, maxStart);
    const qint64 s50 = std::clamp<qint64>(c50 - halfWindow, 0, maxStart);
    const qint64 s80 = std::clamp<qint64>(c80 - halfWindow, 0, maxStart);

    return { s20, s50, s80 };
}

core::Result<QList<float>> embedTrack(
    AudioEmbedder &embedder, const QString &path, qint64 trackStartMs, qint64 durationMs)
{
    if (durationMs <= 0) {
        return core::Error {
            .code = QString(errc::kAudioDecodeFailed),
            .message = QStringLiteral("Invalid duration: %1 ms").arg(durationMs),
            .detail = path,
        };
    }

    const QList<qint64> windowStarts = embedWindowStartsMs(durationMs);
    if (windowStarts.isEmpty()) {
        return core::Error {
            .code = QString(errc::kAudioDecodeFailed),
            .message = QStringLiteral("No embedding windows found"),
            .detail = path,
        };
    }

    std::vector<float> avgEmbedding(AudioEmbedder::kDim, 0.0F);
    int windowCount = 0;

    for (const qint64 winStart : windowStarts) {
        const DecodeOptions opts {
            .sampleRate = AudioEmbedder::kSampleRate,
            .channels = 1,
            .maxDurationMs = kEmbedWindowMs,
            .startMs = trackStartMs + winStart,
        };

        const auto decodeRes = AudioDecoder::decode(path, opts);
        if (!decodeRes.ok()) {
            return decodeRes.error();
        }

        const auto &samples = decodeRes.value().samples;
        if (samples.isEmpty()) {
            return core::Error {
                .code = QString(errc::kAudioDecodeFailed),
                .message = QStringLiteral("Empty decoded PCM for window at %1 ms").arg(winStart),
                .detail = path,
            };
        }

        std::vector<float> floatPcm;
        floatPcm.reserve(samples.size());
        for (const qint16 s : samples) {
            floatPcm.push_back(static_cast<float>(s) / 32768.0F);
        }

        const auto embedRes = embedder.embed(floatPcm);
        if (!embedRes.ok()) {
            return embedRes.error();
        }

        const auto &vec = embedRes.value();
        if (vec.size() != AudioEmbedder::kDim) {
            return core::Error {
                .code = QString(errc::kAudioEmbedFailed),
                .message = QStringLiteral("Unexpected embedding dimension: %1").arg(vec.size()),
                .detail = path,
            };
        }

        for (int i = 0; i < AudioEmbedder::kDim; ++i) {
            avgEmbedding.at(i) += vec.at(i);
        }
        windowCount++;
    }

    if (windowCount == 0) {
        return core::Error {
            .code = QString(errc::kAudioEmbedFailed),
            .message = QStringLiteral("No windows embedded"),
            .detail = path,
        };
    }

    // Average and L2 normalize
    double normSq = 0.0;
    for (int i = 0; i < AudioEmbedder::kDim; ++i) {
        avgEmbedding.at(i) /= static_cast<float>(windowCount);
        normSq += static_cast<double>(avgEmbedding.at(i)) * static_cast<double>(avgEmbedding.at(i));
    }

    const double norm = std::sqrt(normSq);
    QList<float> result;
    result.reserve(AudioEmbedder::kDim);

    if (norm > 1e-12) {
        for (int i = 0; i < AudioEmbedder::kDim; ++i) {
            result.append(static_cast<float>(avgEmbedding.at(i) / norm));
        }
    } else {
        for (int i = 0; i < AudioEmbedder::kDim; ++i) {
            result.append(0.0F);
        }
    }

    return result;
}

} // namespace linernotes::audio
