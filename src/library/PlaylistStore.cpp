// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlaylistStore.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>

namespace linernotes::library {

PlaylistStore::PlaylistStore(Database &db)
    : m_db(db)
{
}

core::Result<qint64> PlaylistStore::createManual(const QString &name, const QList<qint64> &trackIds)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Playlist name cannot be empty"),
            .detail = name,
        };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    const QSqlDatabase &conn = connRes.value();
    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction"),
            .detail = QString(),
        };
    }

    // Determine new playlist position (max existing position + 1, or 0 if none)
    qint64 nextPos = 0;
    QSqlQuery posQuery(conn);
    if (posQuery.exec(QStringLiteral("SELECT MAX(position) FROM playlists")) && posQuery.next()) {
        if (!posQuery.isNull(0)) {
            nextPos = posQuery.value(0).toLongLong() + 1;
        }
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery insertPlaylist(conn);
    insertPlaylist.prepare(
        QStringLiteral("INSERT INTO playlists (name, kind, rule, position, created_at, updated_at) "
                       "VALUES (?, 'manual', NULL, ?, ?, ?)"));
    insertPlaylist.addBindValue(trimmedName);
    insertPlaylist.addBindValue(nextPos);
    insertPlaylist.addBindValue(now);
    insertPlaylist.addBindValue(now);

    if (!insertPlaylist.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = insertPlaylist.lastError().text(),
            .detail = trimmedName,
        };
    }

    const qint64 playlistId = insertPlaylist.lastInsertId().toLongLong();

    if (!trackIds.isEmpty()) {
        QSqlQuery insertItem(conn);
        insertItem.prepare(
            QStringLiteral("INSERT INTO playlist_items (playlist_id, track_id, position, added_at) "
                           "VALUES (?, ?, ?, ?)"));

        for (int i = 0; i < trackIds.size(); ++i) {
            insertItem.bindValue(0, playlistId);
            insertItem.bindValue(1, trackIds.at(i));
            insertItem.bindValue(2, i);
            insertItem.bindValue(3, now);
            if (!insertItem.exec()) {
                return core::Error {
                    .code = QString(errc::kDbQuery),
                    .message = insertItem.lastError().text(),
                    .detail = QString::number(playlistId),
                };
            }
        }
    }

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        return commitRes.error();
    }

    return playlistId;
}

} // namespace linernotes::library
