// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DuplicateSource.h"

#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>
#include <library/FingerprintStore.h>

#include <algorithm>

namespace linernotes::butler {

namespace {

KeepFactors parseKeepFactors(const QSqlQuery &q)
{
    KeepFactors factors;
    factors.codec = q.value(6).toString();
    factors.sampleRate = q.value(7).toInt();
    factors.bitDepth = q.value(8).toInt();
    factors.bitrate = q.value(9).toInt();

    int filled = 0;
    if (!q.value(10).toString().trimmed().isEmpty()) {
        ++filled;
    }
    if (!q.value(11).toString().trimmed().isEmpty()) {
        ++filled;
    }
    if (!q.value(12).toString().trimmed().isEmpty()) {
        ++filled;
    }
    if (!q.value(13).toString().trimmed().isEmpty()) {
        ++filled;
    }
    if (!q.value(14).isNull()) {
        ++filled;
    }
    if (!q.value(15).isNull()) {
        ++filled;
    }
    if (!q.value(16).toString().trimmed().isEmpty()) {
        ++filled;
    }
    factors.filledTagFields = filled;
    factors.hasCover = (q.value(17).toInt() != 0);
    return factors;
}

} // namespace

DuplicateSource::DuplicateSource(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<QList<DupTrack>> DuplicateSource::loadTracks() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const library::FingerprintStore fpStore(m_db, m_clock);
    const auto fpRes = fpStore.loadAll();
    if (!fpRes.ok()) {
        return fpRes.error();
    }

    QHash<qint64, audio::RawFingerprint> fpMap;
    for (const auto &sfp : fpRes.value()) {
        fpMap.insert(sfp.fileId,
            audio::RawFingerprint {
                .algorithm = sfp.algorithm,
                .items = sfp.items,
            });
    }

    QSqlQuery q(conn);
    const QString sql = QStringLiteral(
        "SELECT "
        "  t.id, "
        "  t.file_id, "
        "  COALESCE(t.work_id, 0), "
        "  COALESCE(tv.version_type, ''), "
        "  COALESCE(f.duration_ms, 0), "
        "  COALESCE(f.content_hash, ''), "
        "  COALESCE(f.codec, ''), "
        "  COALESCE(f.sample_rate, 0), "
        "  COALESCE(f.bit_depth, 0), "
        "  COALESCE(f.bitrate, 0), "
        "  em.title, "
        "  em.artist, "
        "  em.album, "
        "  em.album_artist, "
        "  em.year, "
        "  em.track_number, "
        "  em.genre, "
        "  (CASE WHEN f.cover_id IS NOT NULL OR al.cover_id IS NOT NULL THEN 1 ELSE 0 END) "
        "FROM tracks t "
        "JOIN files f ON f.id = t.file_id "
        "LEFT JOIN track_versions tv ON tv.track_id = t.id "
        "LEFT JOIN effective_metadata em ON em.track_id = t.id "
        "LEFT JOIN albums al ON al.id = t.album_id "
        "WHERE f.missing_since IS NULL "
        "  AND t.cue_index IS NULL "
        "ORDER BY t.id ASC;");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("DuplicateSource::loadTracks query failed"),
        };
    }

    QList<DupTrack> results;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const qint64 fileId = q.value(1).toLongLong();
        const qint64 workId = q.value(2).toLongLong();
        const QString versionType = q.value(3).toString();
        const qint64 durationMs = q.value(4).toLongLong();
        const QString contentHash = q.value(5).toString();

        const KeepFactors factors = parseKeepFactors(q);

        DupTrack dt;
        dt.trackId = trackId;
        dt.workId = workId;
        dt.versionType = versionType;
        dt.durationMs = durationMs;
        dt.contentHash = contentHash;
        dt.keepScore = keepScore(factors);

        if (const auto it = fpMap.constFind(fileId); it != fpMap.constEnd()) {
            dt.fingerprint = it.value();
        }

        results.append(dt);
    }

    return results;
}

