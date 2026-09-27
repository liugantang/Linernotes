// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <Qt>

#include <core/Result.h>
#include <library/LibraryEnums.h>
#include <library/SmartRule.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

/// 专辑排序列。
enum class AlbumSortKey : std::uint8_t {
    Title,
    Artist,
    Year,
    DateAdded,
};

/// 艺人排序列。
enum class ArtistSortKey : std::uint8_t {
    Name,
};

/// 曲目筛选条件。
struct TrackFilter {
    std::optional<qint64> albumId;
    std::optional<qint64> artistId; ///< track_artists 中 role = 'artist' 的曲目
    std::optional<QString> genre; ///< 精确匹配 effective_metadata.genre
    bool favoritesOnly = false; ///< favorites 中 entity_type = 'track'
    std::optional<qint64> playlistId; ///< 手动歌单：只返回该歌单中的曲目
    std::optional<SmartRule> smartRule; ///< 智能歌单：返回满足规则的曲目
};

/// 专辑筛选条件。
struct AlbumFilter {
    std::optional<qint64> artistId; ///< artistId 按 album_artists 筛选
    bool favoritesOnly = false; ///< favorites 中 entity_type = 'album'
};

/// 艺人筛选条件。
struct ArtistFilter {
    bool favoritesOnly = false; ///< favorites 中 entity_type = 'artist'
};

/// 单条曲目浏览行数据。
struct TrackRow {
    qint64 trackId = 0;
    qint64 fileId = 0;
    std::optional<qint64> albumId;
    QString path;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    QString composer;
    std::optional<int> year;
    std::optional<int> trackNumber;
    std::optional<int> discNumber;
    qint64 durationMs = 0;
    QString codec;
    int sampleRate = 0;
    int bitDepth = 0;
    int bitrate = 0;
    QString coverHash; ///< files.cover_id → covers.hash，无则为空
    bool favorite = false; ///< track 收藏
    int rating = 0; ///< ratings，无则 0
    qint64 addedAt = 0; ///< files.first_seen_at
    int playCount = 0;
    std::optional<qint64> lastPlayedAtMs;

    bool operator==(const TrackRow &) const = default;
};

/// 单条专辑浏览行数据。
struct AlbumRow {
    qint64 albumId = 0;
    QString title;
    QString albumArtist;
    std::optional<int> year;
    int trackCount = 0;
    qint64 totalDurationMs = 0;
    QString coverHash;
    bool favorite = false;

    bool operator==(const AlbumRow &) const = default;
};

/// 单条艺人浏览行数据。
struct ArtistRow {
    qint64 artistId = 0;
    QString name;
    int trackCount = 0;
    int albumCount = 0;
    QString coverHash; ///< 该艺人任一专辑的封面（取 year 最早、再按 album id 的第一个有封面的专辑）
    bool favorite = false;

    bool operator==(const ArtistRow &) const = default;
};

/// 曲库浏览查询（只读）。每个方法只执行少量 SQL，结果按页返回，供 UI 模型懒加载。
///
/// 约定与线程安全：
/// - 绑定构造时传入的连接，只能在该连接所属线程使用；
/// - 所有查询排除缺失文件（files.missing_since IS NOT NULL）；
/// - 每种排序以 id 同方向决胜，保证分页稳定不重不漏；
/// - 排序时 NULL 值统一放在最后（ASC 与 DESC 均使用 NULLS LAST）。
class LibraryQuery {
public:
    explicit LibraryQuery(const QSqlDatabase &db);

    /// 计算符合条件的曲目总数。
    core::Result<int> countTracks(const TrackFilter &filter) const;

    /// 分页查询曲目列表。
    core::Result<QList<TrackRow>> tracks(const TrackFilter &filter, TrackSortKey key,
        Qt::SortOrder order, int offset, int limit) const;

    /// 查询符合条件的全部曲目 ID（按指定排序）。
    core::Result<QList<qint64>> trackIds(
        const TrackFilter &filter, TrackSortKey key, Qt::SortOrder order) const;

    /// 按给定顺序返回存在的曲目（不存在或文件缺失的 id 跳过），供搜索结果、播放队列使用。
    core::Result<QList<TrackRow>> tracksByIds(const QList<qint64> &ids) const;

    /// 根据文件路径批量查询曲目 ID（只返回找到且未缺失的 files.path → tracks.id 映射）。
    core::Result<QHash<QString, qint64>> trackIdsByPaths(const QStringList &paths) const;

    /// 计算符合条件的专辑总数。
    core::Result<int> countAlbums(const AlbumFilter &filter) const;

    /// 分页查询专辑列表。
    core::Result<QList<AlbumRow>> albums(const AlbumFilter &filter, AlbumSortKey key,
        Qt::SortOrder order, int offset, int limit) const;

    /// 按给定顺序返回存在的专辑（不存在或无可见曲目的 id 跳过），供搜索结果等使用。
    core::Result<QList<AlbumRow>> albumsByIds(const QList<qint64> &ids) const;

    /// 查询单个专辑详情；若不存在或无可见曲目返回 std::nullopt。
    core::Result<std::optional<AlbumRow>> album(qint64 albumId) const;

    /// 计算符合条件的艺人总数。
    core::Result<int> countArtists(const ArtistFilter &filter) const;

    /// 分页查询艺人列表。
    core::Result<QList<ArtistRow>> artists(const ArtistFilter &filter, ArtistSortKey key,
        Qt::SortOrder order, int offset, int limit) const;

    /// 按给定顺序返回存在的艺人（不存在或无可见曲目的 id 跳过），供搜索结果等使用。
    core::Result<QList<ArtistRow>> artistsByIds(const QList<qint64> &ids) const;

    /// 查询单个艺人详情；若不存在或无可见曲目返回 std::nullopt。
    core::Result<std::optional<ArtistRow>> artist(qint64 artistId) const;

private:
    QSqlDatabase m_db;
};

} // namespace linernotes::library
