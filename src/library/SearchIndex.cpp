// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SearchIndex.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <library/Errors.h>
#include <library/LibraryLogging.h>
#include <library/SearchText.h>

namespace linernotes::library {

SearchIndex::SearchIndex(const QSqlDatabase &db)
    : m_db(db)
{
}

core::Result<int> SearchIndex::flushDirty()
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    QSqlQuery qDirty(m_db);
    const QString selectSql = QStringLiteral(
        "SELECT d.track_id, em.title, em.artist, em.album, em.album_artist, em.composer, "
        "(SELECT group_concat(aa.alias, ' ') FROM track_artists ta "
        " JOIN artist_aliases aa ON ta.artist_id = aa.artist_id "
        " WHERE ta.track_id = d.track_id) AS aliases "
        "FROM search_dirty d "
        "LEFT JOIN effective_metadata em ON d.track_id = em.track_id;");

    if (!qDirty.exec(selectSql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = qDirty.lastError().text(),
            .detail = selectSql,
        };
    }

    struct DirtyRecord {
        qint64 trackId = 0;
        bool hasEffective = false;
        QString title;
        QString artist;
        QString album;
        QString albumArtist;
        QString composer;
        QString aliases;
    };

    QList<DirtyRecord> records;
    while (qDirty.next()) {
        DirtyRecord rec;
        rec.trackId = qDirty.value(0).toLongLong();
        rec.hasEffective = !qDirty.value(1).isNull() || !qDirty.value(2).isNull()
            || !qDirty.value(3).isNull() || !qDirty.value(4).isNull() || !qDirty.value(5).isNull();
        if (rec.hasEffective) {
            rec.title = qDirty.value(1).toString();
            rec.artist = qDirty.value(2).toString();
            rec.album = qDirty.value(3).toString();
            rec.albumArtist = qDirty.value(4).toString();
            rec.composer = qDirty.value(5).toString();
            rec.aliases = qDirty.value(6).toString();
        }
        records.append(rec);
    }

    if (records.isEmpty()) {
        return 0;
    }

    QSqlQuery qDelete(m_db);
    qDelete.prepare(QStringLiteral("DELETE FROM search_index WHERE rowid = ?;"));

    QSqlQuery qInsert(m_db);
    qInsert.prepare(QStringLiteral(
        "INSERT INTO search_index(rowid, title, artist, album, album_artist, composer, aliases, "
        "romanized) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"));

    for (const auto &rec : records) {
        qDelete.bindValue(0, rec.trackId);
        if (!qDelete.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = qDelete.lastError().text(),
                .detail
                = QStringLiteral("DELETE FROM search_index WHERE rowid = %1").arg(rec.trackId),
            };
        }

        if (rec.hasEffective) {
            const QString indexedTitle = search::indexText(rec.title);
            const QString indexedArtist = search::indexText(rec.artist);
            const QString indexedAlbum = search::indexText(rec.album);
            const QString indexedAlbumArtist = search::indexText(rec.albumArtist);
            const QString indexedComposer = search::indexText(rec.composer);
            const QString indexedAliases = search::indexText(rec.aliases);

            QStringList romanizedParts;
            auto addRomanized = [&](const QString &str) {
                if (!str.isEmpty()) {
                    const QString r = search::romanized(str);
                    if (!r.isEmpty()) {
                        romanizedParts.append(r);
                    }
                }
            };
            addRomanized(rec.title);
            addRomanized(rec.artist);
            addRomanized(rec.album);
            addRomanized(rec.albumArtist);
            addRomanized(rec.composer);
            addRomanized(rec.aliases);

            const QString romanizedCol = romanizedParts.join(u' ');

            qInsert.bindValue(0, rec.trackId);
            qInsert.bindValue(1, indexedTitle);
            qInsert.bindValue(2, indexedArtist);
            qInsert.bindValue(3, indexedAlbum);
            qInsert.bindValue(4, indexedAlbumArtist);
            qInsert.bindValue(5, indexedComposer);
            qInsert.bindValue(6, indexedAliases);
            qInsert.bindValue(7, romanizedCol);

            if (!qInsert.exec()) {
                return core::Error {
                    .code = QString(errc::kDbQuery),
                    .message = qInsert.lastError().text(),
                    .detail
                    = QStringLiteral("INSERT INTO search_index rowid = %1").arg(rec.trackId),
                };
            }
        }
    }

    QSqlQuery qClean(m_db);
    if (!qClean.exec(QStringLiteral("DELETE FROM search_dirty;"))) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = qClean.lastError().text(),
            .detail = QStringLiteral("DELETE FROM search_dirty"),
        };
    }

    return static_cast<int>(records.size());
}

core::Result<QList<SearchHit>> SearchIndex::search(const QString &userInput, int limit) const
{
    if (!m_db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    const QString matchExpr = search::buildMatchQuery(userInput);
    if (matchExpr.isEmpty() || limit <= 0) {
        return QList<SearchHit> { };
    }

    // bm25 weights: title 10, artist 6, album 4, album_artist 5, composer 2, aliases 4, romanized 3
    // Note column order in CREATE VIRTUAL TABLE search_index USING fts5:
    // 0: title, 1: artist, 2: album, 3: album_artist, 4: composer, 5: aliases, 6: romanized
    const QString sql = QStringLiteral(
        "SELECT rowid, bm25(search_index, 10.0, 6.0, 4.0, 5.0, 2.0, 4.0, 3.0) AS score "
        "FROM search_index "
        "WHERE search_index MATCH ? "
        "ORDER BY score ASC "
        "LIMIT ?;");

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.bindValue(0, matchExpr);
    q.bindValue(1, limit);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = matchExpr,
        };
    }

    QList<SearchHit> hits;
    while (q.next()) {
        SearchHit hit;
        hit.trackId = q.value(0).toLongLong();
        hit.rank = q.value(1).toDouble();
        hits.append(hit);
    }

    return hits;
}

} // namespace linernotes::library
