// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackIssueStore.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>

namespace linernotes::library {

TrackIssueStore::TrackIssueStore(Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<void> TrackIssueStore::add(
    qint64 trackId, TrackIssueKind kind, std::optional<TagField> field, const QString &detail)
{
    if (trackId <= 0) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("trackId must be positive"),
            .detail = QString::number(trackId),
        };
    }

    const QString kindStr = trackIssueKindToString(kind);
    if (kindStr.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Invalid track issue kind"),
            .detail = QString(),
        };
    }

    const QString fieldStr = field.has_value() ? tagFieldToColumn(*field) : QString();
    const qint64 now = m_clock.nowMs();

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT INTO track_issues (track_id, kind, field, detail, created_at) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(track_id, kind, field) DO UPDATE SET detail = excluded.detail"));
    q.addBindValue(trackId);
    q.addBindValue(kindStr);
    q.addBindValue(fieldStr);
    q.addBindValue(detail);
    q.addBindValue(now);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(trackId),
        };
    }

    return { };
}

core::Result<int> TrackIssueStore::count(TrackIssueKind kind) const
{
    const QString kindStr = trackIssueKindToString(kind);
    if (kindStr.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Invalid track issue kind"),
            .detail = QString(),
        };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(DISTINCT track_id) FROM track_issues WHERE kind = ?"));
    q.addBindValue(kindStr);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = kindStr,
        };
    }

    if (q.next()) {
        return q.value(0).toInt();
    }

    return 0;
}

} // namespace linernotes::library
