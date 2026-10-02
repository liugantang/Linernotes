// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QtGlobal>

#include <core/Result.h>
#include <rec/Recommender.h>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {

inline constexpr qint64 kTasteLookbackDays = 30;
inline constexpr qint64 kTasteLookbackMs = kTasteLookbackDays * 24LL * 3600 * 1000;
inline constexpr double kTasteHalfLifeDays = 14.0;
inline constexpr int kTasteMaxPlaySeeds = 20;
inline constexpr int kTasteMaxFavoriteSeeds = 20;
inline constexpr double kTasteFavoriteWeight = 0.5;

// 长期口味种子（与 sessionSeeds 不同：看最近 30 天与收藏）
// - play_events 中 track_id 非空、started_at >= nowMs - 30 天的记录，每条贡献 c × 0.5^(距今天数 /
// 14)，
//   c 为听完程度（同 sessionSeeds 的算法；skipped 且 c < 0.5 的记录不贡献）；按 trackId
//   累加，取权重最高的 20 首
// - 收藏的曲目（favorites entity_type='track'，按 created_at 降序最多 20 首）每首再加 0.5
// - 同一 trackId 合并相加
// 结果可为空（新用户），引擎照常工作。
[[nodiscard]] core::Result<QList<Seed>> tasteSeeds(library::Database &db, qint64 nowMs);

} // namespace linernotes::rec
