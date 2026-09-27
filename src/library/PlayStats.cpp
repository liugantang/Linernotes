// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayStats.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/PlayCountRule.h>

namespace linernotes::library {

namespace {

QString aggregationSelectSql()
{
    return QStringLiteral(
        "SELECT "
        "  track_id, "
        "  COUNT(CASE WHEN %1 THEN 1 END) AS play_count, "
        "  MAX(CASE WHEN %1 THEN started_at END) AS last_played_at, "
        "  COALESCE(SUM(played_ms), 0) AS total_played_ms, "
        "  AVG(CASE WHEN track_duration_ms IS NOT NULL AND track_duration_ms > 0 "
        "           THEN MIN(played_ms * 1.0 / track_duration_ms, 1.0) END) AS avg_completion, "
        "  COUNT(CASE WHEN skipped = 1 THEN 1 END) AS skip_count "
        "FROM play_events")
        .arg(PlayCountRule::sqlCondition());
}

} // namespace

PlayStats::PlayStats(Database &db)
    : m_db(db)
{
}

core::Result<void> PlayStats::refreshTrack(qint64 trackId, const PlayCountRule &rule)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for refreshTrack"),
            .detail = QString::number(trackId),
        };
    }

    QSqlQuery delQ(conn);
    delQ.prepare(QStringLiteral("DELETE FROM track_play_stats WHERE track_id = ?;"));
    delQ.addBindValue(trackId);
    if (!delQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = delQ.lastError().text(),
            .detail = QStringLiteral("Failed to delete track_play_stats"),
        };
    }

    const QString sql = QStringLiteral(
        "INSERT INTO track_play_stats (track_id, play_count, last_played_at, total_played_ms, "
        "avg_completion, skip_count) %1 WHERE track_id = :track_id GROUP BY track_id;")
                            .arg(aggregationSelectSql());

    QSqlQuery insQ(conn);
    if (!insQ.prepare(sql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = insQ.lastError().text(),
            .detail = QStringLiteral("Failed to prepare refreshTrack insert query"),
        };
    }
    rule.bindSql(insQ);
    insQ.bindValue(QStringLiteral(":track_id"), trackId);

    if (!insQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = insQ.lastError().text(),
            .detail = QStringLiteral("Failed to execute refreshTrack insert query"),
        };
    }

    return tx.commit();
}

core::Result<void> PlayStats::rebuildAll(const PlayCountRule &rule)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for rebuildAll"),
            .detail = QString(),
        };
    }

    QSqlQuery delQ(conn);
    if (!delQ.exec(QStringLiteral("DELETE FROM track_play_stats;"))) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = delQ.lastError().text(),
            .detail = QStringLiteral("Failed to clear track_play_stats"),
        };
    }

    const QString sql = QStringLiteral(
        "INSERT INTO track_play_stats (track_id, play_count, last_played_at, total_played_ms, "
        "avg_completion, skip_count) %1 WHERE track_id IS NOT NULL GROUP BY track_id;")
                            .arg(aggregationSelectSql());

    QSqlQuery insQ(conn);
    if (!insQ.prepare(sql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = insQ.lastError().text(),
            .detail = QStringLiteral("Failed to prepare rebuildAll insert query"),
        };
    }
    rule.bindSql(insQ);

    if (!insQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = insQ.lastError().text(),
            .detail = QStringLiteral("Failed to execute rebuildAll insert query"),
        };
    }

    return tx.commit();
}

core::Result<TrackPlayStats> PlayStats::track(qint64 trackId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT play_count, last_played_at, total_played_ms, avg_completion, skip_count "
        "FROM track_play_stats WHERE track_id = ?;"));
    q.addBindValue(trackId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("Failed to query track_play_stats"),
        };
    }

    TrackPlayStats stats;
    if (q.next()) {
        stats.playCount = q.value(0).toInt();
        if (!q.value(1).isNull()) {
            stats.lastPlayedAtMs = q.value(1).toLongLong();
        }
        stats.totalPlayedMs = q.value(2).toLongLong();
        if (!q.value(3).isNull()) {
            stats.avgCompletion = q.value(3).toDouble();
        }
        stats.skipCount = q.value(4).toInt();
    }
    return stats;
}

core::Result<std::optional<AlbumCompletion>> PlayStats::album(qint64 albumId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT track_count, played_track_count, completion "
                             "FROM album_play_completion WHERE album_id = ?;"));
    q.addBindValue(albumId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("Failed to query album_play_completion"),
        };
    }

    if (q.next()) {
        AlbumCompletion comp;
        comp.trackCount = q.value(0).toInt();
        comp.playedTrackCount = q.value(1).toInt();
        comp.completion = q.value(2).toDouble();
        return std::make_optional(comp);
    }
    return std::optional<AlbumCompletion> { std::nullopt };
}

} // namespace linernotes::library
