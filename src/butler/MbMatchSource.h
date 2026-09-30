// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <butler/AlbumMatch.h>
#include <butler/MusicBrainz.h>
#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

enum class MbMatchStatus : std::uint8_t {
    Matched,
    Ambiguous,
    NoMatch
}; // 持久化名 matched / ambiguous / no_match

struct MbAlbumTrack { // 一首本地曲目的现状
    LocalTrack local; // 7.3 的结构：标题、时长（files.duration_ms，NULL → 0）、disc/track 编号
    QString artist { };
    QString albumArtist { };
    std::optional<int> year = std::nullopt;
    std::optional<int> trackTotal = std::nullopt;
    std::optional<int> discTotal = std::nullopt;
    bool titleNeedsOnline = false;
};

struct MbAlbumInput {
    qint64 albumId = 0;
    QString searchTitle { }; // albums.title
    QString searchArtist { }; // albums.album_artist；为空时取曲目 artist 中出现最多的
    QList<MbAlbumTrack> tracks; // 按 disc, track, id 排序
    [[nodiscard]] LocalAlbum toLocalAlbum() const;
};

struct MbAlbumResult {
    MbMatchStatus status = MbMatchStatus::NoMatch;
    std::optional<MbRelease> release = std::nullopt; // matched / ambiguous 时有
    std::optional<AlbumMatch> match = std::nullopt;
};

class MbMatchSource {
public:
    explicit MbMatchSource(library::Database &db);

    /// 需要匹配的专辑 id（升序）：还没有 mb_album_matches 行，且至少一首曲目满足：
    /// effective_metadata 的 year / album_artist / track_number 任一为空，或有 needs_online 的
    /// title 问题。
    [[nodiscard]] core::Result<QList<qint64>> pendingAlbums() const;

    [[nodiscard]] core::Result<MbAlbumInput> load(qint64 albumId) const;

    /// 写一条 mb_album_matches（INSERT OR REPLACE），matched 时同一事务写 mb_track_matches。
    core::Result<void> saveMatch(qint64 albumId, const MbAlbumResult &result, qint64 now) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::butler
