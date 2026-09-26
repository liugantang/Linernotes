// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <core/Result.h>

#include <cstdint>
#include <functional>
#include <utility>

namespace linernotes::library {

/// 根据 effective_metadata 为 track 建立专辑与艺人关联。只在写线程、调用方已开启的事务中使用。
class EntityLinker {
public:
    explicit EntityLinker(const QSqlDatabase &db, std::function<qint64()> nowMs = { });

    /// 重新关联一个 track：设置 tracks.album_id；重建该 track 的 track_artists（role
    /// artist、composer）。
    core::Result<void> linkTrack(qint64 trackId);

    /// 删除没有任何 track 引用的专辑，以及既没有 track_artists 也没有 album_artists 引用的艺人。
    /// 返回删除的 (专辑数, 艺人数)。
    core::Result<std::pair<int, int>> removeOrphans();

    /// 清理艺人 ID 缓存。
    void clearCache();

private:
    qint64 getNow() const;
    core::Result<qint64> getOrCreateArtist(const QString &name, qint64 now);
    core::Result<void> rebuildAlbumArtists(
        qint64 albumId, const QString &albumArtistField, qint64 now);
    core::Result<void> rebuildExplicitAlbumArtists(
        qint64 albumId, const QString &albumArtistField, qint64 now);
    core::Result<void> rebuildInferredAlbumArtists(qint64 albumId);
    core::Result<void> updateAlbumYearAndArtists(qint64 albumId, qint64 now);
    core::Result<void> linkTrackArtists(
        qint64 trackId, const QString &artistField, const QString &composerField, qint64 now);
    core::Result<void> insertTrackArtistRoles(
        qint64 trackId, const QString &field, const QString &role, qint64 now);
    core::Result<std::pair<QString, QVariant>> resolveAlbumGroupingKey(
        qint64 trackId, const QString &albumField, const QString &albumArtistField);
    core::Result<qint64> getOrCreateAlbum(const QString &groupingKey, const QString &albumTitle,
        const QVariant &albumArtistVal, qint64 now);
    core::Result<void> linkTrackAlbum(
        qint64 trackId, const QString &albumField, const QString &albumArtistField, qint64 now);

    QSqlDatabase m_db;
    std::function<qint64()> m_nowMs;
    QHash<QString, qint64> m_artistCache;

    QSqlQuery m_findTrackMetadataStmt;
    QSqlQuery m_findTrackFilePathStmt;
    QSqlQuery m_findArtistByNameStmt;
    QSqlQuery m_insertArtistStmt;
    QSqlQuery m_deleteTrackArtistsStmt;
    QSqlQuery m_insertTrackArtistStmt;
    QSqlQuery m_getTrackAlbumIdStmt;
    QSqlQuery m_findAlbumByKeyStmt;
    QSqlQuery m_insertAlbumStmt;
    QSqlQuery m_updateTrackAlbumStmt;
    QSqlQuery m_updateAlbumYearStmt;
    QSqlQuery m_updateAlbumCoverStmt;
    QSqlQuery m_deleteAlbumArtistsStmt;
    QSqlQuery m_insertAlbumArtistStmt;
    QSqlQuery m_getAlbumTracksFirstArtistsStmt;
    QSqlQuery m_getAlbumArtistFieldStmt;
    QSqlQuery m_deleteOrphanAlbumsStmt;
    QSqlQuery m_deleteOrphanArtistsStmt;
    QSqlQuery m_deleteOrphanCoversStmt;
};

} // namespace linernotes::library
