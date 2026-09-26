// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

/**
 * @file LibraryQuery.cpp
 * @brief Performance-optimized library browse queries for Tracks, Albums, and Artists.
 *
 * Design and Performance Architecture:
 * 1. Dedicated `track_sort` table:
 *    Deep pagination (e.g. OFFSET 50000) over normalized schemas requires traversing thousands
 *    of rows and joining `files` on every row just to check visibility (`missing_since IS NULL`).
 *    By materializing visible state and all browse/sort columns into a dedicated `track_sort` table
 *    maintained automatically via triggers on `effective_metadata`, `files`, and `tracks`,
 *    SQLite can execute the pagination step (`WITH page AS ...`) as a pure covering index scan.
 *    Skipping 50,000 items in a covering B-Tree takes under 2 ms instead of hundreds of
 * milliseconds.
 *
 * 2. Two Partial Indexes per Sort Key (`WHERE visible = 1`):
 *    - Ascending (NULLs Last): SQLite sorts NULLs first by default in ASC order. Writing
 *      `ORDER BY col ASC NULLS LAST` disables regular B-Tree indexing and triggers temporary sorts.
 *      To preserve indexing while keeping NULLs at the end, we define expression indexes like
 *      `(col IS NULL, col, track_id) WHERE visible = 1` and write queries with matching
 *      `ORDER BY col IS NULL, col ASC, track_id ASC`.
 *    - Descending (NULLs Last): In SQLite, NULL is considered smaller than any value. Therefore,
 *      in DESC order, non-NULL values appear first in descending order, and NULLs appear last
 * naturally. A regular index `(col, track_id) WHERE visible = 1` can thus be scanned backwards
 *      (`SCAN ... BACKWARD`) with no temporary B-Tree.
 *    - `album_artist_key`: Computed as `COALESCE(album_artist, artist)` so tracks without an album
 * artist are grouped by track artist rather than appearing as empty album artists at the top of
 * Default sort.
 *    - Two-step query: The inner CTE (`page`) selects only <= 200 `track_id`s using the covering
 * index, and the outer query joins metadata, covers, favorites, and ratings only for that page.
 */

#include "LibraryQuery.h"

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <library/Errors.h>

#include <algorithm>

