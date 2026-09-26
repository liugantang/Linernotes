// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EntityLinker.h"

#include <QDateTime>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QSqlError>

#include <library/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::library {

EntityLinker::EntityLinker(const QSqlDatabase &db, std::function<qint64()> nowMs)
    : m_db(db)
    , m_nowMs(std::move(nowMs))
    , m_findTrackMetadataStmt(m_db)
    , m_findTrackFilePathStmt(m_db)
    , m_findArtistByNameStmt(m_db)
    , m_insertArtistStmt(m_db)
    , m_deleteTrackArtistsStmt(m_db)
    , m_insertTrackArtistStmt(m_db)
    , m_getTrackAlbumIdStmt(m_db)
    , m_findAlbumByKeyStmt(m_db)
    , m_insertAlbumStmt(m_db)
    , m_updateTrackAlbumStmt(m_db)
    , m_updateAlbumYearStmt(m_db)
    , m_updateAlbumCoverStmt(m_db)
    , m_deleteAlbumArtistsStmt(m_db)
    , m_insertAlbumArtistStmt(m_db)
    , m_getAlbumTracksFirstArtistsStmt(m_db)
    , m_getAlbumArtistFieldStmt(m_db)
    , m_deleteOrphanAlbumsStmt(m_db)
    , m_deleteOrphanArtistsStmt(m_db)
    , m_deleteOrphanCoversStmt(m_db)
{
    m_findTrackMetadataStmt.prepare(QStringLiteral(
        "SELECT artist, album, album_artist, composer FROM effective_metadata WHERE track_id = ?"));
    m_findTrackFilePathStmt.prepare(QStringLiteral(
        "SELECT f.path FROM files f JOIN tracks t ON f.id = t.file_id WHERE t.id = ?"));
    m_findArtistByNameStmt.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ? LIMIT 1"));
    m_insertArtistStmt.prepare(
        QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, ?)"));
    m_deleteTrackArtistsStmt.prepare(
        QStringLiteral("DELETE FROM track_artists WHERE track_id = ?"));
    m_insertTrackArtistStmt.prepare(QStringLiteral(
        "INSERT INTO track_artists (track_id, artist_id, role, position) VALUES (?, ?, ?, ?)"));
    m_getTrackAlbumIdStmt.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    m_findAlbumByKeyStmt.prepare(
        QStringLiteral("SELECT id FROM albums WHERE grouping_key = ? LIMIT 1"));
    m_insertAlbumStmt.prepare(
        QStringLiteral("INSERT INTO albums (grouping_key, title, album_artist, year, created_at) "
                       "VALUES (?, ?, ?, NULL, ?)"));
    m_updateTrackAlbumStmt.prepare(QStringLiteral("UPDATE tracks SET album_id = ? WHERE id = ?"));
    m_updateAlbumYearStmt.prepare(QStringLiteral(
        "UPDATE albums SET year = (SELECT MIN(em.year) FROM tracks t JOIN effective_metadata em ON "
        "t.id = em.track_id WHERE t.album_id = ? AND em.year IS NOT NULL) WHERE id = ?"));
    m_updateAlbumCoverStmt.prepare(QStringLiteral(
        "UPDATE albums SET cover_id = ("
        "SELECT f.cover_id FROM tracks t "
        "JOIN files f ON t.file_id = f.id "
        "LEFT JOIN effective_metadata em ON t.id = em.track_id "
        "WHERE t.album_id = ? AND f.cover_id IS NOT NULL "
        "ORDER BY em.disc_number ASC NULLS LAST, em.track_number ASC NULLS LAST, f.path ASC "
        "LIMIT 1"
        ") WHERE id = ?"));
    m_deleteAlbumArtistsStmt.prepare(
        QStringLiteral("DELETE FROM album_artists WHERE album_id = ?"));
    m_insertAlbumArtistStmt.prepare(QStringLiteral(
        "INSERT INTO album_artists (album_id, artist_id, position) VALUES (?, ?, ?)"));
    m_getAlbumTracksFirstArtistsStmt.prepare(
        QStringLiteral("SELECT t.id, (SELECT ta.artist_id FROM track_artists ta WHERE ta.track_id "
                       "= t.id AND ta.role = 'artist' AND ta.position = 0) AS first_artist_id FROM "
                       "tracks t WHERE t.album_id = ?"));
    m_getAlbumArtistFieldStmt.prepare(
        QStringLiteral("SELECT album_artist FROM albums WHERE id = ?"));
    m_deleteOrphanAlbumsStmt.prepare(
        QStringLiteral("DELETE FROM albums WHERE id NOT IN (SELECT DISTINCT album_id FROM tracks "
                       "WHERE album_id IS NOT NULL)"));
    m_deleteOrphanArtistsStmt.prepare(
        QStringLiteral("DELETE FROM artists WHERE id NOT IN (SELECT artist_id FROM track_artists) "
                       "AND id NOT IN (SELECT artist_id FROM album_artists)"));
    m_deleteOrphanCoversStmt.prepare(
        QStringLiteral("DELETE FROM covers WHERE id NOT IN (SELECT cover_id FROM files WHERE "
                       "cover_id IS NOT NULL) "
                       "AND id NOT IN (SELECT cover_id FROM albums WHERE cover_id IS NOT NULL)"));
}

