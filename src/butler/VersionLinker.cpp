// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "VersionLinker.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <butler/ArtistName.h>
#include <butler/TitleMatchStore.h>
#include <butler/TitleVersion.h>
#include <butler/VersionSuffixStore.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/LibraryEnums.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace linernotes::butler {

namespace {

struct RawTrack {
    qint64 trackId = 0;
    QString title;
    std::optional<qint64> mainArtistId = std::nullopt;
};

struct ProcessedTrack {
    qint64 trackId = 0;
    QString baseTitle;
    library::VersionType type = library::VersionType::Studio;
    bool unresolved = false;
    QString groupingKey;
    std::optional<qint64> mainArtistId = std::nullopt;
    QString baseKey;
};

core::Result<QList<RawTrack>> loadAllTracks(const QSqlDatabase &conn)
{
    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT t.id, em.title, ta.artist_id "
                                       "FROM tracks t "
                                       "LEFT JOIN effective_metadata em ON em.track_id = t.id "
                                       "LEFT JOIN track_artists ta ON ta.track_id = t.id AND "
                                       "ta.role = 'artist' AND ta.position = 0 "
                                       "ORDER BY t.id;");
    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to load tracks for version linking"),
            .detail = q.lastError().text(),
        };
    }
    QList<RawTrack> tracks;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        if (!tracks.isEmpty() && tracks.last().trackId == trackId) {
            continue;
        }
        RawTrack t;
        t.trackId = trackId;
        if (!q.value(1).isNull()) {
            t.title = q.value(1).toString();
        }
        if (!q.value(2).isNull()) {
            t.mainArtistId = q.value(2).toLongLong();
        }
        tracks.append(t);
    }
    return tracks;
}

void processTracks(const QList<RawTrack> &rawTracks,
    const std::function<SuffixClass(const QString &)> &classify, QList<ProcessedTrack> &validTracks,
    QList<qint64> &emptyTrackIds)
{
    for (const auto &raw : rawTracks) {
        const QString trimmedTitle = raw.title.trimmed();
        if (trimmedTitle.isEmpty()) {
            emptyTrackIds.append(raw.trackId);
            continue;
        }

        const TitleVersion tv = resolveTitle(trimmedTitle, classify);
        const QString baseKey = exactKey(tv.baseTitle);

        QString groupingKey;
        if (raw.mainArtistId.has_value() && !baseKey.isEmpty()) {
            groupingKey = baseKey + QLatin1Char('\x1f') + QString::number(raw.mainArtistId.value());
        } else {
            groupingKey = QStringLiteral("t%1").arg(raw.trackId);
        }

        validTracks.append(ProcessedTrack {
            .trackId = raw.trackId,
            .baseTitle = tv.baseTitle,
            .type = tv.type,
            .unresolved = tv.unresolved,
            .groupingKey = groupingKey,
            .mainArtistId = raw.mainArtistId,
            .baseKey = baseKey,
        });
    }
}
class DisjointSet {
public:
    void add(const QString &x)
    {
        if (!m_parent.contains(x)) {
            m_parent.insert(x, x);
        }
    }

    QString find(const QString &x)
    {
        add(x);
        QString root = x;
        while (m_parent.value(root, root) != root) {
            root = m_parent.value(root, root);
        }
        QString curr = x;
        while (curr != root) {
            const QString next = m_parent.value(curr, curr);
            m_parent.insert(curr, root);
            curr = next;
        }
        return root;
    }

    void unite(const QString &a, const QString &b)
    {
        const QString rootA = find(a);
        const QString rootB = find(b);
        if (rootA != rootB) {
            m_parent.insert(rootA, rootB);
        }
    }

    bool contains(const QString &x) const { return m_parent.contains(x); }

    QList<QString> keys() const { return m_parent.keys(); }

private:
    QHash<QString, QString> m_parent;
};

QHash<QString, QStringList> buildTitleMatchMap(
    const QHash<QPair<QString, QString>, TitleMatchVerdict> &titleMatchVerdicts)
{
    QHash<QString, QStringList> matches;
    for (auto it = titleMatchVerdicts.cbegin(); it != titleMatchVerdicts.cend(); ++it) {
        if (it.value().same && it.value().confidence >= kTitleMatchMinConfidence) {
            const QString &keyA = it.key().first;
            const QString &keyB = it.key().second;
            auto matchIt = matches.find(keyA);
            if (matchIt == matches.end()) {
                matches.insert(keyA, QStringList { keyB });
            } else {
                matchIt.value().append(keyB);
            }
        }
    }
    return matches;
}

