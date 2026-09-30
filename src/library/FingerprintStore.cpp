// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "FingerprintStore.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

namespace linernotes::library {

namespace {

// Little-endian uint32 serialization, matching audio::toBlob / itemsFromBlob format
QByteArray serializeItems(const QList<quint32> &items)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (const quint32 item : items) {
        stream << item;
    }
    return blob;
}

std::optional<QList<quint32>> deserializeItems(const QByteArray &blob)
{
    if (blob.size() % static_cast<qsizetype>(sizeof(quint32)) != 0) {
        return std::nullopt;
    }
    const qsizetype count = blob.size() / static_cast<qsizetype>(sizeof(quint32));
    QList<quint32> items;
    items.reserve(count);
    QDataStream stream(blob);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (qsizetype i = 0; i < count; ++i) {
        quint32 item = 0;
        stream >> item;
        items.append(item);
    }
    return items;
}

} // namespace

FingerprintStore::FingerprintStore(Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<QList<qint64>> FingerprintStore::pendingFileIds() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    const QString sql
        = QStringLiteral("SELECT f.id "
                         "FROM files f "
                         "LEFT JOIN fingerprints fp ON fp.file_id = f.id "
                         "WHERE f.missing_since IS NULL "
                         "  AND f.scan_error IS NULL "
                         "  AND EXISTS (SELECT 1 FROM tracks t WHERE t.file_id = f.id) "
                         "  AND ( "
                         "      fp.file_id IS NULL "
                         "      OR f.content_hash IS NULL "
                         "      OR fp.content_hash IS NULL "
                         "      OR fp.content_hash != f.content_hash "
                         "  ) "
                         "ORDER BY f.id ASC;");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<qint64> fileIds;
    while (q.next()) {
        fileIds.append(q.value(0).toLongLong());
    }
    return fileIds;
}

core::Result<QString> FingerprintStore::filePath(qint64 fileId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT path FROM files WHERE id = ?;"));
    q.addBindValue(fileId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(fileId),
        };
    }

    if (!q.next()) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = QStringLiteral("File not found for file_id %1").arg(fileId),
            .detail = QString::number(fileId),
        };
    }

    return q.value(0).toString();
}

core::Result<void> FingerprintStore::save(qint64 fileId, int algorithm, const QList<quint32> &items)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery hashQuery(conn);
    hashQuery.prepare(QStringLiteral("SELECT content_hash FROM files WHERE id = ?;"));
    hashQuery.addBindValue(fileId);
    if (!hashQuery.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = hashQuery.lastError().text(),
            .detail = QString::number(fileId),
        };
    }
    if (!hashQuery.next()) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = QStringLiteral("File not found for file_id %1").arg(fileId),
            .detail = QString::number(fileId),
        };
    }
    const QVariant contentHash = hashQuery.value(0);

    const QByteArray blob = serializeItems(items);
    const qint64 now = m_clock.nowMs();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO fingerprints (file_id, content_hash, "
                             "algorithm, fingerprint, error, computed_at) "
                             "VALUES (?, ?, ?, ?, NULL, ?);"));
    q.addBindValue(fileId);
    q.addBindValue(contentHash);
    q.addBindValue(algorithm);
    q.addBindValue(blob);
    q.addBindValue(now);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(fileId),
        };
    }

    return { };
}

core::Result<void> FingerprintStore::saveFailure(qint64 fileId, const QString &error)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery hashQuery(conn);
    hashQuery.prepare(QStringLiteral("SELECT content_hash FROM files WHERE id = ?;"));
    hashQuery.addBindValue(fileId);
    if (!hashQuery.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = hashQuery.lastError().text(),
            .detail = QString::number(fileId),
        };
    }
    if (!hashQuery.next()) {
        return core::Error {
            .code = QString(errc::kFileRead),
            .message = QStringLiteral("File not found for file_id %1").arg(fileId),
            .detail = QString::number(fileId),
        };
    }
    const QVariant contentHash = hashQuery.value(0);
    const qint64 now = m_clock.nowMs();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO fingerprints (file_id, content_hash, "
                             "algorithm, fingerprint, error, computed_at) "
                             "VALUES (?, ?, NULL, NULL, ?, ?);"));
    q.addBindValue(fileId);
    q.addBindValue(contentHash);
    q.addBindValue(error);
    q.addBindValue(now);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(fileId),
        };
    }

    return { };
}

core::Result<std::optional<StoredFingerprint>> FingerprintStore::load(qint64 fileId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT algorithm, fingerprint FROM fingerprints "
                             "WHERE file_id = ? AND fingerprint IS NOT NULL AND error IS NULL;"));
    q.addBindValue(fileId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(fileId),
        };
    }

    if (!q.next()) {
        return std::optional<StoredFingerprint>(std::nullopt);
    }

    const int algorithm = q.value(0).toInt();
    const QByteArray blob = q.value(1).toByteArray();
    const auto itemsOpt = deserializeItems(blob);
    if (!itemsOpt.has_value()) {
        return std::optional<StoredFingerprint>(std::nullopt);
    }

    return std::optional<StoredFingerprint>(StoredFingerprint {
        .fileId = fileId,
        .algorithm = algorithm,
        .items = *itemsOpt,
    });
}

core::Result<QList<StoredFingerprint>> FingerprintStore::loadAll() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT fp.file_id, fp.algorithm, fp.fingerprint "
                                       "FROM fingerprints fp "
                                       "JOIN files f ON f.id = fp.file_id "
                                       "WHERE fp.fingerprint IS NOT NULL "
                                       "  AND fp.error IS NULL "
                                       "  AND fp.content_hash IS NOT NULL "
                                       "  AND f.content_hash IS NOT NULL "
                                       "  AND fp.content_hash = f.content_hash "
                                       "ORDER BY fp.file_id ASC;");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<StoredFingerprint> results;
    while (q.next()) {
        const qint64 fileId = q.value(0).toLongLong();
        const int algorithm = q.value(1).toInt();
        const QByteArray blob = q.value(2).toByteArray();
        const auto itemsOpt = deserializeItems(blob);
        if (itemsOpt.has_value()) {
            results.append(StoredFingerprint {
                .fileId = fileId,
                .algorithm = algorithm,
                .items = *itemsOpt,
            });
        }
    }

    return results;
}

} // namespace linernotes::library