namespace linernotes::library {

namespace {

TrackRow parseTrackRow(const QSqlQuery &q)
{
    TrackRow row;
    row.trackId = q.value(0).toLongLong();
    row.fileId = q.value(1).toLongLong();
    if (!q.value(2).isNull()) {
        row.albumId = q.value(2).toLongLong();
    }
    row.path = q.value(3).toString();
    row.title = q.value(4).toString();
    row.artist = q.value(5).toString();
    row.album = q.value(6).toString();
    row.albumArtist = q.value(7).toString();
    row.genre = q.value(8).toString();
    row.composer = q.value(9).toString();
    if (!q.value(10).isNull()) {
        row.year = q.value(10).toInt();
    }
    if (!q.value(11).isNull()) {
        row.trackNumber = q.value(11).toInt();
    }
    if (!q.value(12).isNull()) {
        row.discNumber = q.value(12).toInt();
    }
    row.durationMs = q.value(13).toLongLong();
    row.codec = q.value(14).toString();
    row.sampleRate = q.value(15).toInt();
    row.bitDepth = q.value(16).toInt();
    row.bitrate = q.value(17).toInt();
    row.coverHash = q.value(18).toString();
    row.favorite = q.value(19).toInt() != 0;
    row.rating = q.value(20).toInt();
    row.addedAt = q.value(21).toLongLong();
    return row;
}

AlbumRow parseAlbumRow(const QSqlQuery &q)
{
    AlbumRow row;
    row.albumId = q.value(0).toLongLong();
    row.title = q.value(1).toString();
    row.albumArtist = q.value(2).toString();
    if (!q.value(3).isNull()) {
        row.year = q.value(3).toInt();
    }
    row.trackCount = q.value(4).toInt();
    row.totalDurationMs = q.value(5).toLongLong();
    row.coverHash = q.value(6).toString();
    row.favorite = q.value(7).toInt() != 0;
    return row;
}

ArtistRow parseArtistRow(const QSqlQuery &q)
{
    ArtistRow row;
    row.artistId = q.value(0).toLongLong();
    row.name = q.value(1).toString();
    row.trackCount = q.value(2).toInt();
    row.albumCount = q.value(3).toInt();
    row.coverHash = q.value(4).toString();
    row.favorite = q.value(5).toInt() != 0;
    return row;
}

struct OrderClauses {
    QString pageOrder;
    QString outerOrder;
};

OrderClauses buildTrackOrderClauses(TrackSortKey key, bool isAsc)
{
    OrderClauses clauses;
    switch (key) {
    case TrackSortKey::Default:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("ts.album_artist_key IS NULL, ts.album_artist_key ASC, "
                                 "ts.album IS NULL, ts.album ASC, "
                                 "ts.disc_number IS NULL, ts.disc_number ASC, "
                                 "ts.track_number IS NULL, ts.track_number ASC, "
                                 "ts.path ASC, ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("COALESCE(em.album_artist, em.artist) IS NULL, "
                                                "COALESCE(em.album_artist, em.artist) ASC, "
                                                "em.album IS NULL, em.album ASC, "
                                                "em.disc_number IS NULL, em.disc_number ASC, "
                                                "em.track_number IS NULL, em.track_number ASC, "
                                                "f.path ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.album_artist_key DESC, ts.album DESC, "
                                               "ts.disc_number DESC, ts.track_number DESC, "
                                               "ts.path DESC, ts.track_id DESC");
            clauses.outerOrder
                = QStringLiteral("COALESCE(em.album_artist, em.artist) DESC, em.album DESC, "
                                 "em.disc_number DESC, em.track_number DESC, "
                                 "f.path DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Title:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.title IS NULL, ts.title ASC, ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.title IS NULL, em.title ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.title DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("em.title DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Artist:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.artist IS NULL, ts.artist ASC, ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.artist IS NULL, em.artist ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.artist DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("em.artist DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Album:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.album IS NULL, ts.album ASC, "
                                               "ts.disc_number IS NULL, ts.disc_number ASC, "
                                               "ts.track_number IS NULL, ts.track_number ASC, "
                                               "ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.album IS NULL, em.album ASC, "
                                                "em.disc_number IS NULL, em.disc_number ASC, "
                                                "em.track_number IS NULL, em.track_number ASC, "
                                                "t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral(
                "ts.album DESC, ts.disc_number DESC, ts.track_number DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral(
                "em.album DESC, em.disc_number DESC, em.track_number DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Year:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.year IS NULL, ts.year ASC, "
                                               "ts.album IS NULL, ts.album ASC, "
                                               "ts.disc_number IS NULL, ts.disc_number ASC, "
                                               "ts.track_number IS NULL, ts.track_number ASC, "
                                               "ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.year IS NULL, em.year ASC, "
                                                "em.album IS NULL, em.album ASC, "
                                                "em.disc_number IS NULL, em.disc_number ASC, "
                                                "em.track_number IS NULL, em.track_number ASC, "
                                                "t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.year DESC, ts.album DESC, ts.disc_number DESC, "
                                               "ts.track_number DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("em.year DESC, em.album DESC, em.disc_number DESC, "
                                                "em.track_number DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Duration:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("ts.duration_ms IS NULL, ts.duration_ms ASC, ts.track_id ASC");
            clauses.outerOrder
                = QStringLiteral("f.duration_ms IS NULL, f.duration_ms ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.duration_ms DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("f.duration_ms DESC, t.id DESC");
        }
        break;

    case TrackSortKey::DateAdded:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("ts.added_at IS NULL, ts.added_at ASC, ts.track_id ASC");
            clauses.outerOrder
                = QStringLiteral("f.first_seen_at IS NULL, f.first_seen_at ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.added_at DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("f.first_seen_at DESC, t.id DESC");
        }
        break;
    }
    return clauses;
}

OrderClauses buildAlbumOrderClauses(AlbumSortKey key, bool isAsc)
{
    OrderClauses clauses;
    switch (key) {
    case AlbumSortKey::Title:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("a.title IS NULL, a.title ASC, a.id ASC");
            clauses.outerOrder = QStringLiteral("p.title IS NULL, p.title ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.title DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.title DESC, p.id DESC");
        }
        break;

    case AlbumSortKey::Artist:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("a.album_artist IS NULL, a.album_artist ASC, a.id ASC");
            clauses.outerOrder
                = QStringLiteral("p.album_artist IS NULL, p.album_artist ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.album_artist DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.album_artist DESC, p.id DESC");
        }
        break;

    case AlbumSortKey::Year:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("a.year IS NULL, a.year ASC, a.id ASC");
            clauses.outerOrder = QStringLiteral("p.year IS NULL, p.year ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.year DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.year DESC, p.id DESC");
        }
        break;

    case AlbumSortKey::DateAdded:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("a.created_at IS NULL, a.created_at ASC, a.id ASC");
            clauses.outerOrder = QStringLiteral("p.created_at IS NULL, p.created_at ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.created_at DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.created_at DESC, p.id DESC");
        }
        break;
    }
    return clauses;
}