QHash<qint64, QSet<QString>> collectArtistBaseKeys(const QList<ProcessedTrack> &tracks)
{
    QHash<qint64, QSet<QString>> artistBaseKeys;
    for (const auto &t : tracks) {
        if (!t.mainArtistId.has_value() || t.baseKey.isEmpty()) {
            continue;
        }
        const qint64 artistId = t.mainArtistId.value();
        auto it = artistBaseKeys.find(artistId);
        if (it == artistBaseKeys.end()) {
            artistBaseKeys.insert(artistId, QSet<QString> { t.baseKey });
        } else {
            it.value().insert(t.baseKey);
        }
    }
    return artistBaseKeys;
}

void connectMatchingGroups(DisjointSet &dsu, const QHash<qint64, QSet<QString>> &artistBaseKeys,
    const QHash<QString, QStringList> &titleMatches)
{
    for (auto it = artistBaseKeys.cbegin(); it != artistBaseKeys.cend(); ++it) {
        const qint64 artistId = it.key();
        const QSet<QString> &baseKeys = it.value();

        for (const auto &baseKey : baseKeys) {
            const auto matchIt = titleMatches.constFind(baseKey);
            if (matchIt == titleMatches.constEnd()) {
                continue;
            }
            const QString g1 = baseKey + QLatin1Char('\x1f') + QString::number(artistId);
            for (const auto &partner : matchIt.value()) {
                if (baseKeys.contains(partner)) {
                    const QString g2 = partner + QLatin1Char('\x1f') + QString::number(artistId);
                    dsu.unite(g1, g2);
                }
            }
        }
    }
}

QHash<QString, QString> computeMinKeyPerRoot(DisjointSet &dsu)
{
    QHash<QString, QString> minKeyPerRoot;
    const auto allKeys = dsu.keys();
    for (const auto &g : allKeys) {
        const QString root = dsu.find(g);
        const auto it = minKeyPerRoot.find(root);
        if (it == minKeyPerRoot.end() || g < it.value()) {
            minKeyPerRoot.insert(root, g);
        }
    }
    return minKeyPerRoot;
}

void updateTrackGroupingKeys(
    QList<ProcessedTrack> &tracks, DisjointSet &dsu, const QHash<QString, QString> &minKeyPerRoot)
{
    for (auto &t : tracks) {
        if (t.mainArtistId.has_value() && !t.baseKey.isEmpty()) {
            if (dsu.contains(t.groupingKey)) {
                const QString root = dsu.find(t.groupingKey);
                const auto it = minKeyPerRoot.constFind(root);
                if (it != minKeyPerRoot.constEnd()) {
                    t.groupingKey = it.value();
                }
            }
        }
    }
}

void applyTitleMatches(QList<ProcessedTrack> &tracks,
    const QHash<QPair<QString, QString>, TitleMatchVerdict> &titleMatchVerdicts)
{
    if (titleMatchVerdicts.isEmpty()) {
        return;
    }

    const auto titleMatches = buildTitleMatchMap(titleMatchVerdicts);
    if (titleMatches.isEmpty()) {
        return;
    }

    const auto artistBaseKeys = collectArtistBaseKeys(tracks);
    DisjointSet dsu;
    connectMatchingGroups(dsu, artistBaseKeys, titleMatches);

    const auto minKeyPerRoot = computeMinKeyPerRoot(dsu);
    updateTrackGroupingKeys(tracks, dsu, minKeyPerRoot);
}

core::Result<void> saveTrackVersionsAndCleanEmpty(const QSqlDatabase &conn,
    const QList<ProcessedTrack> &validTracks, const QList<qint64> &emptyTrackIds, qint64 nowMs)
{
    QSqlQuery insertTv(conn);
    insertTv.prepare(QStringLiteral("INSERT OR REPLACE INTO track_versions "
                                    "(track_id, base_title, version_type, unresolved, updated_at) "
                                    "VALUES (?, ?, ?, ?, ?);"));

    for (const auto &t : validTracks) {
        insertTv.bindValue(0, t.trackId);
        insertTv.bindValue(1, t.baseTitle);
        insertTv.bindValue(2, library::versionTypeToString(t.type));
        insertTv.bindValue(3, t.unresolved ? 1 : 0);
        insertTv.bindValue(4, nowMs);
        if (!insertTv.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = QStringLiteral("Failed to insert into track_versions"),
                .detail = insertTv.lastError().text(),
            };
        }
    }

    if (!emptyTrackIds.isEmpty()) {
        QSqlQuery deleteTv(conn);
        deleteTv.prepare(QStringLiteral("DELETE FROM track_versions WHERE track_id = ?;"));
        for (const qint64 trackId : emptyTrackIds) {
            deleteTv.bindValue(0, trackId);
            if (!deleteTv.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = QStringLiteral("Failed to delete from track_versions"),
                    .detail = deleteTv.lastError().text(),
                };
            }
        }
    }
    return { };
}

