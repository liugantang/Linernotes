// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <audio/Fingerprint.h>

#include <cstdint>
#include <optional>
#include <type_traits>

namespace linernotes::butler {

inline constexpr double kSameRecordingThreshold = 0.90;
inline constexpr qint64 kDurationToleranceMs = 3000;

enum class DuplicateKind : std::uint8_t { Exact, SameRecording, Suspect };

QString duplicateKindToString(DuplicateKind kind);
std::optional<DuplicateKind> duplicateKindFromString(const QString &str);

inline size_t qHash(DuplicateKind kind, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<std::underlying_type_t<DuplicateKind>>(kind), seed);
}

struct DupTrack {
    qint64 trackId = 0;
    qint64 workId = 0; // 0 = 无作品
    QString versionType { }; // track_versions.version_type 原样；无则空
    qint64 durationMs = 0;
    QString contentHash { };
    std::optional<audio::RawFingerprint> fingerprint = std::nullopt;
    double keepScore = 0.0; // 见 keepScore()
    bool operator==(const DupTrack &) const = default;
};

struct DupGroup {
    DuplicateKind kind = DuplicateKind::Exact;
    QList<qint64> trackIds; // 升序
    qint64 recommendedTrackId = 0;
    bool operator==(const DupGroup &) const = default;
};

/// 候选簇：按 (workId, versionType) 分组（workId 为 0 的不参与），组内按时长排序后，相邻时长差 ≤
/// kDurationToleranceMs 的连成一簇； 只返回 ≥ 2 首的簇。exact 另算（见 findDuplicates）。
QList<QList<qint64>> candidateClusters(const QList<DupTrack> &tracks);

/// 识别重复：
/// 1. 并查集。content_hash 相同（非空）的曲目两两连边（不要求同作品）。
/// 2. 每个候选簇内：两首都有指纹且 fingerprintSimilarity ≥ kSameRecordingThreshold → 连边。
/// 3. 每个 ≥ 2 首的连通分量成一组：所有成员 content_hash 相同 → Exact，否则 SameRecording。
/// 4.
/// 候选簇内没有指纹的曲目（指纹缺失或计算失败）：若它不在任何已成的组里，把簇内所有未入组的曲目合成一个
/// Suspect 组（≥ 2 首时）。
/// 5. 每组 recommendedTrackId = keepScore 最高者（相同取 trackId 最小）。结果按组内最小 trackId
/// 排序。
QList<DupGroup> findDuplicates(const QList<DupTrack> &tracks);

struct KeepFactors {
    QString codec { }; // files.codec，小写比较
    int sampleRate = 0;
    int bitDepth = 0;
    int bitrate = 0; // kbps
    int filledTagFields = 0; // 见 DuplicateSource
    bool hasCover = false;
    bool operator==(const KeepFactors &) const = default;
};

/// 保留建议分：无损（flac、alac、wav、aiff、ape、wavpack/wv、tta、dsd 系列）+1000；
/// 无损时 + bitDepth * 10 + sampleRate / 1000；有损时 + bitrate / 10；
/// + filledTagFields * 5；有封面 +5。
double keepScore(const KeepFactors &f);

} // namespace linernotes::butler
