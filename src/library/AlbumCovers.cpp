// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AlbumCovers.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Database.h>
#include <library/Errors.h>

namespace linernotes::library {

core::Result<void> setOnlineAlbumCover(
    Database &db, qint64 albumId, const CoverStore::Info &info, const QUrl &url, qint64 now)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    Transaction tx(conn, Transaction::Mode::Immediate);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for setOnlineAlbumCover"),
            .detail = { },
        };
    }

    QSqlQuery insertCoverQ(conn);
    insertCoverQ.prepare(QStringLiteral(
        "INSERT INTO covers (hash, mime, width, height, source, source_path, created_at) "
        "VALUES (?, ?, ?, ?, 'online', ?, ?) "
        "ON CONFLICT(hash) DO NOTHING;"));
    insertCoverQ.addBindValue(info.hash);
    insertCoverQ.addBindValue(info.mime);
    insertCoverQ.addBindValue(info.width);
    insertCoverQ.addBindValue(info.height);
    insertCoverQ.addBindValue(url.toString());
    insertCoverQ.addBindValue(now);
    if (!insertCoverQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = insertCoverQ.lastError().text(),
            .detail = insertCoverQ.lastQuery(),
        };
    }

    QSqlQuery selectCoverQ(conn);
    selectCoverQ.prepare(QStringLiteral("SELECT id FROM covers WHERE hash = ?;"));
    selectCoverQ.addBindValue(info.hash);
    if (!selectCoverQ.exec() || !selectCoverQ.next()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = selectCoverQ.lastError().text(),
            .detail = QStringLiteral("Failed to find cover id by hash"),
        };
    }
    const qint64 coverId = selectCoverQ.value(0).toLongLong();

    QSqlQuery updateAlbumQ(conn);
    updateAlbumQ.prepare(QStringLiteral("UPDATE albums SET cover_id = ? WHERE id = ?;"));
    updateAlbumQ.addBindValue(coverId);
    updateAlbumQ.addBindValue(albumId);
    if (!updateAlbumQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = updateAlbumQ.lastError().text(),
            .detail = updateAlbumQ.lastQuery(),
        };
    }

    return tx.commit();
}

} // namespace linernotes::library