qint64 EntityLinker::getNow() const
{
    if (m_nowMs) {
        return m_nowMs();
    }
    return QDateTime::currentMSecsSinceEpoch();
}

void EntityLinker::clearCache()
{
    m_artistCache.clear();
}

core::Result<qint64> EntityLinker::getOrCreateArtist(const QString &name, qint64 now)
{
    if (name.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Artist name cannot be empty"),
            .detail = QString(),
        };
    }

    const auto it = m_artistCache.constFind(name);
    if (it != m_artistCache.constEnd()) {
        return it.value();
    }

    m_findArtistByNameStmt.bindValue(0, name);
    if (!m_findArtistByNameStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_findArtistByNameStmt.lastError().text(),
            .detail = QStringLiteral("findArtistByName"),
        };
    }

    if (m_findArtistByNameStmt.next()) {
        const qint64 id = m_findArtistByNameStmt.value(0).toLongLong();
        m_artistCache.insert(name, id);
        return id;
    }

    m_insertArtistStmt.bindValue(0, name);
    m_insertArtistStmt.bindValue(1, now);
    if (!m_insertArtistStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_insertArtistStmt.lastError().text(),
            .detail = QStringLiteral("insertArtist"),
        };
    }

    const qint64 id = m_insertArtistStmt.lastInsertId().toLongLong();
    m_artistCache.insert(name, id);
    return id;
}

core::Result<void> EntityLinker::rebuildExplicitAlbumArtists(
    qint64 albumId, const QString &albumArtistField, qint64 now)
{
    const QStringList tokens = albumArtistField.split(QStringLiteral(" / "));
    int pos = 0;
    QSet<qint64> seenArtistIds;
    for (const QString &token : tokens) {
        const QString trimmed = token.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const auto artistRes = getOrCreateArtist(trimmed, now);
        if (!artistRes.ok()) {
            return artistRes.error();
        }
        const qint64 artistId = artistRes.value();
        if (seenArtistIds.contains(artistId)) {
            continue;
        }
        seenArtistIds.insert(artistId);

        m_insertAlbumArtistStmt.bindValue(0, albumId);
        m_insertAlbumArtistStmt.bindValue(1, artistId);
        m_insertAlbumArtistStmt.bindValue(2, pos++);
        if (!m_insertAlbumArtistStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = m_insertAlbumArtistStmt.lastError().text(),
                .detail = QStringLiteral("insertAlbumArtist"),
            };
        }
    }
    return { };
}

