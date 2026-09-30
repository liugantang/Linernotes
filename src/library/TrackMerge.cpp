// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackMerge.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>
#include <optional>

namespace linernotes::library {

namespace {

struct PlayStatsRow {
    int playCount = 0;
    QVariant lastPlayedAt;
    qint64 totalPlayedMs = 0;
    QVariant avgCompletion;
    int skipCount = 0;
};

core::Result<qint64> checkTracksExist(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT id, file_id FROM tracks WHERE id = ? OR id = ?;"));
    q.addBindValue(fromTrackId);
    q.addBindValue(toTrackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("checkTracksExist failed"),
        };
    }

    bool fromFound = false;
    bool toFound = false;
    qint64 fromFileId = 0;

    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        const qint64 fileId = q.value(1).toLongLong();
        if (id == fromTrackId) {
            fromFound = true;
            fromFileId = fileId;
        }
        if (id == toTrackId) {
            toFound = true;
        }
    }

    if (!fromFound || !toFound) {
        return core::Error {
            .code = QString(errc::kTrackNotFound),
            .message = QStringLiteral("Track not found for merge"),
            .detail = QStringLiteral("from found: %1, to found: %2").arg(fromFound).arg(toFound),
        };
    }

    return fromFileId;
}

core::Result<void> mergePlayEventsAndMoments(
    QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    QSqlQuery peQ(conn);
    peQ.prepare(QStringLiteral("UPDATE play_events SET track_id = ? WHERE track_id = ?;"));
    peQ.addBindValue(toTrackId);
    peQ.addBindValue(fromTrackId);
    if (!peQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = peQ.lastError().text(),
            .detail = QStringLiteral("Failed to update play_events"),
        };
    }

    QSqlQuery momQ(conn);
    momQ.prepare(QStringLiteral("UPDATE moments SET track_id = ? WHERE track_id = ?;"));
    momQ.addBindValue(toTrackId);
    momQ.addBindValue(fromTrackId);
    if (!momQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = momQ.lastError().text(),
            .detail = QStringLiteral("Failed to update moments"),
        };
    }

    return { };
}

QVariant computeMergedAvgCompletion(
    const PlayStatsRow &from, const PlayStatsRow &to, int totalPlayCount)
{
    const bool fromHasAvg = !from.avgCompletion.isNull();
    const bool toHasAvg = !to.avgCompletion.isNull();
    if (fromHasAvg && toHasAvg) {
        if (totalPlayCount > 0) {
            const double weighted = ((from.avgCompletion.toDouble() * from.playCount)
                                        + (to.avgCompletion.toDouble() * to.playCount))
                / static_cast<double>(totalPlayCount);
            return { weighted };
        }
        return { (from.avgCompletion.toDouble() + to.avgCompletion.toDouble()) / 2.0 };
    }
    if (fromHasAvg) {
        return from.avgCompletion;
    }
    if (toHasAvg) {
        return to.avgCompletion;
    }
    return { };
}

QVariant computeMergedLastPlayedAt(const PlayStatsRow &from, const PlayStatsRow &to)
{
    const bool fromHasLast = !from.lastPlayedAt.isNull();
    const bool toHasLast = !to.lastPlayedAt.isNull();
    if (fromHasLast && toHasLast) {
        return { std::max(from.lastPlayedAt.toLongLong(), to.lastPlayedAt.toLongLong()) };
    }
    if (fromHasLast) {
        return from.lastPlayedAt;
    }
    if (toHasLast) {
        return to.lastPlayedAt;
    }
    return { };
}

core::Result<void> mergeBothPlayStats(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId,
    const PlayStatsRow &from, const PlayStatsRow &to)
{
    const int mergedPlayCount = from.playCount + to.playCount;
    const qint64 mergedTotalPlayedMs = from.totalPlayedMs + to.totalPlayedMs;
    const int mergedSkipCount = from.skipCount + to.skipCount;
    const QVariant mergedLastPlayedAt = computeMergedLastPlayedAt(from, to);
    const QVariant mergedAvgCompletion = computeMergedAvgCompletion(from, to, mergedPlayCount);

    QSqlQuery updStatsQ(conn);
    updStatsQ.prepare(QStringLiteral(
        "UPDATE track_play_stats SET play_count = ?, last_played_at = ?, total_played_ms = ?, "
        "avg_completion = ?, skip_count = ? WHERE track_id = ?;"));
    updStatsQ.addBindValue(mergedPlayCount);
    updStatsQ.addBindValue(mergedLastPlayedAt);
    updStatsQ.addBindValue(mergedTotalPlayedMs);
    updStatsQ.addBindValue(mergedAvgCompletion);
    updStatsQ.addBindValue(mergedSkipCount);
    updStatsQ.addBindValue(toTrackId);
    if (!updStatsQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = updStatsQ.lastError().text(),
            .detail = QStringLiteral("Failed to update merged track_play_stats"),
        };
    }

    QSqlQuery delStatsQ(conn);
    delStatsQ.prepare(QStringLiteral("DELETE FROM track_play_stats WHERE track_id = ?;"));
    delStatsQ.addBindValue(fromTrackId);
    if (!delStatsQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = delStatsQ.lastError().text(),
            .detail = QStringLiteral("Failed to delete from track_play_stats"),
        };
    }

    return { };
}