QString buildTrackFilterWhereSql(const TrackFilter &filter, QList<QVariant> &binds)
{
    QString whereSql;
    if (filter.albumId.has_value()) {
        whereSql += QStringLiteral(" AND ts.album_id = ?");
        binds.append(filter.albumId.value());
    }

    if (filter.artistId.has_value()) {
        whereSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM track_artists ta WHERE ta.track_id = ts.track_id AND "
            "ta.artist_id = ? AND ta.role = 'artist')");
        binds.append(filter.artistId.value());
    }

    if (filter.genre.has_value()) {
        whereSql += QStringLiteral(" AND ts.genre = ?");
        binds.append(filter.genre.value());
    }

    if (filter.favoritesOnly) {
        whereSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'track' AND "
            "fav.entity_id = ts.track_id)");
    }
    return whereSql;
}

} // namespace

LibraryQuery::LibraryQuery(const QSqlDatabase &db)
    : m_db(db)
{
}

core::Result<int> LibraryQuery::countTracks(const TrackFilter &filter) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (!filter.albumId.has_value() && !filter.artistId.has_value() && !filter.genre.has_value()
        && !filter.favoritesOnly) {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM track_sort WHERE visible = 1;"))) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = QStringLiteral("countTracks failed"),
                .detail = q.lastError().text(),
            };
        }
        if (q.next()) {
            return q.value(0).toInt();
        }
        return 0;
    }

    QList<QVariant> binds;
    const QString whereSql = buildTrackFilterWhereSql(filter, binds);
    const QString sql
        = QStringLiteral("SELECT COUNT(*) FROM track_sort ts WHERE ts.visible = 1") + whereSql;

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("countTracks failed"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return q.value(0).toInt();
    }

    return 0;
}

core::Result<QList<TrackRow>> LibraryQuery::tracks(
    const TrackFilter &filter, TrackSortKey key, Qt::SortOrder order, int offset, int limit) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (limit <= 0 || offset < 0) {
        return QList<TrackRow> { };
    }

    const bool isAsc = (order == Qt::AscendingOrder);
    const OrderClauses orderClauses = buildTrackOrderClauses(key, isAsc);

    QList<QVariant> binds;
    const QString pageWhereSql = buildTrackFilterWhereSql(filter, binds);

    binds.append(limit);
    binds.append(offset);

    const QString sql = QStringLiteral(
        "WITH page AS ("
        "  SELECT ts.track_id "
        "  FROM track_sort ts "
        "  WHERE ts.visible = 1 %1 "
        "  ORDER BY %2 "
        "  LIMIT ? OFFSET ?"
        ") "
        "SELECT t.id, t.file_id, t.album_id, f.path, "
        "em.title, em.artist, em.album, em.album_artist, em.genre, em.composer, "
        "em.year, em.track_number, em.disc_number, "
        "f.duration_ms, f.codec, f.sample_rate, f.bit_depth, f.bitrate, "
        "c.hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav, "
        "COALESCE(r.rating, 0) AS rating, "
        "f.first_seen_at "
        "FROM page p "
        "JOIN tracks t ON p.track_id = t.id "
        "JOIN files f ON t.file_id = f.id "
        "LEFT JOIN effective_metadata em ON t.id = em.track_id "
        "LEFT JOIN covers c ON f.cover_id = c.id "
        "LEFT JOIN favorites fav ON fav.entity_type = 'track' AND fav.entity_id = t.id "
        "LEFT JOIN ratings r ON r.track_id = t.id "
        "ORDER BY %3;")
                            .arg(pageWhereSql, orderClauses.pageOrder, orderClauses.outerOrder);

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("tracks browse query failed"),
            .detail = q.lastError().text(),
        };
    }

    QList<TrackRow> rows;
    rows.reserve(std::min(limit, 256));
    while (q.next()) {
        rows.append(parseTrackRow(q));
    }

    return rows;
}

