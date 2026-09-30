// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MbMatchSource.h"

#include <QHash>
#include <QPair>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <butler/Errors.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

core::Result<QPair<QString, QString>> queryAlbumInfo(const QSqlDatabase &conn, qint64 albumId)
{
    QSqlQuery albumQ(conn);
    albumQ.prepare(QStringLiteral("SELECT title, album_artist FROM albums WHERE id = ?;"));
    albumQ.addBindValue(albumId);
    if (!albumQ.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = albumQ.lastError().text(),
            .detail = albumQ.lastQuery(),
        };
    }
    if (!albumQ.next()) {
        return core::Error {
            .code = QString(errc::kMbMatchAlbumNotFound),
            .message = QStringLiteral("Album not found"),
            .detail = QString::number(albumId),
        };
    }
    return qMakePair(albumQ.value(0).toString(), albumQ.value(1).toString());
}

MbAlbumTrack parseAlbumTrackRow(const QSqlQuery &tracksQ)
{
    MbAlbumTrack trk;
    trk.local.trackId = tracksQ.value(0).toLongLong();
    trk.local.title = tracksQ.value(1).toString();
    trk.local.durationMs = tracksQ.value(2).isNull() ? 0 : tracksQ.value(2).toLongLong();
    if (!tracksQ.value(3).isNull()) {
        trk.local.discNumber = tracksQ.value(3).toInt();
    }
    if (!tracksQ.value(4).isNull()) {
        trk.local.trackNumber = tracksQ.value(4).toInt();
    }
    trk.artist = tracksQ.value(5).toString();
    trk.albumArtist = tracksQ.value(6).toString();
    if (!tracksQ.value(7).isNull()) {
        trk.year = tracksQ.value(7).toInt();
    }
    if (!tracksQ.value(8).isNull()) {
        trk.trackTotal = tracksQ.value(8).toInt();
    }
    if (!tracksQ.value(9).isNull()) {
        trk.discTotal = tracksQ.value(9).toInt();
    }
    trk.titleNeedsOnline = tracksQ.value(10).toBool();
    return trk;
}

core::Result<QList<MbAlbumTrack>> queryAlbumTracks(const QSqlDatabase &conn, qint64 albumId)
{
    QSqlQuery tracksQ(conn);
    tracksQ.prepare(
        QStringLiteral("SELECT t.id, em.title, f.duration_ms, em.disc_number, em.track_number, "
                       "       em.artist, em.album_artist, em.year, em.track_total, em.disc_total, "
                       "       EXISTS("
                       "           SELECT 1 FROM track_issues ti "
                       "           WHERE ti.track_id = t.id "
                       "             AND ti.kind = 'needs_online' "
                       "             AND ti.field = 'title'"
                       "       ) AS title_needs_online "
                       "FROM tracks t "
                       "JOIN files f ON f.id = t.file_id "
                       "LEFT JOIN effective_metadata em ON em.track_id = t.id "
                       "WHERE t.album_id = ?;"));
    tracksQ.addBindValue(albumId);
    if (!tracksQ.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = tracksQ.lastError().text(),
            .detail = tracksQ.lastQuery(),
        };
    }

    QList<MbAlbumTrack> tracks;
    while (tracksQ.next()) {
        tracks.append(parseAlbumTrackRow(tracksQ));
    }
    return tracks;
}

void sortAlbumTracks(QList<MbAlbumTrack> &tracks)
{
    std::ranges::stable_sort(tracks, [](const MbAlbumTrack &a, const MbAlbumTrack &b) {
        const int discA = a.local.discNumber.value_or(0);
        const int discB = b.local.discNumber.value_or(0);
        if (discA != discB) {
            return discA < discB;
        }
        const int trkA = a.local.trackNumber.value_or(0);
        const int trkB = b.local.trackNumber.value_or(0);
        if (trkA != trkB) {
            return trkA < trkB;
        }
        return a.local.trackId < b.local.trackId;
    });
}

QString determineSearchArtist(const QString &searchArtist, const QList<MbAlbumTrack> &tracks)
{
    if (!searchArtist.trimmed().isEmpty()) {
        return searchArtist;
    }
    QHash<QString, int> artistCounts;
    for (const auto &t : tracks) {
        const QString a = t.artist.trimmed();
        if (!a.isEmpty()) {
            artistCounts.insert(a, artistCounts.value(a, 0) + 1);
        }
    }
    int maxCount = 0;
    QString bestArtist;
    for (auto it = artistCounts.cbegin(); it != artistCounts.cend(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            bestArtist = it.key();
        }
    }
    return bestArtist;
}

QString statusToString(MbMatchStatus status)
{
    switch (status) {
    case MbMatchStatus::Matched:
        return QStringLiteral("matched");
    case MbMatchStatus::Ambiguous:
        return QStringLiteral("ambiguous");
    case MbMatchStatus::NoMatch:
        return QStringLiteral("no_match");
    }
    return QStringLiteral("no_match");
}

