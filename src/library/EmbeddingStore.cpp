// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EmbeddingStore.h"

#include <QByteArray>
#include <QDataStream>
#include <QFloat16>
#include <QIODevice>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtEndian>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>

namespace linernotes::library {

namespace {

QByteArray serializeVector(const QList<float> &vec)
{
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    for (const float v : vec) {
        const qfloat16 f16(v);
        stream << f16;
    }
    return blob;
}

std::optional<QList<float>> deserializeVector(const QByteArray &blob)
{
    if (blob.size() % static_cast<qsizetype>(sizeof(quint16)) != 0) {
        return std::nullopt;
    }
    // 整块转换：逐个经 QDataStream 读取在 1.5 万首时要数秒（尤其 debug 构建）
    const qsizetype count = blob.size() / static_cast<qsizetype>(sizeof(quint16));
    QList<qfloat16> halves(count);
    qFromLittleEndian<quint16>(blob.constData(), count, halves.data());
    QList<float> vec(count);
    qFloatFromFloat16(vec.data(), halves.constData(), count);
    return vec;
}

constexpr const char *kPendingTracksFromAndWhereSql = R"(
FROM tracks t
JOIN files f ON f.id = t.file_id
LEFT JOIN audio_embeddings ae ON ae.track_id = t.id
WHERE f.missing_since IS NULL
  AND f.scan_error IS NULL
  AND (
      ae.track_id IS NULL
      OR ae.model != ?
      OR f.content_hash IS NULL
      OR ae.content_hash IS NULL
      OR ae.content_hash != f.content_hash
  )
)";

} // namespace

EmbeddingStore::EmbeddingStore(Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<QList<qint64>> EmbeddingStore::pendingTrackIds(const QString &model) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT t.id ") + QString::fromUtf8(kPendingTracksFromAndWhereSql)
        + QStringLiteral(" ORDER BY t.id ASC;"));
    q.addBindValue(model);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<qint64> trackIds;
    while (q.next()) {
        trackIds.append(q.value(0).toLongLong());
    }
    q.finish();
    return trackIds;
}

core::Result<int> EmbeddingStore::pendingCount(const QString &model) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) ") + QString::fromUtf8(kPendingTracksFromAndWhereSql)
        + QStringLiteral(";"));
    q.addBindValue(model);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    if (q.next()) {
        const int count = q.value(0).toInt();
        q.finish();
        return count;
    }
    q.finish();
    return 0;
}

core::Result<EmbedSource> EmbeddingStore::source(qint64 trackId) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT f.path, t.cue_index, t.start_ms, t.end_ms, f.duration_ms "
                             "FROM tracks t "
                             "JOIN files f ON f.id = t.file_id "
                             "WHERE t.id = ?;"));
    q.addBindValue(trackId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(trackId),
        };
    }

    if (!q.next()) {
        return core::Error {
            .code = QString(errc::kTrackNotFound),
            .message = QStringLiteral("Track not found: %1").arg(trackId),
            .detail = QString::number(trackId),
        };
    }

    const QString path = q.value(0).toString();
    const bool isCue = !q.value(1).isNull();
    qint64 startMs = 0;
    qint64 durationMs = 0;

    if (isCue) {
        startMs = q.value(2).isNull() ? 0 : q.value(2).toLongLong();
        if (!q.value(3).isNull()) {
            durationMs = q.value(3).toLongLong() - startMs;
        } else {
            const qint64 fileDuration = q.value(4).isNull() ? 0 : q.value(4).toLongLong();
            durationMs = fileDuration - startMs;
        }
    } else {
        startMs = 0;
        durationMs = q.value(4).isNull() ? 0 : q.value(4).toLongLong();
    }

    durationMs = std::max<qint64>(0, durationMs);

    return EmbedSource {
        .trackId = trackId,
        .path = path,
        .startMs = startMs,
        .durationMs = durationMs,
    };
}