core::Result<void> EntityLinker::rebuildInferredAlbumArtists(qint64 albumId)
{
    m_getAlbumTracksFirstArtistsStmt.bindValue(0, albumId);
    if (!m_getAlbumTracksFirstArtistsStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_getAlbumTracksFirstArtistsStmt.lastError().text(),
            .detail = QStringLiteral("getAlbumTracksFirstArtists"),
        };
    }

    std::optional<qint64> commonArtistId;
    bool hasTracks = false;
    bool allSame = true;

    while (m_getAlbumTracksFirstArtistsStmt.next()) {
        hasTracks = true;
        if (m_getAlbumTracksFirstArtistsStmt.value(1).isNull()) {
            allSame = false;
            break;
        }
        const qint64 firstArtistId = m_getAlbumTracksFirstArtistsStmt.value(1).toLongLong();
        if (!commonArtistId.has_value()) {
            commonArtistId = firstArtistId;
        } else if (commonArtistId.value() != firstArtistId) {
            allSame = false;
            break;
        }
    }

    if (hasTracks && allSame && commonArtistId.has_value()) {
        m_insertAlbumArtistStmt.bindValue(0, albumId);
        m_insertAlbumArtistStmt.bindValue(1, commonArtistId.value());
        m_insertAlbumArtistStmt.bindValue(2, 0);
        if (!m_insertAlbumArtistStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = m_insertAlbumArtistStmt.lastError().text(),
                .detail = QStringLiteral("insertAlbumArtistCommon"),
            };
        }
    }

    return { };
}

core::Result<void> EntityLinker::rebuildAlbumArtists(
    qint64 albumId, const QString &albumArtistField, qint64 now)
{
    m_deleteAlbumArtistsStmt.bindValue(0, albumId);
    if (!m_deleteAlbumArtistsStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_deleteAlbumArtistsStmt.lastError().text(),
            .detail = QStringLiteral("deleteAlbumArtists"),
        };
    }

    if (!albumArtistField.isEmpty()) {
        return rebuildExplicitAlbumArtists(albumId, albumArtistField, now);
    }
    return rebuildInferredAlbumArtists(albumId);
}

core::Result<void> EntityLinker::updateAlbumYearAndArtists(qint64 albumId, qint64 now)
{
    // Update year to MIN(effective year) of all tracks in album
    m_updateAlbumYearStmt.bindValue(0, albumId);
    m_updateAlbumYearStmt.bindValue(1, albumId);
    if (!m_updateAlbumYearStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_updateAlbumYearStmt.lastError().text(),
            .detail = QStringLiteral("updateAlbumYear"),
        };
    }

    // Update cover_id to first track's cover_id by (disc_number, track_number, path)
    m_updateAlbumCoverStmt.bindValue(0, albumId);
    m_updateAlbumCoverStmt.bindValue(1, albumId);
    if (!m_updateAlbumCoverStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_updateAlbumCoverStmt.lastError().text(),
            .detail = QStringLiteral("updateAlbumCover"),
        };
    }

    // Get album_artist field from albums table
    m_getAlbumArtistFieldStmt.bindValue(0, albumId);
    if (!m_getAlbumArtistFieldStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_getAlbumArtistFieldStmt.lastError().text(),
            .detail = QStringLiteral("getAlbumArtistField"),
        };
    }

    if (m_getAlbumArtistFieldStmt.next()) {
        const QString albumArtistField = m_getAlbumArtistFieldStmt.value(0).toString();
        return rebuildAlbumArtists(albumId, albumArtistField, now);
    }

    return { };
}

core::Result<void> EntityLinker::insertTrackArtistRoles(
    qint64 trackId, const QString &field, const QString &role, qint64 now)
{
    if (field.isEmpty()) {
        return { };
    }

    const QStringList tokens = field.split(QStringLiteral(" / "));
    int pos = 0;
    QSet<qint64> seenArtistIds;
    for (const QString &token : tokens) {
        const QString trimmed = token.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const auto artistRes = getOrCreateArtist(trimmed, now);
        if (!artistRes.ok()) {
            return artistRes.error();
        }
        const qint64 artistId = artistRes.value();
        if (seenArtistIds.contains(artistId)) {
            continue;
        }
        seenArtistIds.insert(artistId);

        m_insertTrackArtistStmt.bindValue(0, trackId);
        m_insertTrackArtistStmt.bindValue(1, artistId);
        m_insertTrackArtistStmt.bindValue(2, role);
        m_insertTrackArtistStmt.bindValue(3, pos++);
        if (!m_insertTrackArtistStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = m_insertTrackArtistStmt.lastError().text(),
                .detail = QStringLiteral("insertTrackArtist"),
            };
        }
    }

    return { };
}

