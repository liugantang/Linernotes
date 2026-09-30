// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DuplicateResolver.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>
#include <library/TrackMerge.h>

#include <algorithm>

namespace linernotes::butler {

core::Result<void> SystemFileTrash::moveToTrash(const QString &path)
{
    if (QFile::moveToTrash(path)) {
        return { };
    }
    return core::Error {
        .code = QString(errc::kDuplicateTrashFailed),
        .message = QStringLiteral("Failed to move file to trash"),
        .detail = path,
    };
}

DuplicateResolver::DuplicateResolver(
    library::Database &db, FileTrash &trash, const core::Clock &clock)
    : m_db(db)
    , m_trash(trash)
    , m_clock(clock)
{
}

core::Result<ResolveOutcome> DuplicateResolver::resolve(qint64 groupId, qint64 keepTrackId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    auto conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT m.track_id, f.path "
                             "FROM duplicate_members m "
                             "JOIN tracks t ON t.id = m.track_id "
                             "JOIN files f ON f.id = t.file_id "
                             "WHERE m.group_id = ?;"));
    q.addBindValue(groupId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("Failed to query duplicate members"),
        };
    }

    struct MemberInfo {
        qint64 trackId = 0;
        QString path;
    };

    QList<MemberInfo> members;
    bool hasKeepTrack = false;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QString path = q.value(1).toString();
        if (trackId == keepTrackId) {
            hasKeepTrack = true;
        }
        members.append(MemberInfo { .trackId = trackId, .path = path });
    }

    if (members.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDuplicateGroupNotFound),
            .message = QStringLiteral("Duplicate group not found"),
            .detail = QString::number(groupId),
        };
    }

    if (!hasKeepTrack) {
        return core::Error {
            .code = QString(errc::kDuplicateTrackNotInGroup),
            .message = QStringLiteral("Keep track ID is not in group"),
            .detail = QStringLiteral("group: %1, keep: %2").arg(groupId).arg(keepTrackId),
        };
    }

    ResolveOutcome outcome;
    for (const auto &member : members) {
        if (member.trackId == keepTrackId) {
            continue;
        }

        const auto trashRes = m_trash.moveToTrash(member.path);
        if (!trashRes.ok()) {
            outcome.failedPaths.append(member.path);
            continue;
        }

        const auto mergeRes = library::mergeTrackInto(conn, member.trackId, keepTrackId);
        if (!mergeRes.ok()) {
            outcome.failedPaths.append(member.path);
            continue;
        }

        outcome.removedTrackIds.append(member.trackId);
    }

    if (outcome.failedPaths.isEmpty()) {
        QSqlQuery delGroupQ(conn);
        delGroupQ.prepare(QStringLiteral("DELETE FROM duplicate_groups WHERE id = ?;"));
        delGroupQ.addBindValue(groupId);
        if (!delGroupQ.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = delGroupQ.lastError().text(),
                .detail = QStringLiteral("Failed to delete duplicate group"),
            };
        }
    }

    return outcome;
}

core::Result<void> DuplicateResolver::dismiss(qint64 groupId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT track_id FROM duplicate_members WHERE group_id = ? ORDER BY track_id ASC;"));
    q.addBindValue(groupId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QStringLiteral("Failed to query duplicate members for dismiss"),
        };
    }

    QList<qint64> trackIds;
    while (q.next()) {
        trackIds.append(q.value(0).toLongLong());
    }

    if (trackIds.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDuplicateGroupNotFound),
            .message = QStringLiteral("Duplicate group not found"),
            .detail = QString::number(groupId),
        };
    }

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for dismiss"),
            .detail = QString::number(groupId),
        };
    }

    const qint64 now = m_clock.nowMs();
    QSqlQuery insDismiss(conn);
    insDismiss.prepare(
        QStringLiteral("INSERT OR IGNORE INTO duplicate_dismissals (track_a, track_b, created_at) "
                       "VALUES (?, ?, ?);"));

    for (qsizetype i = 0; i < trackIds.size(); ++i) {
        for (qsizetype j = i + 1; j < trackIds.size(); ++j) {
            const qint64 a = trackIds.at(i);
            const qint64 b = trackIds.at(j);
            insDismiss.bindValue(0, std::min(a, b));
            insDismiss.bindValue(1, std::max(a, b));
            insDismiss.bindValue(2, now);
            if (!insDismiss.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = insDismiss.lastError().text(),
                    .detail = QStringLiteral("Failed to insert into duplicate_dismissals"),
                };
            }
        }
    }

    QSqlQuery delGroupQ(conn);
    delGroupQ.prepare(QStringLiteral("DELETE FROM duplicate_groups WHERE id = ?;"));
    delGroupQ.addBindValue(groupId);
    if (!delGroupQ.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = delGroupQ.lastError().text(),
            .detail = QStringLiteral("Failed to delete duplicate group"),
        };
    }

    return tx.commit();
}

} // namespace linernotes::butler