QString determineWorkTitle(const QList<ProcessedTrack> &tracks)
{
    const ProcessedTrack *bestStudio = nullptr;
    const ProcessedTrack *bestAny = nullptr;
    for (const auto &t : tracks) {
        if (bestAny == nullptr || t.trackId < bestAny->trackId) {
            bestAny = &t;
        }
        if (t.type == library::VersionType::Studio) {
            if (bestStudio == nullptr || t.trackId < bestStudio->trackId) {
                bestStudio = &t;
            }
        }
    }
    if (bestStudio != nullptr) {
        return bestStudio->baseTitle;
    }
    if (bestAny != nullptr) {
        return bestAny->baseTitle;
    }
    return { };
}

core::Result<QHash<QString, qint64>> syncWorks(
    const QSqlDatabase &conn, const QHash<QString, QList<ProcessedTrack>> &groups, qint64 nowMs)
{
    QSqlQuery loadWorks(conn);
    if (!loadWorks.exec(QStringLiteral(
            "SELECT id, grouping_key, title FROM works WHERE grouping_key IS NOT NULL;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to load existing works"),
            .detail = loadWorks.lastError().text(),
        };
    }
    struct ExistingWork {
        qint64 id = 0;
        QString title;
    };
    QHash<QString, ExistingWork> existingWorks;
    while (loadWorks.next()) {
        const qint64 id = loadWorks.value(0).toLongLong();
        const QString key = loadWorks.value(1).toString();
        const QString title = loadWorks.value(2).toString();
        existingWorks.insert(key, ExistingWork { .id = id, .title = title });
    }

    QSqlQuery updateWork(conn);
    updateWork.prepare(QStringLiteral("UPDATE works SET title = ? WHERE id = ?;"));

    QSqlQuery insertWork(conn);
    insertWork.prepare(
        QStringLiteral("INSERT INTO works (grouping_key, title, created_at) VALUES (?, ?, ?);"));

    QHash<QString, qint64> groupWorkIds;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const QString &key = it.key();
        const QString desiredTitle = determineWorkTitle(it.value());

        const auto exIt = existingWorks.constFind(key);
        if (exIt != existingWorks.constEnd()) {
            const qint64 workId = exIt.value().id;
            groupWorkIds.insert(key, workId);
            if (exIt.value().title != desiredTitle) {
                updateWork.bindValue(0, desiredTitle);
                updateWork.bindValue(1, workId);
                if (!updateWork.exec()) {
                    return core::Error {
                        .code = QString(library::errc::kDbQuery),
                        .message = QStringLiteral("Failed to update work title"),
                        .detail = updateWork.lastError().text(),
                    };
                }
            }
        } else {
            insertWork.bindValue(0, key);
            insertWork.bindValue(1, desiredTitle);
            insertWork.bindValue(2, nowMs);
            if (!insertWork.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = QStringLiteral("Failed to insert work"),
                    .detail = insertWork.lastError().text(),
                };
            }
            const qint64 workId = insertWork.lastInsertId().toLongLong();
            groupWorkIds.insert(key, workId);
        }
    }
    return groupWorkIds;
}

core::Result<void> updateTrackWorkIds(const QSqlDatabase &conn,
    const QHash<QString, QList<ProcessedTrack>> &groups, const QHash<QString, qint64> &groupWorkIds,
    const QList<qint64> &emptyTrackIds)
{
    QSqlQuery updateTrack(conn);
    updateTrack.prepare(QStringLiteral("UPDATE tracks SET work_id = ? WHERE id = ?;"));

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const auto idIt = groupWorkIds.constFind(it.key());
        if (idIt == groupWorkIds.constEnd()) {
            continue;
        }
        const qint64 workId = idIt.value();
        for (const auto &track : it.value()) {
            updateTrack.bindValue(0, workId);
            updateTrack.bindValue(1, track.trackId);
            if (!updateTrack.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = QStringLiteral("Failed to update track work_id"),
                    .detail = updateTrack.lastError().text(),
                };
            }
        }
    }

    if (!emptyTrackIds.isEmpty()) {
        QSqlQuery clearTrack(conn);
        clearTrack.prepare(QStringLiteral("UPDATE tracks SET work_id = NULL WHERE id = ?;"));
        for (const qint64 trackId : emptyTrackIds) {
            clearTrack.bindValue(0, trackId);
            if (!clearTrack.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = QStringLiteral("Failed to clear track work_id"),
                    .detail = clearTrack.lastError().text(),
                };
            }
        }
    }
    return { };
}