core::Result<void> EntityLinker::linkTrackArtists(
    qint64 trackId, const QString &artistField, const QString &composerField, qint64 now)
{
    m_deleteTrackArtistsStmt.bindValue(0, trackId);
    if (!m_deleteTrackArtistsStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_deleteTrackArtistsStmt.lastError().text(),
            .detail = QStringLiteral("deleteTrackArtists"),
        };
    }

    auto artRes = insertTrackArtistRoles(trackId, artistField, QStringLiteral("artist"), now);
    if (!artRes.ok()) {
        return artRes;
    }

    return insertTrackArtistRoles(trackId, composerField, QStringLiteral("composer"), now);
}

core::Result<std::pair<QString, QVariant>> EntityLinker::resolveAlbumGroupingKey(
    qint64 trackId, const QString &albumField, const QString &albumArtistField)
{
    if (!albumArtistField.isEmpty()) {
        const QString groupingKey
            = QStringLiteral("aa:") + albumArtistField + QStringLiteral("\x1f") + albumField;
        return std::make_pair(groupingKey, QVariant(albumArtistField));
    }

    m_findTrackFilePathStmt.bindValue(0, trackId);
    if (!m_findTrackFilePathStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_findTrackFilePathStmt.lastError().text(),
            .detail = QStringLiteral("findTrackFilePath"),
        };
    }
    if (!m_findTrackFilePathStmt.next()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("File path not found for track"),
            .detail = QString::number(trackId),
        };
    }
    const QString filePath = m_findTrackFilePathStmt.value(0).toString();
    const QString dirPath = QFileInfo(filePath).absolutePath();
    const QString groupingKey
        = QStringLiteral("dir:") + dirPath + QStringLiteral("\x1f") + albumField;
    return std::make_pair(groupingKey, QVariant());
}

core::Result<qint64> EntityLinker::getOrCreateAlbum(const QString &groupingKey,
    const QString &albumTitle, const QVariant &albumArtistVal, qint64 now)
{
    m_findAlbumByKeyStmt.bindValue(0, groupingKey);
    if (!m_findAlbumByKeyStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_findAlbumByKeyStmt.lastError().text(),
            .detail = QStringLiteral("findAlbumByKey"),
        };
    }

    if (m_findAlbumByKeyStmt.next()) {
        return m_findAlbumByKeyStmt.value(0).toLongLong();
    }

    m_insertAlbumStmt.bindValue(0, groupingKey);
    m_insertAlbumStmt.bindValue(1, albumTitle);
    m_insertAlbumStmt.bindValue(2, albumArtistVal);
    m_insertAlbumStmt.bindValue(3, now);
    if (!m_insertAlbumStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_insertAlbumStmt.lastError().text(),
            .detail = QStringLiteral("insertAlbum"),
        };
    }
    return m_insertAlbumStmt.lastInsertId().toLongLong();
}