core::Result<void> mergePlayStats(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    QSqlQuery statsQ(conn);
    statsQ.prepare(QStringLiteral(
        "SELECT track_id, play_count, last_played_at, total_played_ms, avg_completion, skip_count "
        "FROM track_play_stats WHERE track_id = ? OR track_id = ?;"));
    statsQ.addBindValue(fromTrackId);
    statsQ.addBindValue(toTrackId);
    if (!statsQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = statsQ.lastError().text(),
            .detail = QStringLiteral("Failed to query track_play_stats"),
        };
    }

    std::optional<PlayStatsRow> fromStats;
    std::optional<PlayStatsRow> toStats;
    while (statsQ.next()) {
        const qint64 id = statsQ.value(0).toLongLong();
        PlayStatsRow const row {
            .playCount = statsQ.value(1).toInt(),
            .lastPlayedAt = statsQ.value(2),
            .totalPlayedMs = statsQ.value(3).toLongLong(),
            .avgCompletion = statsQ.value(4),
            .skipCount = statsQ.value(5).toInt(),
        };
        if (id == fromTrackId) {
            fromStats = row;
        } else if (id == toTrackId) {
            toStats = row;
        }
    }

    if (fromStats.has_value() && toStats.has_value()) {
        return mergeBothPlayStats(conn, fromTrackId, toTrackId, *fromStats, *toStats);
    }
    if (fromStats.has_value() && !toStats.has_value()) {
        QSqlQuery moveStatsQ(conn);
        moveStatsQ.prepare(
            QStringLiteral("UPDATE track_play_stats SET track_id = ? WHERE track_id = ?;"));
        moveStatsQ.addBindValue(toTrackId);
        moveStatsQ.addBindValue(fromTrackId);
        if (!moveStatsQ.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = moveStatsQ.lastError().text(),
                .detail = QStringLiteral("Failed to transfer track_play_stats"),
            };
        }
    }

    return { };
}

core::Result<void> mergeFavorites(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    QSqlQuery favQ(conn);
    favQ.prepare(
        QStringLiteral("SELECT entity_id, created_at FROM favorites "
                       "WHERE entity_type = 'track' AND (entity_id = ? OR entity_id = ?);"));
    favQ.addBindValue(fromTrackId);
    favQ.addBindValue(toTrackId);
    if (!favQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = favQ.lastError().text(),
            .detail = QStringLiteral("Failed to query favorites"),
        };
    }

    std::optional<qint64> fromFavCreatedAt;
    bool toIsFavorite = false;
    while (favQ.next()) {
        const qint64 eid = favQ.value(0).toLongLong();
        const qint64 cat = favQ.value(1).toLongLong();
        if (eid == fromTrackId) {
            fromFavCreatedAt = cat;
        }
        if (eid == toTrackId) {
            toIsFavorite = true;
        }
    }

    if (fromFavCreatedAt.has_value() && !toIsFavorite) {
        QSqlQuery insFav(conn);
        insFav.prepare(QStringLiteral(
            "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('track', ?, ?);"));
        insFav.addBindValue(toTrackId);
        insFav.addBindValue(*fromFavCreatedAt);
        if (!insFav.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = insFav.lastError().text(),
                .detail = QStringLiteral("Failed to insert into favorites"),
            };
        }
    }

    return { };
}

