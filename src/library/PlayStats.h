// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

class Database;
struct PlayCountRule;

struct TrackPlayStats {
    int playCount = 0;
    std::optional<qint64> lastPlayedAtMs;
    qint64 totalPlayedMs = 0;
    std::optional<double> avgCompletion;
    int skipCount = 0;

    bool operator==(const TrackPlayStats &) const = default;
};

struct AlbumCompletion {
    int trackCount = 0;
    int playedTrackCount = 0;
    double completion = 0.0;

    bool operator==(const AlbumCompletion &) const = default;
};

class PlayStats {
public:
    explicit PlayStats(Database &db);

    /// 按规则重算单曲（一条 INSERT ... SELECT ... ON CONFLICT DO
    /// UPDATE，或先删后插，放在一个事务里）。
    core::Result<void> refreshTrack(qint64 trackId, const PlayCountRule &rule);

    /// 清空并按规则重算全部（一个事务、一条聚合 INSERT ... SELECT ... GROUP BY track_id）。
    core::Result<void> rebuildAll(const PlayCountRule &rule);

    core::Result<TrackPlayStats> track(qint64 trackId) const; // 无记录返回默认值
    core::Result<std::optional<AlbumCompletion>> album(qint64 albumId) const;

private:
    Database &m_db;
};

} // namespace linernotes::library
