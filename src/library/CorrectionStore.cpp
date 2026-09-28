// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CorrectionStore.h"

#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/MetadataRefresh.h>

#include <cmath>

namespace linernotes::library {

namespace {

core::Result<void> validateProposals(
    const QList<CorrectionProposal> &proposals, const std::optional<double> &autoAcceptThreshold)
{
    if (autoAcceptThreshold.has_value()) {
        const double th = *autoAcceptThreshold;
        if (std::isnan(th) || th < 0.0 || th > 1.0) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("autoAcceptThreshold must be between 0.0 and 1.0"),
                .detail = QString::number(th),
            };
        }
    }

    for (const auto &p : proposals) {
        if (p.trackId <= 0) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("trackId must be positive"),
                .detail = QString::number(p.trackId),
            };
        }
        if (std::isnan(p.confidence) || p.confidence < 0.0 || p.confidence > 1.0) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("Confidence must be between 0.0 and 1.0"),
                .detail = QString::number(p.confidence),
            };
        }
        const QString col = tagFieldToColumn(p.field);
        if (col.isEmpty()) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("Invalid tag field"),
                .detail = QString(),
            };
        }
    }
    return { };
}

QString fetchEffectiveValue(const QSqlDatabase &conn, qint64 trackId, TagField field)
{
    const QString col = tagFieldToColumn(field);
    if (col.isEmpty()) {
        return { };
    }
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT %1 FROM effective_metadata WHERE track_id = ?").arg(col));
    q.addBindValue(trackId);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return { };
}

core::Result<QPair<QList<qint64>, QSet<qint64>>> collectPendingCorrections(
    const QSqlDatabase &conn, const QList<qint64> &correctionIds)
{
    QSqlQuery selectStmt(conn);
    selectStmt.prepare(
        QStringLiteral("SELECT entity_id FROM corrections "
                       "WHERE id = ? AND status = 'pending' AND entity_type = 'track'"));

    QSet<qint64> affectedTrackIds;
    QList<qint64> pendingIds;

    for (const qint64 id : correctionIds) {
        selectStmt.bindValue(0, id);
        if (!selectStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = selectStmt.lastError().text(),
                .detail = QString::number(id),
            };
        }
        if (selectStmt.next()) {
            affectedTrackIds.insert(selectStmt.value(0).toLongLong());
            pendingIds.append(id);
        }
    }
    return qMakePair(pendingIds, affectedTrackIds);
}

} // namespace

CorrectionStore::CorrectionStore(Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<qint64> CorrectionStore::createBatch(CorrectionKind kind, const QString &description)
{
    const QString kindStr = correctionKindToString(kind);
    if (kindStr.isEmpty()) {
        return core::Error {
            .code = QString(errc::kCorrectionInvalid),
            .message = QStringLiteral("Invalid correction kind"),
            .detail = QString(),
        };
    }

    const qint64 now = m_clock.nowMs();
    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<qint64> {
        QSqlQuery q(conn);
        q.prepare(
            QStringLiteral("INSERT INTO correction_batches (kind, source, description, created_at) "
                           "VALUES (?, ?, ?, ?)"));
        q.addBindValue(kindStr);
        q.addBindValue(kindStr);
        q.addBindValue(description);
        q.addBindValue(now);

        if (auto res = detail::execWrite(q); !res.ok()) {
            return res.error();
        }
        return q.lastInsertId().toLongLong();
    });
}

