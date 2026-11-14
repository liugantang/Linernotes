// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsStore.h"

#include "Errors.h"
#include "Lyrics.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace linernotes::guide {

LyricsStore::LyricsStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<std::optional<StoredLyrics>> LyricsStore::get(qint64 trackId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT source, text, synced, instrumental, lrclib_id, fetched_at "
                             "FROM lyrics WHERE track_id = ?;"));
    q.addBindValue(trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to query lyrics"),
            .detail = q.lastError().text(),
        };
    }
    if (!q.next()) {
        return std::optional<StoredLyrics>(std::nullopt);
    }

    StoredLyrics stored;
    const QString source = q.value(0).toString();
    if (source == QStringLiteral("manual")) {
        stored.kind = StoredLyrics::Kind::Manual;
    } else if (source == QStringLiteral("lrclib")) {
        stored.kind = StoredLyrics::Kind::Lrclib;
    } else {
        stored.kind = StoredLyrics::Kind::NotFound;
    }
    stored.text = q.value(1).toString();
    stored.synced = q.value(2).toInt() == 1;
    stored.instrumental = q.value(3).toInt() == 1;
    if (!q.value(4).isNull()) {
        stored.lrclibId = q.value(4).toLongLong();
    }
    stored.fetchedAtMs = q.value(5).toLongLong();
    return std::optional<StoredLyrics>(stored);
}

core::Result<void> LyricsStore::saveManual(qint64 trackId, const QString &text) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    if (text.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM lyrics WHERE track_id = ? AND source = 'manual';"));
        q.addBindValue(trackId);
        if (!q.exec()) {
            return core::Error {
                .code = QString(errc::kDbFailed),
                .message = QStringLiteral("Failed to delete manual lyrics"),
                .detail = q.lastError().text(),
            };
        }
        return { };
    }

    const bool synced = looksSynced(text);
    const qint64 now = m_clock.nowMs();
    q.prepare(QStringLiteral(
        "INSERT INTO lyrics (track_id, source, text, synced, instrumental, lrclib_id, fetched_at) "
        "VALUES (?, 'manual', ?, ?, 0, NULL, ?) "
        "ON CONFLICT(track_id) DO UPDATE SET "
        "source = 'manual', text = excluded.text, synced = excluded.synced, "
        "instrumental = 0, lrclib_id = NULL, fetched_at = excluded.fetched_at;"));
    q.addBindValue(trackId);
    q.addBindValue(text);
    q.addBindValue(synced ? 1 : 0);
    q.addBindValue(now);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to save manual lyrics"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> LyricsStore::saveLrclib(
    qint64 trackId, const QString &text, bool synced, bool instrumental, qint64 lrclibId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const qint64 now = m_clock.nowMs();
    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT INTO lyrics (track_id, source, text, synced, instrumental, lrclib_id, fetched_at) "
        "VALUES (?, 'lrclib', ?, ?, ?, ?, ?) "
        "ON CONFLICT(track_id) DO UPDATE SET "
        "source = excluded.source, text = excluded.text, synced = excluded.synced, "
        "instrumental = excluded.instrumental, lrclib_id = excluded.lrclib_id, "
        "fetched_at = excluded.fetched_at "
        "WHERE lyrics.source <> 'manual';"));
    q.addBindValue(trackId);
    q.addBindValue(text);
    q.addBindValue(synced ? 1 : 0);
    q.addBindValue(instrumental ? 1 : 0);
    q.addBindValue(lrclibId > 0 ? QVariant(lrclibId) : QVariant(QMetaType(QMetaType::LongLong)));
    q.addBindValue(now);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to save lrclib lyrics"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> LyricsStore::saveNotFound(qint64 trackId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const qint64 now = m_clock.nowMs();
    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT INTO lyrics (track_id, source, text, synced, instrumental, lrclib_id, fetched_at) "
        "VALUES (?, 'none', '', 0, 0, NULL, ?) "
        "ON CONFLICT(track_id) DO UPDATE SET "
        "source = excluded.source, text = excluded.text, synced = excluded.synced, "
        "instrumental = excluded.instrumental, lrclib_id = excluded.lrclib_id, "
        "fetched_at = excluded.fetched_at "
        "WHERE lyrics.source <> 'manual';"));
    q.addBindValue(trackId);
    q.addBindValue(now);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to save not found lyrics"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> LyricsStore::removeOnline(qint64 trackId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("DELETE FROM lyrics WHERE track_id = ? AND source IN ('lrclib', 'none');"));
    q.addBindValue(trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to remove online lyrics"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

} // namespace linernotes::guide
