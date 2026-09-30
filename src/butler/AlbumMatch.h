// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <butler/MusicBrainz.h>

#include <cstdint>
#include <optional>

namespace linernotes::butler {

inline constexpr double kMinPairScore = 0.55;
inline constexpr double kMinAlbumScore = 0.75;
inline constexpr double kAmbiguityMargin = 0.05;

struct LocalTrack {
    qint64 trackId = 0;
    QString title { };
    qint64 durationMs = 0; // 0 = 未知
    std::optional<int> discNumber = std::nullopt;
    std::optional<int> trackNumber = std::nullopt;
    bool titleUnknown = false; // 标题不可用，只按时长与编号匹配
    bool operator==(const LocalTrack &) const = default;
};

struct LocalAlbum {
    QString title { };
    QList<LocalTrack> tracks;
    bool operator==(const LocalAlbum &) const = default;
};

struct TrackMapping {
    qint64 trackId = 0;
    int mbIndex = -1; // MbRelease::tracks 的下标
    double score = 0.0; // 这一对的得分 [0,1]
    bool operator==(const TrackMapping &) const = default;
};

struct AlbumMatch {
    QString releaseId { };
    double score = 0.0; // [0,1]
    QList<TrackMapping> mapping; // 只含成功对应的本地曲目，按本地曲目顺序
    bool operator==(const AlbumMatch &) const = default;
};

struct MatchDecision {
    std::optional<AlbumMatch> best; // 通过阈值的最佳匹配
    bool ambiguous
        = false; // 有两个候选都通过阈值、分差 < kAmbiguityMargin(0.05)，且它们会导致不同的补全结果
    bool operator==(const MatchDecision &) const = default;
};

/// 曲目标题相似度 [0,1]：两边先 exactKey（butler/ArtistName.h），再做一次“去掉括号内容”的变体
/// （括号：() （） [] ［］ 【】 「」『』 以外的不算——注意：「」『』 内通常是正文，**不**去掉），
/// 取 (原键, 去括号键) 两两组合中的最大值；每组相似度 = 1 - Levenshtein(按 QChar) /
/// max(长度)；两者都为空返回 0。
double titleSimilarity(const QString &a, const QString &b);

/// 时长得分 [0,1]：任一为 0 → 0.5（未知，中性）；差 ≤ 2 s → 1；差 ≥ 15 s → 0；之间线性。
double durationScore(qint64 aMs, qint64 bMs);

/// 一对曲目的得分：0.6 * titleSimilarity + 0.4 * durationScore；
/// 若本地有 disc/track 编号且与 MB 的 (disc, position) 相同，再 +0.1，封顶 1。
/// local.titleUnknown 时为 0.7 * durationScore + 0.3 * (本地 disc/track 编号与 MB 的 (disc,
/// position) 相同 ? 1 : 0)； 此时若本地时长为 0（未知）→ 返回 0。
double pairScore(const LocalTrack &local, const MbTrack &mb);

/// 本地专辑 vs 一个 release：
/// 1. 按 pairScore 从高到低贪心配对（每个本地曲目、每个 MB 音轨最多用一次），pairScore <
/// kMinPairScore(0.55) 的对不配。
/// 2. score = (配上的各对 pairScore 之和 / 本地曲目数)
///           * sizePenalty，sizePenalty = 1 - 0.3 * max(0, MB 音轨数 - 本地曲目数) / MB 音轨数
///    （本地只有部分曲目是常见情况，只轻微扣分；本地比 MB 多的曲目自然体现为没配上）。
/// 3. 本地曲目为空或 release 无音轨 → score 0、mapping 为空。
AlbumMatch scoreRelease(const LocalAlbum &local, const MbRelease &release);

/// 对所有候选调用 scoreRelease，取最高分；score < kMinAlbumScore(0.75) → best 为空。
/// 次高者也通过阈值且分差 < 0.05 时，比较两者“补全结果”是否等价：
/// 等价 = 年份（yearFromDate(date)）相同、音轨数相同、对应关系中每个本地曲目的 (disc, position)
/// 相同。 等价则不算歧义（MB 上同一张专辑常有多个数字版/地区版，取哪个结果都一样）。不等价 →
/// ambiguous = true，best 仍给出最高分者。
MatchDecision decide(const LocalAlbum &local, const QList<MbRelease> &releases);

} // namespace linernotes::butler
