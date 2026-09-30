// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "VersionLinker.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <butler/ArtistName.h>
#include <butler/TitleVersion.h>
#include <butler/VersionSuffixStore.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/LibraryEnums.h>

#include <optional>

namespace linernotes::butler {

namespace {

struct RawTrack {
    qint64 trackId = 0;
    QString title;
    std::optional<qint64> mainArtistId;
};

struct ProcessedTrack {
    qint64 trackId = 0;
    QString baseTitle;
    library::VersionType type = library::VersionType::Studio;
    bool unresolved = false;
    QString groupingKey;
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
        });
    }
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

core::Result<VersionLinkStats> VersionLinker::linkAll(int promptVersion) const
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
    const auto storeRes = suffixStore.loadAll(promptVersion);
    if (!storeRes.ok()) {
        return storeRes.error();
    }
    const auto &verdicts = storeRes.value();

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
