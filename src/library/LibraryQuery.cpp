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

#include "LibraryQuerySql.h"

#include <QList>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <library/Errors.h>

#include <algorithm>

namespace linernotes::library {

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
        && !filter.favoritesOnly && !filter.playlistId.has_value()
        && !filter.smartRule.has_value()) {
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
    const QString whereSql = detail::buildTrackFilterWhereSql(filter, binds);
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

namespace {

struct EffectiveTrackSort {
    TrackSortKey key;
    Qt::SortOrder order;
};

EffectiveTrackSort resolveTrackSort(
    const TrackFilter &filter, TrackSortKey key, Qt::SortOrder order)
{
    if (key != TrackSortKey::PlaylistOrder || filter.playlistId.has_value()) {
        return { .key = key, .order = order };
    }
    if (filter.smartRule.has_value()) {
        TrackSortKey k = filter.smartRule->sortKey;
        if (k == TrackSortKey::PlaylistOrder) {
            k = TrackSortKey::Default;
        }
        return { .key = k, .order = filter.smartRule->sortOrder };
    }
    return { .key = TrackSortKey::Default, .order = order };
}

} // namespace

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

    const auto [effectiveKey, effectiveOrder] = resolveTrackSort(filter, key, order);
    const bool isAsc = (effectiveOrder == Qt::AscendingOrder);
    const detail::OrderClauses orderClauses
        = detail::buildTrackOrderClauses(effectiveKey, isAsc, filter.playlistId);

    QList<QVariant> binds;
    const QString pageWhereSql = detail::buildTrackFilterWhereSql(filter, binds);

    binds.append(limit);
    binds.append(offset);

    const QString sql = QStringLiteral("WITH page AS ("
                                       "  SELECT ts.track_id "
                                       "  FROM track_sort ts "
                                       "  WHERE ts.visible = 1 %1 "
                                       "  ORDER BY %2 "
                                       "  LIMIT ? OFFSET ?"
                                       ") "
                                       "%3 "
                                       "ORDER BY %4;")
                            .arg(pageWhereSql, orderClauses.pageOrder,
                                detail::trackSelectSql(
                                    QStringLiteral("page p JOIN tracks t ON p.track_id = t.id")),
                                orderClauses.outerOrder);

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
        rows.append(detail::parseTrackRow(q));
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

    const auto [effectiveKey, effectiveOrder] = resolveTrackSort(filter, key, order);
    const bool isAsc = (effectiveOrder == Qt::AscendingOrder);
    const detail::OrderClauses orderClauses
        = detail::buildTrackOrderClauses(effectiveKey, isAsc, filter.playlistId);

    QList<QVariant> binds;
    const QString whereSql = detail::buildTrackFilterWhereSql(filter, binds);

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
    return detail::fetchRowsByIds<TrackRow>(m_db, ids,
        QStringLiteral("%1 WHERE f.missing_since IS NULL AND t.id IN (%2)"),
        detail::trackSelectSql(), detail::parseTrackRow, QStringLiteral("tracksByIds"));
}

core::Result<QList<TrackRow>> LibraryQuery::otherVersions(qint64 trackId) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (trackId <= 0) {
        return QList<TrackRow> { };
    }

    const QString sql
        = QStringLiteral("%1 "
                         "WHERE t.work_id IS NOT NULL "
                         "  AND t.work_id = (SELECT work_id FROM tracks WHERE id = ?) "
                         "  AND t.id != ? "
                         "  AND f.missing_since IS NULL "
                         "ORDER BY "
                         "  CASE WHEN tv.version_type = 'studio' THEN 0 ELSE 1 END ASC, "
                         "  em.year IS NULL, em.year ASC, "
                         "  em.album IS NULL, em.album ASC, "
                         "  t.id ASC "
                         "LIMIT 50;")
              .arg(detail::trackSelectSql());

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.bindValue(0, trackId);
    q.bindValue(1, trackId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("otherVersions query failed"),
            .detail = q.lastError().text(),
        };
    }

    QList<TrackRow> rows;
    while (q.next()) {
        rows.append(detail::parseTrackRow(q));
    }

    return rows;
}

