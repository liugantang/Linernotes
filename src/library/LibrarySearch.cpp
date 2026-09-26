// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LibrarySearch.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <library/Errors.h>
#include <library/LibraryQuery.h>
#include <library/SearchIndex.h>

namespace linernotes::library {

namespace {

core::Result<QList<TrackRow>> fetchHitTracks(
    const LibraryQuery &query, const QSqlDatabase &db, const QString &userInput, int trackLimit)
{
    if (userInput.trimmed().isEmpty() || trackLimit <= 0) {
        return QList<TrackRow> { };
    }

    const SearchIndex searchIndex(db);
    const auto hitsRes = searchIndex.search(userInput, trackLimit);
    if (!hitsRes.ok()) {
        return hitsRes.error();
    }

    const auto &hits = hitsRes.value();
    if (hits.isEmpty()) {
        return QList<TrackRow> { };
    }

    QList<qint64> trackIds;
    trackIds.reserve(hits.size());
    for (const auto &hit : hits) {
        trackIds.append(hit.trackId);
    }

    return query.tracksByIds(trackIds);
}

QList<qint64> orderedUniqueAlbumIds(const QList<TrackRow> &tracks, int limit)
{
    QList<qint64> albumIds;
    if (limit <= 0) {
        return albumIds;
    }
    albumIds.reserve(limit);
    QSet<qint64> seenAlbumIds;
    for (const auto &track : tracks) {
        if (track.albumId.has_value() && track.albumId.value() > 0) {
            const qint64 aid = track.albumId.value();
            if (!seenAlbumIds.contains(aid)) {
                seenAlbumIds.insert(aid);
                albumIds.append(aid);
                if (albumIds.size() >= limit) {
                    break;
                }
            }
        }
    }
    return albumIds;
}

core::Result<QList<qint64>> orderedArtistIdsForTracks(
    const QSqlDatabase &db, const QList<TrackRow> &tracks, int limit)
{
    QList<qint64> artistIds;
    if (tracks.isEmpty() || limit <= 0) {
        return artistIds;
    }

    QList<qint64> trackIds;
    trackIds.reserve(tracks.size());
    for (const auto &t : tracks) {
        trackIds.append(t.trackId);
    }

    QStringList trackPlaceholders;
    trackPlaceholders.reserve(trackIds.size());
    for (int i = 0; i < trackIds.size(); ++i) {
        trackPlaceholders.append(QStringLiteral("?"));
    }

    const QString taSql = QStringLiteral("SELECT track_id, artist_id FROM track_artists "
                                         "WHERE track_id IN (%1) AND role = 'artist' "
                                         "ORDER BY position ASC")
                              .arg(trackPlaceholders.join(QStringLiteral(", ")));

    QSqlQuery qTa(db);
    qTa.prepare(taSql);
    for (int i = 0; i < trackIds.size(); ++i) {
        qTa.bindValue(i, trackIds.at(i));
    }

    if (!qTa.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("search track_artists query failed"),
            .detail = qTa.lastError().text(),
        };
    }

    QHash<qint64, QList<qint64>> trackArtistsMap;
    while (qTa.next()) {
        const qint64 tid = qTa.value(0).toLongLong();
        const qint64 aid = qTa.value(1).toLongLong();
        auto it = trackArtistsMap.find(tid);
        if (it == trackArtistsMap.end()) {
            it = trackArtistsMap.insert(tid, { });
        }
        it.value().append(aid);
    }

    artistIds.reserve(limit);
    QSet<qint64> seenArtistIds;
    for (const auto &t : tracks) {
        const auto it = trackArtistsMap.constFind(t.trackId);
        if (it != trackArtistsMap.constEnd()) {
            for (const qint64 aid : *it) {
                if (aid > 0 && !seenArtistIds.contains(aid)) {
                    seenArtistIds.insert(aid);
                    artistIds.append(aid);
                    if (artistIds.size() >= limit) {
                        return artistIds;
                    }
                }
            }
        }
    }

    return artistIds;
}

} // namespace

LibrarySearch::LibrarySearch(const QSqlDatabase &db)
    : m_db(db)
{
}

core::Result<SearchResults> LibrarySearch::search(
    const QString &userInput, int trackLimit, int groupLimit) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    const LibraryQuery query(m_db);
    const auto tracksRes = fetchHitTracks(query, m_db, userInput, trackLimit);
    if (!tracksRes.ok()) {
        return tracksRes.error();
    }

    SearchResults results;
    results.tracks = tracksRes.value();
    if (results.tracks.isEmpty() || groupLimit <= 0) {
        return results;
    }

    const QList<qint64> albumIds = orderedUniqueAlbumIds(results.tracks, groupLimit);
    const auto albumsRes = query.albumsByIds(albumIds);
    if (!albumsRes.ok()) {
        return albumsRes.error();
    }
    results.albums = albumsRes.value();

    const auto artistIdsRes = orderedArtistIdsForTracks(m_db, results.tracks, groupLimit);
    if (!artistIdsRes.ok()) {
        return artistIdsRes.error();
    }
    const auto artistsRes = query.artistsByIds(artistIdsRes.value());
    if (!artistsRes.ok()) {
        return artistsRes.error();
    }
    results.artists = artistsRes.value();

    return results;
}

} // namespace linernotes::library
