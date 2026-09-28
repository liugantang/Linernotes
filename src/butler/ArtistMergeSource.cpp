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

#include <butler/ArtistCredit.h>
#include <butler/ArtistGroup.h>
#include <butler/ArtistMerge.h>
#include <butler/ArtistName.h>
#include <butler/ButlerLogging.h>
#include <butler/Errors.h>
#include <butler/MusicBrainz.h>
#include <core/Clock.h>
#include <core/Logging.h>
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

    QSqlQuery q2(conn);
    if (!q2.exec(
            QStringLiteral("SELECT DISTINCT old_value FROM corrections "
                           "WHERE entity_type = 'track' AND field IN ('artist', 'album_artist') "
                           "  AND status = 'pending' AND old_value IS NOT NULL"))) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = q2.lastError().text(),
            .detail = QString(),
        };
    }

    while (q2.next()) {
        const QString val = q2.value(0).toString().trimmed();
        if (!val.isEmpty()) {
            skipped.names.insert(val);
        }
    }

    return skipped;
}

constexpr qsizetype kConfirmGroupsPerItem = 10;

void logOversizedGroups(const QList<QList<ArtistEntry>> &oversized)
{
    for (const auto &members : oversized) {
        QStringList names;
        names.reserve(members.size());
        for (const auto &m : members) {
            names.append(m.name);
        }
        qCWarning(lcButler) << "Oversized artist group skipped with members:"
                            << names.join(QStringLiteral(", "));
    }
}

QStringList buildGroupItems(const QList<ArtistGroup> &groups)
{
    QStringList items;
    for (const auto &group : groups) {
        if (!group.exactOnly) {
            continue;
        }
        QJsonArray idsArr;
        for (const auto &member : group.members) {
            idsArr.append(member.artistId);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("group"));
        obj.insert(QStringLiteral("ids"), idsArr);
        items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
    return items;
}

QStringList buildConfirmItems(const QList<ArtistGroup> &groups)
{
    QList<ArtistGroup> confirmGroups;
    for (const auto &group : groups) {
        if (!group.exactOnly) {
            confirmGroups.append(group);
        }
    }

    QStringList items;
    for (qsizetype i = 0; i < confirmGroups.size(); i += kConfirmGroupsPerItem) {
        QJsonArray groupsArr;
        const qsizetype end = std::min(i + kConfirmGroupsPerItem, confirmGroups.size());
        for (qsizetype j = i; j < end; ++j) {
            const auto &group = confirmGroups.at(j);
            QJsonArray groupArr;
            for (const auto &m : group.members) {
                groupArr.append(m.artistId);
            }
            groupsArr.append(groupArr);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), QStringLiteral("confirm"));
        obj.insert(QStringLiteral("groups"), groupsArr);
        items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
    return items;
}

QHash<qint64, MbArtist> addMbAltNames(const QSqlDatabase &conn, const QList<ArtistEntry> &entries,
    qint64 nowMs, QHash<QString, QStringList> &altNames)
{
    QHash<qint64, MbArtist> adoptedMbByArtistId;
    for (const auto &entry : entries) {
        const auto cached = cachedArtistSearch(conn, entry.name, nowMs);
        if (!cached.has_value()) {
            continue;
        }
        const auto adopted = adoptMbArtist(entry.name, cached.value());
        if (!adopted.has_value()) {
            continue;
        }
        adoptedMbByArtistId.insert(entry.artistId, adopted.value());

        const QString key = exactKey(entry.name);
        if (!key.isEmpty()) {
            auto it = altNames.find(key);
            if (it == altNames.end()) {
                it = altNames.insert(key, { });
            }

            const QString &mbName = adopted->name.trimmed();
            if (!mbName.isEmpty() && !it.value().contains(mbName)) {
                it.value().append(mbName);
            }
            for (const auto &alias : adopted->aliases) {
                const QString trimmed = alias.name.trimmed();
                if (!trimmed.isEmpty() && !it.value().contains(trimmed)) {
                    it.value().append(trimmed);
                }
            }
        }
    }
    return adoptedMbByArtistId;
}

QStringList buildMbAliasItems(const QList<ArtistEntry> &entries, const QList<ArtistGroup> &groups,
    const QHash<qint64, MbArtist> &adoptedMbByArtistId)
{
    QSet<qint64> groupedArtistIds;
    for (const auto &group : groups) {
        for (const auto &member : group.members) {
            groupedArtistIds.insert(member.artistId);
        }
    }

    QStringList items;
    for (const auto &entry : entries) {
        if (adoptedMbByArtistId.contains(entry.artistId)
            && !groupedArtistIds.contains(entry.artistId)) {
            QJsonObject obj;
            obj.insert(QStringLiteral("type"), QStringLiteral("mb_alias"));
            obj.insert(QStringLiteral("id"), entry.artistId);
            items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        }
    }
    return items;
}

} // namespace

ArtistMergeSource::ArtistMergeSource(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
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

core::Result<QHash<QString, QStringList>> ArtistMergeSource::loadAltNames() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    if (!q.exec(QStringLiteral("SELECT result FROM artist_credits"))) {
        return core::Error {
            .code = QString(errc::kArtistMergeInvalidResult),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QHash<QString, QStringList> altNames;
    while (q.next()) {
        const QString jsonStr = q.value(0).toString();
        QJsonParseError parseErr { };
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
        if (doc.isNull() || !doc.isObject()) {
            continue;
        }

        const auto creditOpt = artistCreditFromJson(doc.object());
        if (!creditOpt.has_value()) {
            continue;
        }

        for (const auto &performer : creditOpt->performers) {
            const QString key = exactKey(performer.name);
            if (key.isEmpty() || performer.aka.isEmpty()) {
                continue;
            }
            auto it = altNames.find(key);
            if (it == altNames.end()) {
                it = altNames.insert(key, { });
            }
            for (const auto &akaName : performer.aka) {
                const QString trimmed = akaName.trimmed();
                if (!trimmed.isEmpty() && !it.value().contains(trimmed)) {
                    it.value().append(trimmed);
                }
            }
        }
    }

    return altNames;
}

core::Result<QStringList> ArtistMergeSource::findLookupItems() const
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

    const qint64 nowMs = m_clock.nowMs();

    QStringList items;
    for (const auto &entry : allEntries) {
        if (skipped.ids.contains(entry.artistId) || skipped.names.contains(entry.name)) {
            continue;
        }

        const auto cached = cachedArtistSearch(conn, entry.name, nowMs);
        if (cached.has_value()) {
            continue;
        }

        QJsonObject obj;
        obj.insert(QStringLiteral("name"), entry.name);
        items.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }

    return items;
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

    auto altNamesRes = loadAltNames();
    if (!altNamesRes.ok()) {
        return altNamesRes.error();
    }
    auto altNames = altNamesRes.value();

    const qint64 nowMs = m_clock.nowMs();

    QHash<qint64, MbArtist> adoptedMbByArtistId;
    if (useMusicBrainz) {
        adoptedMbByArtistId = addMbAltNames(conn, activeEntries, nowMs, altNames);
    }

    constexpr int kMaxGroupSize = 12;
    const auto grouping = groupArtists(activeEntries, altNames, kMaxGroupSize);

    logOversizedGroups(grouping.oversized);

    QStringList items;
    items.append(buildGroupItems(grouping.groups));
    items.append(buildConfirmItems(grouping.groups));

    if (useMusicBrainz) {
        items.append(buildMbAliasItems(activeEntries, grouping.groups, adoptedMbByArtistId));
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