core::Result<void> CorrectionStore::addProposals(qint64 batchId,
    const QList<CorrectionProposal> &proposals, std::optional<double> autoAcceptThreshold)
{
    if (proposals.isEmpty()) {
        return { };
    }

    if (auto valRes = validateProposals(proposals, autoAcceptThreshold); !valRes.ok()) {
        return valRes;
    }

    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        QSqlQuery batchCheck(conn);
        batchCheck.prepare(QStringLiteral("SELECT id FROM correction_batches WHERE id = ?"));
        batchCheck.addBindValue(batchId);
        if (!batchCheck.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = batchCheck.lastError().text(),
                .detail = QString::number(batchId),
            };
        }
        if (!batchCheck.next()) {
            return core::Error {
                .code = QString(errc::kCorrectionNotFound),
                .message = QStringLiteral("Correction batch not found"),
                .detail = QString::number(batchId),
            };
        }

        QSqlQuery insertStmt(conn);
        insertStmt.prepare(
            QStringLiteral("INSERT INTO corrections ("
                           "  entity_type, entity_id, field, old_value, new_value, "
                           "  source, confidence, status, reason, batch_id, created_at, decided_at"
                           ") VALUES ("
                           "  'track', ?, ?, ?, ?, "
                           "  ?, ?, ?, ?, ?, ?, ?"
                           ")"));

        QSet<qint64> affectedTrackIds;

        for (const auto &p : proposals) {
            const QString fieldCol = tagFieldToColumn(p.field);
            const QString oldVal = p.oldValue.has_value()
                ? *p.oldValue
                : fetchEffectiveValue(conn, p.trackId, p.field);

            const bool autoAccept
                = autoAcceptThreshold.has_value() && (p.confidence >= *autoAcceptThreshold);
            const CorrectionStatus status
                = autoAccept ? CorrectionStatus::Accepted : CorrectionStatus::Pending;

            insertStmt.bindValue(0, p.trackId);
            insertStmt.bindValue(1, fieldCol);
            insertStmt.bindValue(2, oldVal);
            insertStmt.bindValue(3, p.newValue);
            insertStmt.bindValue(4, correctionSourceToString(p.source));
            insertStmt.bindValue(5, p.confidence);
            insertStmt.bindValue(6, correctionStatusToString(status));
            insertStmt.bindValue(7, p.reason);
            insertStmt.bindValue(8, batchId);
            insertStmt.bindValue(9, now);
            if (autoAccept) {
                insertStmt.bindValue(10, now);
                affectedTrackIds.insert(p.trackId);
            } else {
                insertStmt.bindValue(10, QVariant(QMetaType(QMetaType::LongLong)));
            }

            if (auto res = detail::execWrite(insertStmt, QString::number(p.trackId)); !res.ok()) {
                return res;
            }
        }

        if (!affectedTrackIds.isEmpty()) {
            return detail::relinkTracks(conn, affectedTrackIds.values());
        }

        return { };
    });
}

core::Result<QList<CorrectionBatchInfo>> CorrectionStore::batches() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    const QString sql = QStringLiteral(
        "SELECT "
        "  b.id, "
        "  b.kind, "
        "  b.description, "
        "  b.created_at, "
        "  b.reverted_at, "
        "  COALESCE(SUM(CASE WHEN c.status = 'pending' THEN 1 ELSE 0 END), 0) AS pending_cnt, "
        "  COALESCE(SUM(CASE WHEN c.status = 'accepted' THEN 1 ELSE 0 END), 0) AS accepted_cnt, "
        "  COALESCE(SUM(CASE WHEN c.status = 'rejected' THEN 1 ELSE 0 END), 0) AS rejected_cnt, "
        "  COALESCE(SUM(CASE WHEN c.status = 'reverted' THEN 1 ELSE 0 END), 0) AS reverted_cnt "
        "FROM correction_batches b "
        "LEFT JOIN corrections c ON c.batch_id = b.id "
        "GROUP BY b.id "
        "ORDER BY b.created_at DESC, b.id DESC");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QList<CorrectionBatchInfo> list;
    while (q.next()) {
        CorrectionBatchInfo info;
        info.id = q.value(0).toLongLong();
        info.kind
            = correctionKindFromString(q.value(1).toString()).value_or(CorrectionKind::Manual);
        info.description = q.value(2).toString();
        info.createdAt = q.value(3).toLongLong();
        if (!q.value(4).isNull()) {
            info.revertedAt = q.value(4).toLongLong();
        }
        info.pending = q.value(5).toInt();
        info.accepted = q.value(6).toInt();
        info.rejected = q.value(7).toInt();
        info.reverted = q.value(8).toInt();
        list.append(info);
    }
    return list;
}