core::Result<void> EntityLinker::linkTrackAlbum(
    qint64 trackId, const QString &albumField, const QString &albumArtistField, qint64 now)
{
    std::optional<qint64> oldAlbumId;
    m_getTrackAlbumIdStmt.bindValue(0, trackId);
    if (!m_getTrackAlbumIdStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_getTrackAlbumIdStmt.lastError().text(),
            .detail = QStringLiteral("getTrackAlbumId"),
        };
    }
    if (m_getTrackAlbumIdStmt.next() && !m_getTrackAlbumIdStmt.value(0).isNull()) {
        oldAlbumId = m_getTrackAlbumIdStmt.value(0).toLongLong();
    }

    if (albumField.isEmpty()) {
        m_updateTrackAlbumStmt.bindValue(0, QVariant());
        m_updateTrackAlbumStmt.bindValue(1, trackId);
        if (!m_updateTrackAlbumStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = m_updateTrackAlbumStmt.lastError().text(),
                .detail = QStringLiteral("updateTrackAlbumNull"),
            };
        }
        if (oldAlbumId.has_value()) {
            return updateAlbumYearAndArtists(oldAlbumId.value(), now);
        }
        return { };
    }

    const auto keyRes = resolveAlbumGroupingKey(trackId, albumField, albumArtistField);
    if (!keyRes.ok()) {
        return keyRes.error();
    }
    const auto &[groupingKey, albumArtistVal] = keyRes.value();

    const auto albumRes = getOrCreateAlbum(groupingKey, albumField, albumArtistVal, now);
    if (!albumRes.ok()) {
        return albumRes.error();
    }
    const qint64 newAlbumId = albumRes.value();

    m_updateTrackAlbumStmt.bindValue(0, newAlbumId);
    m_updateTrackAlbumStmt.bindValue(1, trackId);
    if (!m_updateTrackAlbumStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_updateTrackAlbumStmt.lastError().text(),
            .detail = QStringLiteral("updateTrackAlbum"),
        };
    }

    auto updateRes = updateAlbumYearAndArtists(newAlbumId, now);
    if (!updateRes.ok()) {
        return updateRes.error();
    }

    if (oldAlbumId.has_value() && oldAlbumId.value() != newAlbumId) {
        return updateAlbumYearAndArtists(oldAlbumId.value(), now);
    }

    return { };
}

core::Result<void> EntityLinker::linkTrack(qint64 trackId)
{
    const qint64 now = getNow();

    m_findTrackMetadataStmt.bindValue(0, trackId);
    if (!m_findTrackMetadataStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_findTrackMetadataStmt.lastError().text(),
            .detail = QStringLiteral("findTrackMetadata"),
        };
    }

    if (!m_findTrackMetadataStmt.next()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Track not found in effective_metadata"),
            .detail = QString::number(trackId),
        };
    }

    const QString artistField = m_findTrackMetadataStmt.value(0).toString();
    const QString albumField = m_findTrackMetadataStmt.value(1).toString();
    const QString albumArtistField = m_findTrackMetadataStmt.value(2).toString();
    const QString composerField = m_findTrackMetadataStmt.value(3).toString();

    auto artRes = linkTrackArtists(trackId, artistField, composerField, now);
    if (!artRes.ok()) {
        return artRes;
    }

    return linkTrackAlbum(trackId, albumField, albumArtistField, now);
}

core::Result<std::pair<int, int>> EntityLinker::removeOrphans()
{
    // 1. Delete orphan albums (albums with no track references).
    // Cascades to album_artists (ON DELETE CASCADE) and triggers favorites_cleanup_album.
    // Note: Deleting artists or albums triggers favorites_cleanup_artist / favorites_cleanup_album.
    // Since we only remove entities with no track references, favorites for existing tracks are not
    // deleted.
    if (!m_deleteOrphanAlbumsStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_deleteOrphanAlbumsStmt.lastError().text(),
            .detail = QStringLiteral("deleteOrphanAlbums"),
        };
    }
    const int albumsRemoved = std::max(0, m_deleteOrphanAlbumsStmt.numRowsAffected());

    // 2. Delete orphan artists (artists with no track_artists and no album_artists references).
    // Cascades to artist_aliases (ON DELETE CASCADE) and triggers favorites_cleanup_artist.
    if (!m_deleteOrphanArtistsStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_deleteOrphanArtistsStmt.lastError().text(),
            .detail = QStringLiteral("deleteOrphanArtists"),
        };
    }
    const int artistsRemoved = std::max(0, m_deleteOrphanArtistsStmt.numRowsAffected());

    // 3. Delete orphan covers (covers not referenced by any file or album).
    if (!m_deleteOrphanCoversStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = m_deleteOrphanCoversStmt.lastError().text(),
            .detail = QStringLiteral("deleteOrphanCovers"),
        };
    }

    m_artistCache.clear();

    return std::make_pair(albumsRemoved, artistsRemoved);
}

} // namespace linernotes::library
