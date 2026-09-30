// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

#include <core/Result.h>

class QSqlDatabase;

namespace linernotes::library {

/// 把 fromTrackId 的用户数据并到 toTrackId，然后删除 from 曲目及其文件行（一个事务）：
/// - play_events、moments：track_id 改为 to；
/// - track_play_stats：两者都有时合并（play_count、total_played_ms、skip_count 相加；last_played_at
/// 取大；
///   avg_completion 按 play_count 加权平均，任一为 NULL 时取另一个），只有 from 有时直接改
///   track_id；
/// - favorites（entity_type='track'）：from 是收藏而 to 不是 → 给 to 加收藏（created_at 取 from
/// 的）；
/// - ratings：to 没有评分时取 from 的；
/// - playlist_items：track_id 改为 to（同一歌单里可能出现两次同一首，保留，不去重）；
/// - 最后 DELETE FROM tracks WHERE id = from；若该文件不再被任何 tracks 引用，DELETE FROM files
/// WHERE id = 该文件。 两个 id 相同或任一不存在 → 错误，不做任何修改。
core::Result<void> mergeTrackInto(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId);

} // namespace linernotes::library