core::Result<void> EmbeddingStore::save(
    qint64 trackId, const QString &model, const QList<float> &vector)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery hashQuery(conn);
    hashQuery.prepare(QStringLiteral("SELECT f.content_hash FROM tracks t "
                                     "JOIN files f ON f.id = t.file_id WHERE t.id = ?;"));
    hashQuery.addBindValue(trackId);
    if (!hashQuery.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = hashQuery.lastError().text(),
            .detail = QString::number(trackId),
        };
    }
    if (!hashQuery.next()) {
        return core::Error {
            .code = QString(errc::kTrackNotFound),
            .message = QStringLiteral("Track not found for track_id %1").arg(trackId),
            .detail = QString::number(trackId),
        };
    }
    const QVariant contentHash = hashQuery.value(0);

    const QByteArray blob = serializeVector(vector);
    const qint64 now = m_clock.nowMs();

    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("INSERT OR REPLACE INTO audio_embeddings (track_id, model, content_hash, "
                       "vector, error, computed_at) "
                       "VALUES (?, ?, ?, ?, NULL, ?);"));
    q.addBindValue(trackId);
    q.addBindValue(model);
    q.addBindValue(contentHash);
    q.addBindValue(blob);
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

core::Result<void> EmbeddingStore::saveFailure(
    qint64 trackId, const QString &model, const QString &error)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery hashQuery(conn);
    hashQuery.prepare(QStringLiteral("SELECT f.content_hash FROM tracks t "
                                     "JOIN files f ON f.id = t.file_id WHERE t.id = ?;"));
    hashQuery.addBindValue(trackId);
    if (!hashQuery.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = hashQuery.lastError().text(),
            .detail = QString::number(trackId),
        };
    }
    if (!hashQuery.next()) {
        return core::Error {
            .code = QString(errc::kTrackNotFound),
            .message = QStringLiteral("Track not found for track_id %1").arg(trackId),
            .detail = QString::number(trackId),
        };
    }
    const QVariant contentHash = hashQuery.value(0);
    const qint64 now = m_clock.nowMs();

    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("INSERT OR REPLACE INTO audio_embeddings (track_id, model, content_hash, "
                       "vector, error, computed_at) "
                       "VALUES (?, ?, ?, NULL, ?, ?);"));
    q.addBindValue(trackId);
    q.addBindValue(model);
    q.addBindValue(contentHash);
    q.addBindValue(error);
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

core::Result<QList<EmbeddingStore::StoredEmbedding>> EmbeddingStore::loadAll(
    const QString &model) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT ae.track_id, ae.vector "
                             "FROM audio_embeddings ae "
                             "JOIN tracks t ON t.id = ae.track_id "
                             "JOIN files f ON f.id = t.file_id "
                             "WHERE ae.model = ? "
                             "  AND ae.vector IS NOT NULL "
                             "  AND ae.error IS NULL "
                             "  AND ae.content_hash IS NOT NULL "
                             "  AND f.content_hash IS NOT NULL "
                             "  AND ae.content_hash = f.content_hash "
                             "ORDER BY ae.track_id ASC;"));
    q.addBindValue(model);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<StoredEmbedding> results;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QByteArray blob = q.value(1).toByteArray();
        const auto vecOpt = deserializeVector(blob);
        if (vecOpt.has_value()) {
            results.append(StoredEmbedding {
                .trackId = trackId,
                .vector = *vecOpt,
            });
        }
    }

    return results;
}

core::Result<int> EmbeddingStore::analyzedCount(const QString &model) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) "
                             "FROM audio_embeddings ae "
                             "JOIN tracks t ON t.id = ae.track_id "
                             "JOIN files f ON f.id = t.file_id "
                             "WHERE ae.model = ? "
                             "  AND ae.vector IS NOT NULL "
                             "  AND ae.error IS NULL "
                             "  AND ae.content_hash IS NOT NULL "
                             "  AND f.content_hash IS NOT NULL "
                             "  AND ae.content_hash = f.content_hash;"));
    q.addBindValue(model);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    if (q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

} // namespace linernotes::library