core::Result<QList<qint64>> DuplicateSource::fingerprintCandidates() const
{
    const auto tracksRes = loadTracks();
    if (!tracksRes.ok()) {
        return tracksRes.error();
    }
    const auto &tracks = tracksRes.value();
    const auto clusters = candidateClusters(tracks);
    if (clusters.isEmpty()) {
        return QList<qint64> { };
    }

    QSet<qint64> clusterTrackIds;
    for (const auto &cluster : clusters) {
        for (const qint64 trackId : cluster) {
            clusterTrackIds.insert(trackId);
        }
    }
    if (clusterTrackIds.isEmpty()) {
        return QList<qint64> { };
    }

    const library::FingerprintStore fpStore(m_db, m_clock);
    const auto pendingRes = fpStore.pendingFileIds();
    if (!pendingRes.ok()) {
        return pendingRes.error();
    }
    const QSet<qint64> pendingFileIds(pendingRes.value().cbegin(), pendingRes.value().cend());
    if (pendingFileIds.isEmpty()) {
        return QList<qint64> { };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    if (!q.exec(QStringLiteral("SELECT id, file_id FROM tracks WHERE cue_index IS NULL;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("Failed to query tracks for file mapping"),
        };
    }

    QHash<qint64, qint64> trackToFile;
    while (q.next()) {
        trackToFile.insert(q.value(0).toLongLong(), q.value(1).toLongLong());
    }

    QSet<qint64> candidateFileSet;
    for (const qint64 trackId : std::as_const(clusterTrackIds)) {
        const qint64 fileId = trackToFile.value(trackId, 0);
        if (fileId > 0 && pendingFileIds.contains(fileId)) {
            candidateFileSet.insert(fileId);
        }
    }

    auto candidateFiles = candidateFileSet.values();
    std::ranges::sort(candidateFiles);
    return candidateFiles;
}

core::Result<void> DuplicateSource::saveGroups(
    const QList<DupGroup> &groups, const QList<DupTrack> &tracks) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QHash<qint64, double> scoreMap;
    for (const auto &t : tracks) {
        scoreMap.insert(t.trackId, t.keepScore);
    }

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for saveGroups"),
            .detail = QString(),
        };
    }

    QSqlQuery delMembers(conn);
    if (!delMembers.exec(QStringLiteral("DELETE FROM duplicate_members;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = delMembers.lastError().text(),
            .detail = QStringLiteral("DELETE FROM duplicate_members"),
        };
    }

    QSqlQuery delGroups(conn);
    if (!delGroups.exec(QStringLiteral("DELETE FROM duplicate_groups;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = delGroups.lastError().text(),
            .detail = QStringLiteral("DELETE FROM duplicate_groups"),
        };
    }

    QSqlQuery insertGroup(conn);
    insertGroup.prepare(
        QStringLiteral("INSERT INTO duplicate_groups (kind, created_at) VALUES (?, ?);"));

    QSqlQuery insertMember(conn);
    insertMember.prepare(QStringLiteral(
        "INSERT INTO duplicate_members (group_id, track_id, keep_score, recommended) "
        "VALUES (?, ?, ?, ?);"));

    const qint64 now = m_clock.nowMs();

    for (const auto &group : groups) {
        const QString kindStr = duplicateKindToString(group.kind);
        insertGroup.bindValue(0, kindStr);
        insertGroup.bindValue(1, now);
        if (!insertGroup.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = insertGroup.lastError().text(),
                .detail = QStringLiteral("INSERT INTO duplicate_groups"),
            };
        }
        const qint64 groupId = insertGroup.lastInsertId().toLongLong();

        for (const qint64 trackId : group.trackIds) {
            const double score = scoreMap.value(trackId, 0.0);
            const int rec = (trackId == group.recommendedTrackId) ? 1 : 0;
            insertMember.bindValue(0, groupId);
            insertMember.bindValue(1, trackId);
            insertMember.bindValue(2, score);
            insertMember.bindValue(3, rec);
            if (!insertMember.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = insertMember.lastError().text(),
                    .detail = QStringLiteral("INSERT INTO duplicate_members"),
                };
            }
        }
    }

    return tx.commit();
}

core::Result<QHash<DuplicateKind, int>> DuplicateSource::countGroups() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QHash<DuplicateKind, int> counts {
        { DuplicateKind::Exact, 0 },
        { DuplicateKind::SameRecording, 0 },
        { DuplicateKind::Suspect, 0 },
    };

    QSqlQuery q(conn);
    if (!q.exec(QStringLiteral("SELECT kind, COUNT(*) FROM duplicate_groups GROUP BY kind;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("DuplicateSource::countGroups query failed"),
        };
    }

    while (q.next()) {
        const QString kindStr = q.value(0).toString();
        const int count = q.value(1).toInt();
        const auto kindOpt = duplicateKindFromString(kindStr);
        if (kindOpt.has_value()) {
            counts.insert(*kindOpt, count);
        }
    }

    return counts;
}

} // namespace linernotes::butler