core::Result<QList<qint64>> LibraryQuery::trackIds(
    const TrackFilter &filter, TrackSortKey key, Qt::SortOrder order) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    const bool isAsc = (order == Qt::AscendingOrder);
    const OrderClauses orderClauses = buildTrackOrderClauses(key, isAsc);

    QList<QVariant> binds;
    const QString whereSql = buildTrackFilterWhereSql(filter, binds);

    const QString sql = QStringLiteral("SELECT ts.track_id "
                                       "FROM track_sort ts "
                                       "WHERE ts.visible = 1 %1 "
                                       "ORDER BY %2;")
                            .arg(whereSql, orderClauses.pageOrder);

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("trackIds query failed"),
            .detail = q.lastError().text(),
        };
    }

    QList<qint64> ids;
    while (q.next()) {
        ids.append(q.value(0).toLongLong());
    }

    return ids;
}

core::Result<QList<TrackRow>> LibraryQuery::tracksByIds(const QList<qint64> &ids) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (ids.isEmpty()) {
        return QList<TrackRow> { };
    }

    QList<qint64> uniqueIds;
    uniqueIds.reserve(ids.size());
    QSet<qint64> seen;
    for (const qint64 id : ids) {
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            uniqueIds.append(id);
        }
    }

    if (uniqueIds.isEmpty()) {
        return QList<TrackRow> { };
    }

    QHash<qint64, TrackRow> rowMap;
    rowMap.reserve(uniqueIds.size());

    constexpr int kBatchSize = 500;
    for (int start = 0; start < uniqueIds.size(); start += kBatchSize) {
        const int count = std::min(kBatchSize, static_cast<int>(uniqueIds.size()) - start);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i) {
            placeholders.append(QStringLiteral("?"));
        }

        const QString sql = QStringLiteral(
            "SELECT t.id, t.file_id, t.album_id, f.path, "
            "em.title, em.artist, em.album, em.album_artist, em.genre, em.composer, "
            "em.year, em.track_number, em.disc_number, "
            "f.duration_ms, f.codec, f.sample_rate, f.bit_depth, f.bitrate, "
            "c.hash, "
            "(fav.entity_id IS NOT NULL) AS is_fav, "
            "COALESCE(r.rating, 0) AS rating, "
            "f.first_seen_at "
            "FROM tracks t "
            "JOIN files f ON t.file_id = f.id "
            "LEFT JOIN effective_metadata em ON t.id = em.track_id "
            "LEFT JOIN covers c ON f.cover_id = c.id "
            "LEFT JOIN favorites fav ON fav.entity_type = 'track' AND fav.entity_id = t.id "
            "LEFT JOIN ratings r ON r.track_id = t.id "
            "WHERE f.missing_since IS NULL AND t.id IN (%1)")
                                .arg(placeholders.join(QStringLiteral(", ")));

        QSqlQuery q(m_db);
        q.prepare(sql);
        for (int i = 0; i < count; ++i) {
            q.bindValue(i, uniqueIds.at(start + i));
        }

        if (!q.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = QStringLiteral("tracksByIds query failed"),
                .detail = q.lastError().text(),
            };
        }

        while (q.next()) {
            const TrackRow row = parseTrackRow(q);
            rowMap.insert(row.trackId, row);
        }
    }

    QList<TrackRow> result;
    result.reserve(ids.size());
    for (const qint64 id : ids) {
        const auto it = rowMap.constFind(id);
        if (it != rowMap.constEnd()) {
            result.append(*it);
        }
    }

    return result;
}

core::Result<int> LibraryQuery::countAlbums(const AlbumFilter &filter) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (!filter.artistId.has_value() && !filter.favoritesOnly) {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral("SELECT COUNT(DISTINCT album_id) FROM track_sort WHERE visible "
                                   "= 1 AND album_id IS NOT NULL;"))) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = QStringLiteral("countAlbums failed"),
                .detail = q.lastError().text(),
            };
        }
        if (q.next()) {
            return q.value(0).toInt();
        }
        return 0;
    }

    QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM albums a "
        "WHERE EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1)");

    QList<QVariant> binds;

    if (filter.artistId.has_value()) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM album_artists aa WHERE aa.album_id = a.id AND aa.artist_id "
            "= ?)");
        binds.append(filter.artistId.value());
    }

    if (filter.favoritesOnly) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'album' AND "
            "fav.entity_id = a.id)");
    }

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("countAlbums failed"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return q.value(0).toInt();
    }

    return 0;
}