core::Result<QList<CorrectionRow>> CorrectionStore::corrections(
    qint64 batchId, const CorrectionFilter &filter) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QString sql = QStringLiteral("SELECT "
                                 "  c.id, "
                                 "  c.batch_id, "
                                 "  c.entity_id, "
                                 "  c.field, "
                                 "  c.old_value, "
                                 "  c.new_value, "
                                 "  c.source, "
                                 "  c.confidence, "
                                 "  c.reason, "
                                 "  c.status, "
                                 "  em.title AS track_title, "
                                 "  f.path AS file_path "
                                 "FROM corrections c "
                                 "LEFT JOIN effective_metadata em ON em.track_id = c.entity_id "
                                 "LEFT JOIN tracks t ON t.id = c.entity_id "
                                 "LEFT JOIN files f ON f.id = t.file_id "
                                 "WHERE c.batch_id = ? "
                                 "  AND c.confidence >= ? "
                                 "  AND c.confidence <= ?");

    if (filter.field.has_value()) {
        sql += QStringLiteral(" AND c.field = ?");
    }
    if (filter.status.has_value()) {
        sql += QStringLiteral(" AND c.status = ?");
    }
    sql += QStringLiteral(" ORDER BY c.id ASC");

    QSqlQuery q(conn);
    q.prepare(sql);
    q.addBindValue(batchId);
    q.addBindValue(filter.minConfidence);
    q.addBindValue(filter.maxConfidence);

    if (filter.field.has_value()) {
        q.addBindValue(tagFieldToColumn(*filter.field));
    }
    if (filter.status.has_value()) {
        q.addBindValue(correctionStatusToString(*filter.status));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(batchId),
        };
    }

    QList<CorrectionRow> rows;
    while (q.next()) {
        CorrectionRow row;
        row.id = q.value(0).toLongLong();
        row.batchId = q.value(1).toLongLong();
        row.trackId = q.value(2).toLongLong();
        row.field = tagFieldFromColumn(q.value(3).toString()).value_or(TagField::Title);
        row.oldValue = q.value(4).toString();
        row.newValue = q.value(5).toString();
        row.source
            = correctionSourceFromString(q.value(6).toString()).value_or(CorrectionSource::Rule);
        row.confidence = q.value(7).toDouble();
        row.reason = q.value(8).toString();
        row.status
            = correctionStatusFromString(q.value(9).toString()).value_or(CorrectionStatus::Pending);
        row.trackTitle = q.value(10).toString();
        row.filePath = q.value(11).toString();
        rows.append(row);
    }
    return rows;
}

core::Result<void> CorrectionStore::accept(const QList<qint64> &correctionIds)
{
    if (correctionIds.isEmpty()) {
        return { };
    }

    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        auto pendingRes = collectPendingCorrections(conn, correctionIds);
        if (!pendingRes.ok()) {
            return pendingRes.error();
        }
        const auto [pendingIds, affectedTrackIds] = pendingRes.value();
        if (pendingIds.isEmpty()) {
            return { };
        }

        QSqlQuery updateStmt(conn);
        updateStmt.prepare(
            QStringLiteral("UPDATE corrections SET status = 'accepted', decided_at = ? "
                           "WHERE id = ? AND status = 'pending'"));

        for (const qint64 id : pendingIds) {
            updateStmt.bindValue(0, now);
            updateStmt.bindValue(1, id);
            if (auto res = detail::execWrite(updateStmt, QString::number(id)); !res.ok()) {
                return res;
            }
        }

        return detail::relinkTracks(conn, affectedTrackIds.values());
    });
}

core::Result<void> CorrectionStore::reject(const QList<qint64> &correctionIds)
{
    if (correctionIds.isEmpty()) {
        return { };
    }

    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        auto pendingRes = collectPendingCorrections(conn, correctionIds);
        if (!pendingRes.ok()) {
            return pendingRes.error();
        }
        const auto [pendingIds, affectedTrackIds] = pendingRes.value();
        if (pendingIds.isEmpty()) {
            return { };
        }

        QSqlQuery updateStmt(conn);
        updateStmt.prepare(
            QStringLiteral("UPDATE corrections SET status = 'rejected', decided_at = ? "
                           "WHERE id = ? AND status = 'pending'"));

        for (const qint64 id : pendingIds) {
            updateStmt.bindValue(0, now);
            updateStmt.bindValue(1, id);
            if (auto res = detail::execWrite(updateStmt, QString::number(id)); !res.ok()) {
                return res;
            }
        }

        return detail::relinkTracks(conn, affectedTrackIds.values());
    });
}

