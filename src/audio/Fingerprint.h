// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <audio/AudioDecoder.h>
#include <core/Result.h>

#include <optional>

namespace linernotes::audio {

struct RawFingerprint {
    int algorithm = 0; // Chromaprint 算法编号
    QList<quint32> items; // chromaprint_get_raw_fingerprint 的结果
    bool operator==(const RawFingerprint &) const = default;
};

/// 与 fpcalc 默认一致：只用前 120 秒。
inline constexpr qint64 kFingerprintMaxDurationMs = 120'000;
inline constexpr int kMaxOffset = 80;
inline constexpr int kMinOverlap = 20;

class Fingerprinter {
public:
    /// 解码（单声道/原采样率即可，Chromaprint 内部重采样）+ 计算。任意线程可调用。
    static core::Result<RawFingerprint> computeFile(const QString &path);
    static core::Result<RawFingerprint> compute(const PcmBuffer &pcm);
};

/// 两个指纹的相似度 [0,1]：在 ±kMaxOffset 个条目（约 ±10 秒）范围内找最佳对齐，
/// 相似度 = 1 - 重叠部分的比特错误率（popcount(a^b) / (32 * 重叠数)）。
/// 重叠条目数少于 kMinOverlap 的对齐不计入；没有任何合法对齐（或任一为空）返回 0。
/// 算法编号不同返回 0。
double fingerprintSimilarity(const RawFingerprint &a, const RawFingerprint &b);

/// 二进制序列化（供存库）：items 按小端 uint32 连续存放。
/// 格式与 library::FingerprintStore 的 blob 格式完全一致。
QByteArray toBlob(const RawFingerprint &fingerprint);

/// 二进制反序列化：长度不是 4 的倍数返回 std::nullopt。
/// 格式与 library::FingerprintStore 的 blob 格式完全一致。
std::optional<QList<quint32>> itemsFromBlob(const QByteArray &blob);

} // namespace linernotes::audio
