// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <core/Result.h>

namespace linernotes::audio {

class AudioEmbedder;

inline constexpr qint64 kEmbedWindowMs = 7000;
inline constexpr QLatin1StringView kEmbeddingModelId { "msclap2023-3x7s" };

// 3 个窗口的起点：中心在曲目的 20%/50%/80%，并夹到 [0, durationMs - kEmbedWindowMs]；
// durationMs <= kEmbedWindowMs 时只返回 {0}。纯函数。
QList<qint64> embedWindowStartsMs(qint64 durationMs);

// 对一首曲目算向量：对每个窗口用 AudioDecoder::decode(path, {sampleRate 44100, channels 1,
// startMs = trackStartMs + 窗口起点, maxDurationMs = kEmbedWindowMs}) 解码，qint16 / 32768 转
// float， 调 embedder.embed()，各窗口向量求平均后再 L2 归一化。trackStartMs 用于 CUE
// 分轨（普通文件为 0）。
core::Result<QList<float>> embedTrack(
    AudioEmbedder &embedder, const QString &path, qint64 trackStartMs, qint64 durationMs);

} // namespace linernotes::audio
