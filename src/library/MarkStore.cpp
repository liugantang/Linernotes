// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MarkStore.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

#include <library/Database.h>
#include <library/Errors.h>

#include <utility>

namespace linernotes::library {

namespace {

QString entityTypeName(FavoriteKind kind)
{
    switch (kind) {
    case FavoriteKind::Track:
        return QStringLiteral("track");
    case FavoriteKind::Album:
        return QStringLiteral("album");
    case FavoriteKind::Artist:
        return QStringLiteral("artist");
    }
    return { };
}

core::Result<void> execWrite(QSqlQuery &q, const QString &detail = QString())
{
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = detail,
        };
    }
    return { };
}

template <typename F>
auto inTransaction(Database &db, F &&body) -> decltype(body(std::declval<const QSqlDatabase &>()))
{
    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();
    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction"),
            .detail = QString(),
        };
    }

    auto res = std::forward<F>(body)(conn);
    if (!res.ok()) {
        return res;
    }

    if (auto commitRes = tx.commit(); !commitRes.ok()) {
        return commitRes.error();
    }

    return res;
}

} // namespace

MarkStore::MarkStore(Database &db)
    : m_db(db)
{
}

core::Result<void> MarkStore::setFavorite(
    FavoriteKind kind, const QList<qint64> &ids, bool favorite)
{
    if (ids.isEmpty()) {
        return { };
    }

    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        const QString typeStr = entityTypeName(kind);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();

        if (favorite) {
            QSqlQuery q(conn);
            q.prepare(QStringLiteral("INSERT INTO favorites (entity_type, entity_id, created_at) "
                                     "VALUES (?, ?, ?) "
                                     "ON CONFLICT(entity_type, entity_id) DO NOTHING"));
            for (const qint64 id : ids) {
                q.bindValue(0, typeStr);
                q.bindValue(1, id);
                q.bindValue(2, now);
                if (auto res = execWrite(q, QString::number(id)); !res.ok()) {
                    return res;
                }
            }
        } else {
            QSqlQuery q(conn);
            q.prepare(
                QStringLiteral("DELETE FROM favorites WHERE entity_type = ? AND entity_id = ?"));
            for (const qint64 id : ids) {
                q.bindValue(0, typeStr);
                q.bindValue(1, id);
                if (auto res = execWrite(q, QString::number(id)); !res.ok()) {
                    return res;
                }
            }
        }
        return { };
    });
}

core::Result<bool> MarkStore::isFavorite(FavoriteKind kind, qint64 id) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT 1 FROM favorites WHERE entity_type = ? AND entity_id = ?"));
    q.bindValue(0, entityTypeName(kind));
    q.bindValue(1, id);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(id),
        };
    }
    return q.next();
}

core::Result<void> MarkStore::setRating(const QList<qint64> &trackIds, int rating)
{
    if (rating < 0 || rating > 5) {
        return core::Error {
            .code = QString(errc::kRatingInvalid),
            .message = QStringLiteral("Rating must be between 0 and 5"),
            .detail = QString::number(rating),
        };
    }

    if (trackIds.isEmpty()) {
        return { };
    }

    return inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();

        if (rating == 0) {
            QSqlQuery q(conn);
            q.prepare(QStringLiteral("DELETE FROM ratings WHERE track_id = ?"));
            for (const qint64 id : trackIds) {
                q.bindValue(0, id);
                if (auto res = execWrite(q, QString::number(id)); !res.ok()) {
                    return res;
                }
            }
        } else {
            QSqlQuery q(conn);
            q.prepare(QStringLiteral(
                "INSERT INTO ratings (track_id, rating, updated_at) VALUES (?, ?, ?) "
                "ON CONFLICT(track_id) DO UPDATE SET rating = excluded.rating, "
                "updated_at = excluded.updated_at"));
            for (const qint64 id : trackIds) {
                q.bindValue(0, id);
                q.bindValue(1, rating);
                q.bindValue(2, now);
                if (auto res = execWrite(q, QString::number(id)); !res.ok()) {
                    return res;
                }
            }
        }
        return { };
    });
}

core::Result<int> MarkStore::rating(qint64 trackId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT rating FROM ratings WHERE track_id = ?"));
    q.bindValue(0, trackId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(trackId),
        };
    }
    if (q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

} // namespace linernotes::library