core::Result<QHash<QString, qint64>> LibraryQuery::trackIdsByPaths(const QStringList &paths) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (paths.isEmpty()) {
        return QHash<QString, qint64> { };
    }

    QList<QString> uniquePaths;
    uniquePaths.reserve(paths.size());
    QSet<QString> seen;
    for (const auto &p : paths) {
        if (!p.isEmpty() && !seen.contains(p)) {
            seen.insert(p);
            uniquePaths.append(p);
        }
    }

    if (uniquePaths.isEmpty()) {
        return QHash<QString, qint64> { };
    }

    QHash<QString, qint64> result;
    result.reserve(uniquePaths.size());

    constexpr int kBatchSize = 500;
    for (int start = 0; start < uniquePaths.size(); start += kBatchSize) {
        const int count = std::min(kBatchSize, static_cast<int>(uniquePaths.size()) - start);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i) {
            placeholders.append(QStringLiteral("?"));
        }

        const QString sql = QStringLiteral("SELECT f.path, t.id "
                                           "FROM tracks t "
                                           "JOIN files f ON t.file_id = f.id "
                                           "WHERE f.missing_since IS NULL AND f.path IN (%1);")
                                .arg(placeholders.join(QStringLiteral(", ")));

        QSqlQuery q(m_db);
        q.prepare(sql);
        for (int i = 0; i < count; ++i) {
            q.bindValue(i, uniquePaths.at(start + i));
        }

        if (!q.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = QStringLiteral("trackIdsByPaths query failed"),
                .detail = q.lastError().text(),
            };
        }

        while (q.next()) {
            result.insert(q.value(0).toString(), q.value(1).toLongLong());
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

    QList<QVariant> binds;
    const QString filterSql = detail::buildAlbumFilterWhereSql(filter, binds);
    const QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM albums a "
        "WHERE EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1)%1")
                            .arg(filterSql);

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
    const detail::OrderClauses orderClauses = detail::buildAlbumOrderClauses(key, isAsc);

    QList<QVariant> binds;
    const QString filterSql = detail::buildAlbumFilterWhereSql(filter, binds);

    binds.append(limit);
    binds.append(offset);

    // Reuse outer SELECT projection and subqueries from detail::albumSelectSql ("page p", "p")
    const QString sql = QStringLiteral(
        "WITH page AS ("
        "  SELECT a.id, a.title, a.album_artist, a.year, a.cover_id, a.created_at "
        "  FROM albums a "
        "  WHERE EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1) "
        "%1 "
        "  ORDER BY %2 "
        "  LIMIT ? OFFSET ?"
        ") "
        "%3 "
        "ORDER BY %4;")
                            .arg(filterSql, orderClauses.pageOrder,
                                detail::albumSelectSql(
                                    QStringLiteral("page p"), QStringLiteral("p")),
                                orderClauses.outerOrder);

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
        rows.append(detail::parseAlbumRow(q));
    }

    return rows;
}

core::Result<QList<AlbumRow>> LibraryQuery::albumsByIds(const QList<qint64> &ids) const
{
    return detail::fetchRowsByIds<AlbumRow>(m_db, ids,
        QStringLiteral(
            "%1 WHERE a.id IN (%2) "
            "AND EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1)"),
        detail::albumSelectSql(), detail::parseAlbumRow, QStringLiteral("albumsByIds"));
}

core::Result<std::optional<AlbumRow>> LibraryQuery::album(qint64 albumId) const
{
    const auto rowsRes = albumsByIds({ albumId });
    if (!rowsRes.ok()) {
        return rowsRes.error();
    }
    const auto &rows = rowsRes.value();
    if (rows.isEmpty()) {
        return std::optional<AlbumRow> { std::nullopt };
    }
    return std::make_optional(rows.first());
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

    const QString sql = QStringLiteral("SELECT COUNT(*) "
                                       "FROM artists ar "
                                       "WHERE %1%2")
                            .arg(detail::artistVisibilityWhereSql(QStringLiteral("ar.id")),
                                detail::buildArtistFilterWhereSql(filter));

    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
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
    const QString nameExpr
        = detail::artistDisplayNameSql(QStringLiteral("ar"), filter.namePreference);
    const QString pageOrderClause = isAsc
        ? QStringLiteral("%1 IS NULL, %1 ASC, ar.id ASC").arg(nameExpr)
        : QStringLiteral("%1 DESC, ar.id DESC").arg(nameExpr);
    const QString outerOrderClause = isAsc ? QStringLiteral("p.name IS NULL, p.name ASC, p.id ASC")
                                           : QStringLiteral("p.name DESC, p.id DESC");

    const QString filterSql = detail::buildArtistFilterWhereSql(filter);

    // Reuse outer SELECT projection and subqueries from detail::artistPageSelectSql
    const QString sql
        = QStringLiteral("WITH page AS ("
                         "  SELECT ar.id, %1 AS name, ar.name AS original_name "
                         "  FROM artists ar "
                         "  WHERE %2%3 "
                         "  ORDER BY %4 "
                         "  LIMIT ? OFFSET ?"
                         ") "
                         "%5 "
                         "ORDER BY %6;")
              .arg(nameExpr, detail::artistVisibilityWhereSql(QStringLiteral("ar.id")), filterSql,
                  pageOrderClause,
                  detail::artistPageSelectSql(QStringLiteral("page p"), QStringLiteral("p")),
                  outerOrderClause);

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
        rows.append(detail::parseArtistRow(q));
    }

    return rows;
}

core::Result<QList<ArtistRow>> LibraryQuery::artistsByIds(
    const QList<qint64> &ids, ArtistNamePreference pref) const
{
    return detail::fetchRowsByIds<ArtistRow>(m_db, ids,
        QStringLiteral("%1 WHERE ar.id IN (%2) AND %3")
            .arg(QStringLiteral("%1"), QStringLiteral("%2"),
                detail::artistVisibilityWhereSql(QStringLiteral("ar.id"))),
        detail::artistSelectSql(QStringLiteral("artists ar"), QStringLiteral("ar"), pref),
        detail::parseArtistRow, QStringLiteral("artistsByIds"));
}

core::Result<std::optional<ArtistRow>> LibraryQuery::artist(
    qint64 artistId, ArtistNamePreference pref) const
{
    const auto rowsRes = artistsByIds({ artistId }, pref);
    if (!rowsRes.ok()) {
        return rowsRes.error();
    }
    const auto &rows = rowsRes.value();
    if (rows.isEmpty()) {
        return std::optional<ArtistRow> { std::nullopt };
    }
    return std::make_optional(rows.first());
}

} // namespace linernotes::library