core::Result<QList<AlbumRow>> LibraryQuery::albums(
    const AlbumFilter &filter, AlbumSortKey key, Qt::SortOrder order, int offset, int limit) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (limit <= 0 || offset < 0) {
        return QList<AlbumRow> { };
    }

    const bool isAsc = (order == Qt::AscendingOrder);
    const OrderClauses orderClauses = buildAlbumOrderClauses(key, isAsc);

    QString filterSql;
    QList<QVariant> binds;

    if (filter.artistId.has_value()) {
        filterSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM album_artists aa WHERE aa.album_id = a.id AND aa.artist_id "
            "= ?)");
        binds.append(filter.artistId.value());
    }

    if (filter.favoritesOnly) {
        filterSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'album' AND "
            "fav.entity_id = a.id)");
    }

    binds.append(limit);
    binds.append(offset);

    const QString sql = QStringLiteral(
        "WITH page AS ("
        "  SELECT a.id, a.title, a.album_artist, a.year, a.cover_id, a.created_at "
        "  FROM albums a "
        "  WHERE EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1) "
        "%1 "
        "  ORDER BY %2 "
        "  LIMIT ? OFFSET ?"
        ") "
        "SELECT p.id, p.title, p.album_artist, p.year, "
        "(SELECT COUNT(*) FROM track_sort ts WHERE ts.album_id = p.id AND ts.visible = 1) AS "
        "track_count, "
        "(SELECT COALESCE(SUM(ts.duration_ms), 0) FROM track_sort ts WHERE ts.album_id = p.id AND "
        "ts.visible = 1) AS total_duration, "
        "(SELECT c.hash FROM covers c WHERE c.id = p.cover_id) AS cover_hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav "
        "FROM page p "
        "LEFT JOIN favorites fav ON fav.entity_type = 'album' AND fav.entity_id = p.id "
        "ORDER BY %3;")
                            .arg(filterSql, orderClauses.pageOrder, orderClauses.outerOrder);

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("albums browse query failed"),
            .detail = q.lastError().text(),
        };
    }

    QList<AlbumRow> rows;
    rows.reserve(std::min(limit, 256));
    while (q.next()) {
        rows.append(parseAlbumRow(q));
    }

    return rows;
}

core::Result<std::optional<AlbumRow>> LibraryQuery::album(qint64 albumId) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    const QString sql = QStringLiteral(
        "SELECT a.id, a.title, a.album_artist, a.year, "
        "(SELECT COUNT(*) FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1) AS "
        "track_count, "
        "(SELECT COALESCE(SUM(ts.duration_ms), 0) FROM track_sort ts WHERE ts.album_id = a.id AND "
        "ts.visible = 1) AS total_duration, "
        "(SELECT c.hash FROM covers c WHERE c.id = a.cover_id) AS cover_hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav "
        "FROM albums a "
        "LEFT JOIN favorites fav ON fav.entity_type = 'album' AND fav.entity_id = a.id "
        "WHERE a.id = ? "
        "AND EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1)");

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.bindValue(0, albumId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("album query failed"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return std::make_optional(parseAlbumRow(q));
    }

    return std::optional<AlbumRow> { std::nullopt };
}

core::Result<int> LibraryQuery::countArtists(const ArtistFilter &filter) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM artists ar "
        "WHERE ("
        "  EXISTS (SELECT 1 FROM track_artists ta JOIN track_sort ts ON ta.track_id = ts.track_id "
        "WHERE ta.artist_id = ar.id AND ta.role = 'artist' AND ts.visible = 1)"
        "  OR EXISTS (SELECT 1 FROM album_artists aa JOIN track_sort ts ON aa.album_id = "
        "ts.album_id "
        "WHERE aa.artist_id = ar.id AND ts.visible = 1)"
        ")");

    if (filter.favoritesOnly) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'artist' AND "
            "fav.entity_id = ar.id)");
    }

    QSqlQuery q(m_db);
    q.prepare(sql);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("countArtists failed"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return q.value(0).toInt();
    }

    return 0;
}

