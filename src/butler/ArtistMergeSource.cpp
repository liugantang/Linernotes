// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMergeSource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <butler/ArtistName.h>
#include <butler/Errors.h>
#include <library/Database.h>

#include <algorithm>

namespace linernotes::butler {

namespace {

struct SkippedArtists {
    QSet<qint64> ids;
    QSet<QString> names;
};

core::Result<SkippedArtists> fetchSkippedArtists(const QSqlDatabase &conn)
{
    SkippedArtists skipped;

    QSqlQuery q(conn);
    if (!q.exec(QStringLiteral(
            "SELECT entity_id, new_value FROM corrections "
            "WHERE entity_type = 'artist' AND field = 'alias' AND status = 'pending'"))) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        const QString val = q.value(1).toString().trimmed();
        if (id > 0) {
            skipped.ids.insert(id);
        }
        if (!val.isEmpty()) {
            skipped.names.insert(val);
        }
    }

    return skipped;
}

} // namespace

ArtistMergeSource::ArtistMergeSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<ArtistEntry>> ArtistMergeSource::loadArtists() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    const QString sql
        = QStringLiteral("SELECT a.id, a.name, COUNT(DISTINCT ta.track_id) AS track_count "
                         "FROM artists a "
                         "LEFT JOIN track_artists ta ON ta.artist_id = a.id "
                         "WHERE a.name NOT IN ("
                         "  SELECT old_value FROM corrections "
                         "  WHERE status = 'pending' "
                         "    AND entity_type = 'track' "
                         "    AND field IN ('artist', 'album_artist') "
                         "    AND old_value IS NOT NULL"
                         ") "
                         "GROUP BY a.id, a.name "
                         "ORDER BY a.id ASC");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<ArtistEntry> entries;
    while (q.next()) {
        entries.append(ArtistEntry {
            .artistId = q.value(0).toLongLong(),
            .name = q.value(1).toString(),
            .trackCount = q.value(2).toInt(),
        });
    }

    return entries;
}

core::Result<QStringList> ArtistMergeSource::findItems(bool useMusicBrainz) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    auto artistsRes = loadArtists();
    if (!artistsRes.ok()) {
        return artistsRes.error();
    }
    const auto &allEntries = artistsRes.value();

    auto skippedRes = fetchSkippedArtists(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }
    const auto &skipped = skippedRes.value();

    QList<ArtistEntry> activeEntries;
    activeEntries.reserve(allEntries.size());
    for (const auto &entry : allEntries) {
        if (skipped.ids.contains(entry.artistId) || skipped.names.contains(entry.name)) {
            continue;
        }
        activeEntries.append(entry);
    }

    const auto clustering = clusterArtists(activeEntries);

    QStringList items;

    // 1. Cluster items
    for (const auto &cluster : clustering.clusters) {
        QJsonArray idsArr;
        for (const auto &member : cluster.members) {
            idsArr.append(member.artistId);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("cluster"));
        obj.insert(QStringLiteral("ids"), idsArr);
        items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }

    // 2. Fuzzy items (groups of 10 pairs)
    for (qsizetype i = 0; i < clustering.fuzzyCandidates.size(); i += 10) {
        QJsonArray pairsArr;
        const qsizetype end = std::min(i + 10, clustering.fuzzyCandidates.size());
        for (qsizetype j = i; j < end; ++j) {
            const auto &pair = clustering.fuzzyCandidates.at(j);
            QJsonArray pairArr;
            pairArr.append(pair.a);
            pairArr.append(pair.b);
            pairsArr.append(pairArr);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("fuzzy"));
        obj.insert(QStringLiteral("pairs"), pairsArr);
        items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }

    // 3. MusicBrainz items (CJK artists)
    if (useMusicBrainz) {
        for (const auto &entry : activeEntries) {
            if (containsCjk(entry.name)) {
                QJsonObject obj;
                obj.insert(QStringLiteral("type"), QStringLiteral("mb"));
                obj.insert(QStringLiteral("id"), entry.artistId);
                items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
            }
        }
    }

    return items;
}

core::Result<QStringList> ArtistMergeSource::sampleAlbums(qint64 artistId, int limit) const
{
    if (limit <= 0) {
        return QStringList { };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT al.title, COUNT(DISTINCT t.id) AS track_count "
                             "FROM tracks t "
                             "JOIN track_artists ta ON ta.track_id = t.id "
                             "JOIN albums al ON al.id = t.album_id "
                             "WHERE ta.artist_id = ? AND al.title IS NOT NULL AND al.title != '' "
                             "GROUP BY al.title "
                             "ORDER BY track_count DESC, al.title ASC "
                             "LIMIT ?"));
    q.addBindValue(artistId);
    q.addBindValue(limit);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = q.lastError().text(),
            .detail = QString::number(artistId),
        };
    }

    QStringList albums;
    while (q.next()) {
        albums.append(q.value(0).toString());
    }
    return albums;
}

} // namespace linernotes::butler
