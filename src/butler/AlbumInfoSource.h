// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct AlbumInfoTrack {
    qint64 trackId = 0;
    QString relativePath { }; // 相对于公共目录的路径，如 CD1/01 - Song1.flac
    QString title { }; // needs_online 时仍给出原值，但标记 titleUnusable
    bool titleUnusable = false;
    QString artist { };
    QString albumArtist { };
    std::optional<int> year;
    std::optional<int> trackNumber;
    std::optional<int> discNumber;
    std::optional<int> trackTotal;
    std::optional<int> discTotal;
    qint64 durationMs = 0;

    bool operator==(const AlbumInfoTrack &other) const = default;
};

struct AlbumInfoInput {
    qint64 albumId = 0;
    QString title { }; // albums.title
    QString albumArtist { }; // albums.album_artist
    QString directory { }; // 该专辑所有曲目所在目录的最长公共父目录
    QList<AlbumInfoTrack> tracks; // 按 disc, track, relativePath 排序

    bool operator==(const AlbumInfoInput &other) const = default;
};

class AlbumInfoSource {
public:
    explicit AlbumInfoSource(library::Database &db);

    // 至少一首曲目 effective 值中上述任一字段为空（Title 仅 needs_online），且没有
    // album_info_checks 行；按 id 升序
    [[nodiscard]] core::Result<QList<qint64>> pendingAlbums() const;
    [[nodiscard]] core::Result<QList<QPair<qint64, int>>> pendingAlbumTrackCounts() const;
    [[nodiscard]] core::Result<QList<AlbumInfoInput>> load(const QList<qint64> &albumIds) const;
    core::Result<void> markChecked(
        const QList<qint64> &albumIds, const QString &model, int promptVersion, qint64 now) const;

private:
    library::Database &m_db;
};

// 把待处理专辑分成若干批：每批曲目总数 ≤ maxTracks（默认 150）且专辑数 ≤ maxAlbums（默认 16）；
// 单张专辑超过 maxTracks 时独占一批。返回每批的 itemKey（album id 的 JSON 数组，如 "[3,7,9]"）。
QStringList planAlbumInfoBatches(
    const QList<QPair<qint64, int>> &albumTrackCounts, int maxTracks = 150, int maxAlbums = 16);
core::Result<QList<qint64>> parseAlbumInfoItemKey(const QString &itemKey);

} // namespace linernotes::butler