core::Result<void> insertAlbumMatchRow(
    const QSqlDatabase &conn, qint64 albumId, const MbAlbumResult &result, qint64 now)
{
    QSqlQuery albumMatchQ(conn);
    albumMatchQ.prepare(
        QStringLiteral("INSERT OR REPLACE INTO mb_album_matches "
                       "(album_id, status, release_id, release_group_id, score, label, matched_at) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?);"));
    albumMatchQ.addBindValue(albumId);
    albumMatchQ.addBindValue(statusToString(result.status));

    if (result.release.has_value() && !result.release->id.isEmpty()) {
        albumMatchQ.addBindValue(result.release->id);
    } else {
        albumMatchQ.addBindValue(QVariant(QMetaType(QMetaType::QString)));
    }

    if (result.release.has_value() && !result.release->releaseGroupId.isEmpty()) {
        albumMatchQ.addBindValue(result.release->releaseGroupId);
    } else {
        albumMatchQ.addBindValue(QVariant(QMetaType(QMetaType::QString)));
    }

    if (result.match.has_value()) {
        albumMatchQ.addBindValue(result.match->score);
    } else {
        albumMatchQ.addBindValue(QVariant(QMetaType(QMetaType::Double)));
    }

    if (result.release.has_value() && !result.release->labels.isEmpty()) {
        albumMatchQ.addBindValue(result.release->labels.join(QStringLiteral("; ")));
    } else {
        albumMatchQ.addBindValue(QVariant(QMetaType(QMetaType::QString)));
    }

    albumMatchQ.addBindValue(now);

    if (!albumMatchQ.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = albumMatchQ.lastError().text(),
            .detail = albumMatchQ.lastQuery(),
        };
    }
    return { };
}

core::Result<void> insertTrackMatches(const QSqlDatabase &conn, const MbAlbumResult &result)
{
    if (result.status != MbMatchStatus::Matched || !result.match.has_value()
        || !result.release.has_value()) {
        return { };
    }

    QSqlQuery trackMatchQ(conn);
    trackMatchQ.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO mb_track_matches (track_id, release_id, recording_id, score) "
        "VALUES (?, ?, ?, ?);"));

    const auto &rel = *result.release;
    for (const auto &mapping : result.match->mapping) {
        if (mapping.mbIndex < 0 || mapping.mbIndex >= rel.tracks.size()) {
            continue;
        }
        const auto &mbTrack = rel.tracks.at(mapping.mbIndex);
        trackMatchQ.addBindValue(mapping.trackId);
        trackMatchQ.addBindValue(rel.id);
        trackMatchQ.addBindValue(mbTrack.recordingId);
        trackMatchQ.addBindValue(mapping.score);

        if (!trackMatchQ.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = trackMatchQ.lastError().text(),
                .detail = trackMatchQ.lastQuery(),
            };
        }
    }
    return { };
}

} // namespace

LocalAlbum MbAlbumInput::toLocalAlbum() const
{
    LocalAlbum album;
    album.title = searchTitle;
    album.tracks.reserve(tracks.size());
    for (const auto &t : tracks) {
        LocalTrack lt = t.local;
        lt.titleUnknown = t.titleNeedsOnline;
        album.tracks.append(std::move(lt));
    }
    return album;
}

MbMatchSource::MbMatchSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<qint64>> MbMatchSource::pendingAlbums() const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT a.id "
                             "FROM albums a "
                             "WHERE NOT EXISTS ("
                             "    SELECT 1 FROM mb_album_matches m WHERE m.album_id = a.id"
                             ") "
                             "AND EXISTS ("
                             "    SELECT 1 "
                             "    FROM tracks t "
                             "    LEFT JOIN effective_metadata em ON em.track_id = t.id "
                             "    WHERE t.album_id = a.id "
                             "      AND ("
                             "          em.year IS NULL "
                             "          OR em.album_artist IS NULL "
                             "          OR em.album_artist = '' "
                             "          OR em.track_number IS NULL "
                             "          OR EXISTS ("
                             "              SELECT 1 FROM track_issues ti "
                             "              WHERE ti.track_id = t.id "
                             "                AND ti.kind = 'needs_online' "
                             "                AND ti.field = 'title'"
                             "          )"
                             "      )"
                             ") "
                             "ORDER BY a.id ASC;"));

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }

    QList<qint64> albumIds;
    while (q.next()) {
        albumIds.append(q.value(0).toLongLong());
    }
    return albumIds;
}

core::Result<MbAlbumInput> MbMatchSource::load(qint64 albumId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const auto albumInfoRes = queryAlbumInfo(conn, albumId);
    if (!albumInfoRes.ok()) {
        return albumInfoRes.error();
    }

    const auto tracksRes = queryAlbumTracks(conn, albumId);
    if (!tracksRes.ok()) {
        return tracksRes.error();
    }

    MbAlbumInput input;
    input.albumId = albumId;
    input.searchTitle = albumInfoRes.value().first;
    input.tracks = tracksRes.value();

    sortAlbumTracks(input.tracks);
    input.searchArtist = determineSearchArtist(albumInfoRes.value().second, input.tracks);

    return input;
}

core::Result<void> MbMatchSource::saveMatch(
    qint64 albumId, const MbAlbumResult &result, qint64 now) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn, library::Transaction::Mode::Immediate);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for saveMatch"),
            .detail = { },
        };
    }

    const auto insertAlbumRes = insertAlbumMatchRow(conn, albumId, result, now);
    if (!insertAlbumRes.ok()) {
        return insertAlbumRes.error();
    }

    const auto insertTrackRes = insertTrackMatches(conn, result);
    if (!insertTrackRes.ok()) {
        return insertTrackRes.error();
    }

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        return commitRes.error();
    }

    return { };
}

} // namespace linernotes::butler