core::Result<void> CorrectionStore::acceptEdited(qint64 correctionId, const QString &value)
{
    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        QSqlQuery selectStmt(conn);
        selectStmt.prepare(QStringLiteral("SELECT entity_id, status FROM corrections "
                                          "WHERE id = ? AND entity_type = 'track'"));
        selectStmt.bindValue(0, correctionId);
        if (!selectStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = selectStmt.lastError().text(),
                .detail = QString::number(correctionId),
            };
        }
        if (!selectStmt.next()) {
            return core::Error {
                .code = QString(errc::kCorrectionNotFound),
                .message = QStringLiteral("Correction not found"),
                .detail = QString::number(correctionId),
            };
        }

        const qint64 trackId = selectStmt.value(0).toLongLong();
        const QString currentStatus = selectStmt.value(1).toString();
        if (currentStatus != QStringLiteral("pending")) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("Only pending corrections can be accepted"),
                .detail = currentStatus,
            };
        }

        QSqlQuery updateStmt(conn);
        updateStmt.prepare(QStringLiteral("UPDATE corrections SET "
                                          "  new_value = ?, "
                                          "  source = 'user', "
                                          "  status = 'accepted', "
                                          "  decided_at = ? "
                                          "WHERE id = ? AND status = 'pending'"));
        updateStmt.bindValue(0, value);
        updateStmt.bindValue(1, now);
        updateStmt.bindValue(2, correctionId);

        if (auto res = detail::execWrite(updateStmt, QString::number(correctionId)); !res.ok()) {
            return res;
        }

        return detail::relinkTracks(conn, { trackId });
    });
}

core::Result<void> CorrectionStore::revertBatch(qint64 batchId)
{
    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        QSqlQuery batchCheck(conn);
        batchCheck.prepare(
            QStringLiteral("SELECT reverted_at FROM correction_batches WHERE id = ?"));
        batchCheck.addBindValue(batchId);
        if (!batchCheck.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = batchCheck.lastError().text(),
                .detail = QString::number(batchId),
            };
        }
        if (!batchCheck.next()) {
            return core::Error {
                .code = QString(errc::kCorrectionNotFound),
                .message = QStringLiteral("Correction batch not found"),
                .detail = QString::number(batchId),
            };
        }

        if (!batchCheck.value(0).isNull()) {
            return { };
        }

        QSqlQuery trackQuery(conn);
        trackQuery.prepare(
            QStringLiteral("SELECT DISTINCT entity_id FROM corrections "
                           "WHERE batch_id = ? AND status = 'accepted' AND entity_type = 'track'"));
        trackQuery.addBindValue(batchId);
        if (!trackQuery.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = trackQuery.lastError().text(),
                .detail = QString::number(batchId),
            };
        }

        QSet<qint64> affectedTrackIds;
        while (trackQuery.next()) {
            affectedTrackIds.insert(trackQuery.value(0).toLongLong());
        }

        QSqlQuery revertAccepted(conn);
        revertAccepted.prepare(QStringLiteral("UPDATE corrections SET status = 'reverted' "
                                              "WHERE batch_id = ? AND status = 'accepted'"));
        revertAccepted.addBindValue(batchId);
        if (auto res = detail::execWrite(revertAccepted, QString::number(batchId)); !res.ok()) {
            return res;
        }

        QSqlQuery rejectPending(conn);
        rejectPending.prepare(
            QStringLiteral("UPDATE corrections SET status = 'rejected', decided_at = ? "
                           "WHERE batch_id = ? AND status = 'pending'"));
        rejectPending.addBindValue(now);
        rejectPending.addBindValue(batchId);
        if (auto res = detail::execWrite(rejectPending, QString::number(batchId)); !res.ok()) {
            return res;
        }

        QSqlQuery updateBatch(conn);
        updateBatch.prepare(
            QStringLiteral("UPDATE correction_batches SET reverted_at = ? WHERE id = ?"));
        updateBatch.addBindValue(now);
        updateBatch.addBindValue(batchId);
        if (auto res = detail::execWrite(updateBatch, QString::number(batchId)); !res.ok()) {
            return res;
        }

        if (!affectedTrackIds.isEmpty()) {
            return detail::relinkTracks(conn, affectedTrackIds.values());
        }

        return { };
    });
}

} // namespace linernotes::library
