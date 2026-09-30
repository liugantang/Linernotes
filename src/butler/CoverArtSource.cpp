// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CoverArtSource.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Database.h>
#include <library/Errors.h>

#include <utility>
namespace linernotes::butler {

CoverArtSource::CoverArtSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<qint64>> CoverArtSource::pendingAlbums() const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT a.id "
                             "FROM albums a "
                             "JOIN mb_album_matches m ON m.album_id = a.id "
                             "WHERE a.cover_id IS NULL "
                             "  AND m.status = 'matched' "
                             "  AND m.release_id IS NOT NULL "
                             "  AND m.release_id != '' "
                             "  AND m.cover_checked_at IS NULL "
                             "ORDER BY a.id ASC;"));
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }

    QList<qint64> albums;
    while (q.next()) {
        albums.append(q.value(0).toLongLong());
    }
    return albums;
}

core::Result<std::optional<CoverArtTarget>> CoverArtSource::load(qint64 albumId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT m.release_id, m.release_group_id "
                             "FROM albums a "
                             "JOIN mb_album_matches m ON m.album_id = a.id "
                             "WHERE a.id = ? "
                             "  AND a.cover_id IS NULL "
                             "  AND m.status = 'matched' "
                             "  AND m.release_id IS NOT NULL "
                             "  AND m.release_id != '' "
                             "  AND m.cover_checked_at IS NULL;"));
    q.addBindValue(albumId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }

    if (!q.next()) {
        return std::optional<CoverArtTarget> { };
    }

    CoverArtTarget target;
    target.albumId = albumId;
    target.releaseId = q.value(0).toString();
    target.releaseGroupId = q.value(1).toString();
    return std::optional<CoverArtTarget> { std::move(target) };
}

core::Result<void> CoverArtSource::saveCover(
    qint64 albumId, const library::CoverStore::Info &info, const QUrl &url, qint64 now) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn, library::Transaction::Mode::Immediate);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for saveCover"),
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
            .code = QString(library::errc::kDbQuery),
            .message = insertCoverQ.lastError().text(),
            .detail = insertCoverQ.lastQuery(),
        };
    }

    QSqlQuery selectCoverQ(conn);
    selectCoverQ.prepare(QStringLiteral("SELECT id FROM covers WHERE hash = ?;"));
    selectCoverQ.addBindValue(info.hash);
    if (!selectCoverQ.exec() || !selectCoverQ.next()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = selectCoverQ.lastError().text(),
            .detail = QStringLiteral("Failed to find cover id by hash"),
        };
    }
    const qint64 coverId = selectCoverQ.value(0).toLongLong();

    QSqlQuery updateAlbumQ(conn);
    updateAlbumQ.prepare(
        QStringLiteral("UPDATE albums SET cover_id = ? WHERE id = ? AND cover_id IS NULL;"));
    updateAlbumQ.addBindValue(coverId);
    updateAlbumQ.addBindValue(albumId);
    if (!updateAlbumQ.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = updateAlbumQ.lastError().text(),
            .detail = updateAlbumQ.lastQuery(),
        };
    }

    QSqlQuery updateCheckedQ(conn);
    updateCheckedQ.prepare(
        QStringLiteral("UPDATE mb_album_matches SET cover_checked_at = ? WHERE album_id = ?;"));
    updateCheckedQ.addBindValue(now);
    updateCheckedQ.addBindValue(albumId);
    if (!updateCheckedQ.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = updateCheckedQ.lastError().text(),
            .detail = updateCheckedQ.lastQuery(),
        };
    }

    return tx.commit();
}

core::Result<void> CoverArtSource::markChecked(qint64 albumId, qint64 now) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("UPDATE mb_album_matches SET cover_checked_at = ? WHERE album_id = ?;"));
    q.addBindValue(now);
    q.addBindValue(albumId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }
    return { };
}

} // namespace linernotes::butler