core::Result<int> deleteOrphanWorksAndCount(const QSqlDatabase &conn)
{
    QSqlQuery delWorks(conn);
    if (!delWorks.exec(QStringLiteral("DELETE FROM works WHERE id NOT IN (SELECT DISTINCT work_id "
                                      "FROM tracks WHERE work_id IS NOT NULL);"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to delete orphan works"),
            .detail = delWorks.lastError().text(),
        };
    }

    QSqlQuery countWorks(conn);
    if (!countWorks.exec(QStringLiteral("SELECT COUNT(*) FROM works;")) || !countWorks.next()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to count works"),
            .detail = countWorks.lastError().text(),
        };
    }
    return countWorks.value(0).toInt();
}

} // namespace

VersionLinker::VersionLinker(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<VersionLinkStats> VersionLinker::linkAll(
    int suffixPromptVersion, int titleMatchPromptVersion) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for version linking"),
            .detail = QString(),
        };
    }

    const VersionSuffixStore suffixStore(m_db, m_clock);
    const auto storeRes = suffixStore.loadAll(suffixPromptVersion);
    if (!storeRes.ok()) {
        return storeRes.error();
    }
    const auto &verdicts = storeRes.value();

    const TitleMatchStore titleMatchStore(m_db, m_clock);
    const auto titleMatchRes = titleMatchStore.loadAll(titleMatchPromptVersion);
    if (!titleMatchRes.ok()) {
        return titleMatchRes.error();
    }
    const auto &titleMatchVerdicts = titleMatchRes.value();

    auto classify = [&verdicts](const QString &s) -> SuffixClass {
        SuffixClass cls = classifySuffix(s);
        if (cls.role != SuffixRole::Unknown) {
            return cls;
        }
        const QString key = suffixKey(s);
        const auto it = verdicts.constFind(key);
        if (it != verdicts.constEnd()) {
            return it.value().cls;
        }
        return SuffixClass { .role = SuffixRole::Unknown, .type = library::VersionType::Studio };
    };

    const auto rawTracksRes = loadAllTracks(conn);
    if (!rawTracksRes.ok()) {
        return rawTracksRes.error();
    }

    QList<ProcessedTrack> validTracks;
    QList<qint64> emptyTrackIds;
    processTracks(rawTracksRes.value(), classify, validTracks, emptyTrackIds);
    applyTitleMatches(validTracks, titleMatchVerdicts);

    const qint64 nowMs = m_clock.nowMs();

    const auto saveTvRes = saveTrackVersionsAndCleanEmpty(conn, validTracks, emptyTrackIds, nowMs);
    if (!saveTvRes.ok()) {
        return saveTvRes.error();
    }

    QHash<QString, QList<ProcessedTrack>> groups;
    int unresolvedCount = 0;
    for (const auto &t : validTracks) {
        if (t.unresolved) {
            ++unresolvedCount;
        }
        auto it = groups.find(t.groupingKey);
        if (it != groups.end()) {
            it.value().append(t);
        } else {
            groups.insert(t.groupingKey, { t });
        }
    }

    const auto syncWorksRes = syncWorks(conn, groups, nowMs);
    if (!syncWorksRes.ok()) {
        return syncWorksRes.error();
    }

    const auto updateTracksRes
        = updateTrackWorkIds(conn, groups, syncWorksRes.value(), emptyTrackIds);
    if (!updateTracksRes.ok()) {
        return updateTracksRes.error();
    }

    const auto countWorksRes = deleteOrphanWorksAndCount(conn);
    if (!countWorksRes.ok()) {
        return countWorksRes.error();
    }

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        return commitRes.error();
    }

    VersionLinkStats stats;
    stats.tracks = static_cast<int>(validTracks.size());
    stats.works = countWorksRes.value();
    stats.unresolved = unresolvedCount;

    return stats;
}

core::Result<int> VersionLinker::countPending() const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    const QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM tracks t "
        "JOIN effective_metadata em ON em.track_id = t.id "
        "LEFT JOIN track_versions tv ON tv.track_id = t.id "
        "WHERE TRIM(em.title) != '' AND (tv.track_id IS NULL OR tv.unresolved = 1);");

    if (!q.exec(sql) || !q.next()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to count pending version links"),
            .detail = q.lastError().text(),
        };
    }
    return q.value(0).toInt();
}

} // namespace linernotes::butler
