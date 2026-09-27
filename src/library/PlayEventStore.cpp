// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayEventStore.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <core/Logging.h>
#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>

namespace linernotes::library {

namespace {

QVariant optionalValue(const std::optional<qint64> &opt)
{
    if (opt.has_value()) {
        return *opt;
    }
    return QVariant(QMetaType(QMetaType::LongLong));
}

QVariant optionalString(const QString &str)
{
    if (!str.isEmpty()) {
        return str;
    }
    return QVariant(QMetaType(QMetaType::QString));
}

void bindEventFields(QSqlQuery &q, const PlayEvent &event)
{
    q.bindValue(QStringLiteral(":track_id"), optionalValue(event.trackId));
    q.bindValue(QStringLiteral(":started_at"), event.startedAtMs);
    q.bindValue(QStringLiteral(":ended_at"), optionalValue(event.endedAtMs));
    q.bindValue(QStringLiteral(":played_ms"), event.playedMs);
    q.bindValue(QStringLiteral(":track_duration_ms"), optionalValue(event.durationMs));
    q.bindValue(QStringLiteral(":completed"), event.completed ? 1 : 0);
    q.bindValue(QStringLiteral(":skipped"), event.skipped ? 1 : 0);
    q.bindValue(QStringLiteral(":snapshot"), optionalString(event.snapshot));
    q.bindValue(QStringLiteral(":play_source"), core::playSourceToString(event.playSource));
    q.bindValue(QStringLiteral(":skip_position_ms"), optionalValue(event.skipPositionMs));
    q.bindValue(QStringLiteral(":paused_ms"), event.pausedMs);
    q.bindValue(QStringLiteral(":device"), optionalString(event.device));
}

PlayEvent readEvent(const QSqlQuery &q)
{
    PlayEvent event;
    event.id = q.value(0).toLongLong();
    if (!q.value(1).isNull()) {
        event.trackId = q.value(1).toLongLong();
    }
    event.startedAtMs = q.value(2).toLongLong();
    if (!q.value(3).isNull()) {
        event.endedAtMs = q.value(3).toLongLong();
    }
    event.playedMs = q.value(4).toLongLong();
    if (!q.value(5).isNull()) {
        event.durationMs = q.value(5).toLongLong();
    }
    event.completed = q.value(6).toInt() != 0;
    event.skipped = q.value(7).toInt() != 0;
    if (!q.value(8).isNull()) {
        event.snapshot = q.value(8).toString();
    }
    event.playSource = core::playSourceFromString(q.value(9).toString());
    if (!q.value(10).isNull()) {
        event.skipPositionMs = q.value(10).toLongLong();
    }
    event.pausedMs = q.value(11).toLongLong();
    if (!q.value(12).isNull()) {
        event.device = q.value(12).toString();
    }
    return event;
}

} // namespace

PlayEventStore::PlayEventStore(Database &db)
    : m_db(db)
{
}

core::Result<qint64> PlayEventStore::insert(const PlayEvent &event)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("INSERT INTO play_events ("
                             "  track_id, started_at, ended_at, played_ms, track_duration_ms,"
                             "  completed, skipped, source, snapshot, play_source,"
                             "  skip_position_ms, paused_ms, device"
                             ") VALUES ("
                             "  :track_id, :started_at, :ended_at, :played_ms, :track_duration_ms,"
                             "  :completed, :skipped, 'local', :snapshot, :play_source,"
                             "  :skip_position_ms, :paused_ms, :device"
                             ");"));

    bindEventFields(q, event);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("Failed to insert play_event"),
        };
    }

    return q.lastInsertId().toLongLong();
}

core::Result<void> PlayEventStore::update(const PlayEvent &event)
{
    if (event.id < 0) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Invalid event id for update"),
            .detail = QString::number(event.id),
        };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("UPDATE play_events SET "
                             "  track_id = :track_id,"
                             "  started_at = :started_at,"
                             "  ended_at = :ended_at,"
                             "  played_ms = :played_ms,"
                             "  track_duration_ms = :track_duration_ms,"
                             "  completed = :completed,"
                             "  skipped = :skipped,"
                             "  snapshot = :snapshot,"
                             "  play_source = :play_source,"
                             "  skip_position_ms = :skip_position_ms,"
                             "  paused_ms = :paused_ms,"
                             "  device = :device"
                             " WHERE id = :id;"));

    bindEventFields(q, event);
    q.bindValue(QStringLiteral(":id"), event.id);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(event.id),
        };
    }

    return { };
}

core::Result<QList<PlayEvent>> PlayEventStore::eventsBetween(qint64 fromMs, qint64 toMs) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT id, track_id, started_at, ended_at, played_ms, track_duration_ms,"
        "       completed, skipped, snapshot, play_source, skip_position_ms, paused_ms, device "
        "FROM play_events "
        "WHERE started_at >= ? AND started_at < ? "
        "ORDER BY started_at ASC;"));
    q.bindValue(0, fromMs);
    q.bindValue(1, toMs);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("eventsBetween query failed"),
        };
    }

    QList<PlayEvent> events;
    while (q.next()) {
        events.append(readEvent(q));
    }

    return events;
}

} // namespace linernotes::library
