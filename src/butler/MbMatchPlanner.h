// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QStringList>

#include <butler/AlbumMatch.h>
#include <butler/MbMatchSource.h>
#include <butler/MusicBrainz.h>
#include <library/CorrectionStore.h>

namespace linernotes::butler {

inline constexpr int kMaxDetailFetches = 3;
inline constexpr int kMinCandidateScore = 70;

/// 从搜索结果选出要取详情的 release：
/// 丢弃 score < 70 的；按 (releaseGroupId, trackCount) 去重（保留 score 最高的；releaseGroupId
/// 为空的不去重）； 优先 trackCount >= 本地曲目数的，再按 score 降序；最多 kMaxDetailFetches(3)
/// 个。
[[nodiscard]] QStringList selectCandidates(
    const QList<MbReleaseSummary> &summaries, int localTrackCount);

/// 由匹配结果生成修正提议（source = MusicBrainz，confidence = match.score，reason 写 "MusicBrainz
/// release <id>"）。 只补空字段，已有值一律不改：
///   year ← yearFromDate(originalDate) ，没有则 yearFromDate(date)
///   album_artist ← release.artist；artist ← mbTrack.artist（为空时不补）
///   track_number ← position；disc_number ← disc（仅 release.discCount > 1
///   或本地已有其他曲目有碟号时；单碟专辑不补碟号） track_total ← 该碟音轨数；disc_total ←
///   discCount（同上，单碟不补） title ← mbTrack.title，仅当 titleNeedsOnline 为
///   true（此时覆盖现值）
[[nodiscard]] QList<library::CorrectionProposal> buildProposals(
    const MbAlbumInput &input, const MbRelease &release, const AlbumMatch &match);

} // namespace linernotes::butler