core::Result<void> mergeRatings(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    QSqlQuery ratQ(conn);
    ratQ.prepare(
        QStringLiteral("SELECT track_id FROM ratings WHERE track_id = ? OR track_id = ?;"));
    ratQ.addBindValue(fromTrackId);
    ratQ.addBindValue(toTrackId);
    if (!ratQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = ratQ.lastError().text(),
            .detail = QStringLiteral("Failed to query ratings"),
        };
    }

    bool fromHasRating = false;
    bool toHasRating = false;
    while (ratQ.next()) {
        const qint64 id = ratQ.value(0).toLongLong();
        if (id == fromTrackId) {
            fromHasRating = true;
        }
        if (id == toTrackId) {
            toHasRating = true;
        }
    }

    if (fromHasRating && !toHasRating) {
        QSqlQuery updRat(conn);
        updRat.prepare(QStringLiteral("UPDATE ratings SET track_id = ? WHERE track_id = ?;"));
        updRat.addBindValue(toTrackId);
        updRat.addBindValue(fromTrackId);
        if (!updRat.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = updRat.lastError().text(),
                .detail = QStringLiteral("Failed to update rating track_id"),
            };
        }
    }

    return { };
}

core::Result<void> mergePlaylistItems(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    QSqlQuery pliQ(conn);
    pliQ.prepare(QStringLiteral("UPDATE playlist_items SET track_id = ? WHERE track_id = ?;"));
    pliQ.addBindValue(toTrackId);
    pliQ.addBindValue(fromTrackId);
    if (!pliQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = pliQ.lastError().text(),
            .detail = QStringLiteral("Failed to update playlist_items"),
        };
    }
    return { };
}

core::Result<void> deleteTrackAndFile(QSqlDatabase &conn, qint64 fromTrackId, qint64 fromFileId)
{
    QSqlQuery delTrackQ(conn);
    delTrackQ.prepare(QStringLiteral("DELETE FROM tracks WHERE id = ?;"));
    delTrackQ.addBindValue(fromTrackId);
    if (!delTrackQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = delTrackQ.lastError().text(),
            .detail = QStringLiteral("Failed to delete track"),
        };
    }

    QSqlQuery checkFileQ(conn);
    checkFileQ.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE file_id = ?;"));
    checkFileQ.addBindValue(fromFileId);
    if (!checkFileQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = checkFileQ.lastError().text(),
            .detail = QStringLiteral("Failed to check file tracks count"),
        };
    }

    if (checkFileQ.next() && checkFileQ.value(0).toInt() == 0) {
        QSqlQuery delFileQ(conn);
        delFileQ.prepare(QStringLiteral("DELETE FROM files WHERE id = ?;"));
        delFileQ.addBindValue(fromFileId);
        if (!delFileQ.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = delFileQ.lastError().text(),
                .detail = QStringLiteral("Failed to delete file"),
            };
        }
    }

    return { };
}

} // namespace

core::Result<void> mergeTrackInto(QSqlDatabase &conn, qint64 fromTrackId, qint64 toTrackId)
{
    if (fromTrackId <= 0 || toTrackId <= 0 || fromTrackId == toTrackId) {
        return core::Error {
            .code = QString(errc::kTrackMergeInvalid),
            .message = QStringLiteral("Invalid track IDs for merge"),
            .detail = QStringLiteral("from: %1, to: %2").arg(fromTrackId).arg(toTrackId),
        };
    }

    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for mergeTrackInto"),
            .detail = QStringLiteral("from: %1, to: %2").arg(fromTrackId).arg(toTrackId),
        };
    }

    const auto fileRes = checkTracksExist(conn, fromTrackId, toTrackId);
    if (!fileRes.ok()) {
        return fileRes.error();
    }
    const qint64 fromFileId = fileRes.value();

    const auto peRes = mergePlayEventsAndMoments(conn, fromTrackId, toTrackId);
    if (!peRes.ok()) {
        return peRes.error();
    }

    const auto statsRes = mergePlayStats(conn, fromTrackId, toTrackId);
    if (!statsRes.ok()) {
        return statsRes.error();
    }

    const auto favRes = mergeFavorites(conn, fromTrackId, toTrackId);
    if (!favRes.ok()) {
        return favRes.error();
    }

    const auto ratRes = mergeRatings(conn, fromTrackId, toTrackId);
    if (!ratRes.ok()) {
        return ratRes.error();
    }

    const auto pliRes = mergePlaylistItems(conn, fromTrackId, toTrackId);
    if (!pliRes.ok()) {
        return pliRes.error();
    }

    const auto delRes = deleteTrackAndFile(conn, fromTrackId, fromFileId);
    if (!delRes.ok()) {
        return delRes.error();
    }

    return tx.commit();
}

} // namespace linernotes::library