core::Result<QList<ArtistRow>> LibraryQuery::artists(
    const ArtistFilter &filter, ArtistSortKey key, Qt::SortOrder order, int offset, int limit) const
{
    Q_UNUSED(key);

    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (limit <= 0 || offset < 0) {
        return QList<ArtistRow> { };
    }

    const bool isAsc = (order == Qt::AscendingOrder);
    const QString pageOrderClause = isAsc
        ? QStringLiteral("ar.name IS NULL, ar.name ASC, ar.id ASC")
        : QStringLiteral("ar.name DESC, ar.id DESC");
    const QString outerOrderClause = isAsc ? QStringLiteral("p.name IS NULL, p.name ASC, p.id ASC")
                                           : QStringLiteral("p.name DESC, p.id DESC");

    QString filterSql;
    if (filter.favoritesOnly) {
        filterSql = QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'artist' AND "
            "fav.entity_id = ar.id)");
    }

    const QString sql = QStringLiteral(
        "WITH page AS ("
        "  SELECT ar.id, ar.name "
        "  FROM artists ar "
        "  WHERE ("
        "    EXISTS (SELECT 1 FROM track_artists ta JOIN track_sort ts ON ta.track_id = "
        "ts.track_id "
        "WHERE ta.artist_id = ar.id AND ta.role = 'artist' AND ts.visible = 1)"
        "    OR EXISTS (SELECT 1 FROM album_artists aa JOIN track_sort ts ON aa.album_id = "
        "ts.album_id "
        "WHERE aa.artist_id = ar.id AND ts.visible = 1)"
        "  ) %1 "
        "  ORDER BY %2 "
        "  LIMIT ? OFFSET ?"
        ") "
        "SELECT p.id, p.name, "
        "(SELECT COUNT(DISTINCT ta.track_id) FROM track_artists ta JOIN track_sort ts ON "
        "ta.track_id = ts.track_id "
        "WHERE ta.artist_id = p.id AND ta.role = 'artist' AND ts.visible = 1) AS track_count, "
        "(SELECT COUNT(DISTINCT a.id) FROM albums a JOIN album_artists aa ON a.id = aa.album_id "
        "WHERE aa.artist_id = p.id AND EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = "
        "a.id AND ts.visible = 1)) AS album_count, "
        "(SELECT c.hash FROM albums a JOIN album_artists aa ON a.id = aa.album_id JOIN covers c "
        "ON a.cover_id = c.id WHERE aa.artist_id = p.id AND a.cover_id IS NOT NULL AND EXISTS "
        "(SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1) ORDER BY a.year "
        "ASC NULLS LAST, a.id ASC LIMIT 1) AS cover_hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav "
        "FROM page p "
        "LEFT JOIN favorites fav ON fav.entity_type = 'artist' AND fav.entity_id = p.id "
        "ORDER BY %3;")
                            .arg(filterSql, pageOrderClause, outerOrderClause);

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.bindValue(0, limit);
    q.bindValue(1, offset);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("artists browse query failed"),
            .detail = q.lastError().text(),
        };
    }

    QList<ArtistRow> rows;
    rows.reserve(std::min(limit, 256));
    while (q.next()) {
        rows.append(parseArtistRow(q));
    }

    return rows;
}

core::Result<std::optional<ArtistRow>> LibraryQuery::artist(qint64 artistId) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    const QString sql = QStringLiteral(
        "SELECT ar.id, ar.name, "
        "(SELECT COUNT(DISTINCT ta.track_id) FROM track_artists ta JOIN track_sort ts ON "
        "ta.track_id = ts.track_id "
        "WHERE ta.artist_id = ar.id AND ta.role = 'artist' AND ts.visible = 1) AS track_count, "
        "(SELECT COUNT(DISTINCT a.id) FROM albums a JOIN album_artists aa ON a.id = aa.album_id "
        "WHERE aa.artist_id = ar.id AND EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = "
        "a.id AND ts.visible = 1)) AS album_count, "
        "(SELECT c.hash FROM albums a JOIN album_artists aa ON a.id = aa.album_id JOIN covers c ON "
        "a.cover_id = c.id WHERE aa.artist_id = ar.id AND a.cover_id IS NOT NULL AND EXISTS "
        "(SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1) ORDER BY a.year "
        "ASC NULLS LAST, a.id ASC LIMIT 1) AS cover_hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav "
        "FROM artists ar "
        "LEFT JOIN favorites fav ON fav.entity_type = 'artist' AND fav.entity_id = ar.id "
        "WHERE ar.id = ? "
        "AND ("
        "  EXISTS (SELECT 1 FROM track_artists ta JOIN track_sort ts ON ta.track_id = ts.track_id "
        "WHERE ta.artist_id = ar.id AND ta.role = 'artist' AND ts.visible = 1)"
        "  OR EXISTS (SELECT 1 FROM album_artists aa JOIN track_sort ts ON aa.album_id = "
        "ts.album_id "
        "WHERE aa.artist_id = ar.id AND ts.visible = 1)"
        ")");

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.bindValue(0, artistId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("artist query failed"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return std::make_optional(parseArtistRow(q));
    }

    return std::optional<ArtistRow> { std::nullopt };
}

} // namespace linernotes::library
