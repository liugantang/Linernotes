// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackCorrections.h"

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

namespace linernotes::library::detail {

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

} // namespace

core::Result<void> ensureBatchExists(const QSqlDatabase &conn, qint64 batchId)
{
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
    return { };
}

core::Result<void> addTrackProposals(Database &db, const core::Clock &clock, qint64 batchId,
    const QList<CorrectionProposal> &proposals, std::optional<double> autoAcceptThreshold)
{
    if (proposals.isEmpty()) {
        return { };
    }

    if (auto valRes = validateProposals(proposals, autoAcceptThreshold); !valRes.ok()) {
        return valRes;
    }

    const qint64 now = clock.nowMs();

    return inTransaction(db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto batchRes = ensureBatchExists(conn, batchId); !batchRes.ok()) {
            return batchRes;
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

            if (auto res = execWrite(insertStmt, QString::number(p.trackId)); !res.ok()) {
                return res;
            }
        }

        if (!affectedTrackIds.isEmpty()) {
            return relinkTracks(conn, affectedTrackIds.values());
        }

        return { };
    });
}

core::Result<QList<CorrectionRow>> fetchTrackCorrections(
    Database &db, qint64 batchId, const CorrectionFilter &filter)
{
    auto connRes = db.connection();
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
                                 "  f.path AS file_path, "
                                 "  t.id AS target_track_id "
                                 "FROM corrections c "
                                 "LEFT JOIN effective_metadata em ON em.track_id = c.entity_id "
                                 "LEFT JOIN tracks t ON t.id = c.entity_id "
                                 "LEFT JOIN files f ON f.id = t.file_id "
                                 "WHERE c.batch_id = ? AND c.entity_type = 'track' "
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
        const bool trackExists = !q.value(12).isNull();
        row.stale = (row.status == CorrectionStatus::Pending) && !trackExists;
        rows.append(row);
    }
    return rows;
}

core::Result<AcceptOutcome> acceptTrackCorrections(const QSqlDatabase &conn,
    const QList<qint64> &correctionIds, qint64 now, QSet<qint64> &affectedTrackIds)
{
    QSqlQuery selectStmt(conn);
    selectStmt.prepare(
        QStringLiteral("SELECT c.entity_id, t.id FROM corrections c "
                       "LEFT JOIN tracks t ON t.id = c.entity_id "
                       "WHERE c.id = ? AND c.status = 'pending' AND c.entity_type = 'track'"));

    QSqlQuery updateStmt(conn);
    updateStmt.prepare(QStringLiteral("UPDATE corrections SET status = 'accepted', decided_at = ? "
                                      "WHERE id = ? AND status = 'pending'"));

    AcceptOutcome outcome;

    for (const qint64 id : correctionIds) {
        selectStmt.bindValue(0, id);
        if (!selectStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = selectStmt.lastError().text(),
                .detail = QString::number(id),
            };
        }
        if (!selectStmt.next()) {
            continue;
        }

        const bool trackExists = !selectStmt.value(1).isNull();
        if (!trackExists) {
            ++outcome.skippedStale;
            continue;
        }

        const qint64 trackId = selectStmt.value(0).toLongLong();
        affectedTrackIds.insert(trackId);
        updateStmt.bindValue(0, now);
        updateStmt.bindValue(1, id);
        if (auto res = execWrite(updateStmt, QString::number(id)); !res.ok()) {
            return res.error();
        }
        ++outcome.accepted;
    }

    return outcome;
}

core::Result<void> acceptEditedTrackCorrection(
    Database &db, const core::Clock &clock, qint64 correctionId, const QString &value)
{
    const qint64 now = clock.nowMs();

    return inTransaction(db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        QSqlQuery selectStmt(conn);
        selectStmt.prepare(QStringLiteral("SELECT c.entity_id, c.status, t.id FROM corrections c "
                                          "LEFT JOIN tracks t ON t.id = c.entity_id "
                                          "WHERE c.id = ? AND c.entity_type = 'track'"));
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
        if (selectStmt.value(2).isNull()) {
            return core::Error {
                .code = QString(errc::kCorrectionNotFound),
                .message = QStringLiteral("Track not found"),
                .detail = QString::number(trackId),
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

        if (auto res = execWrite(updateStmt, QString::number(correctionId)); !res.ok()) {
            return res;
        }

        return relinkTracks(conn, { trackId });
    });
}

core::Result<void> revertTrackCorrections(
    const QSqlDatabase &conn, qint64 batchId, QSet<qint64> &affectedTrackIds)
{
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

    while (trackQuery.next()) {
        affectedTrackIds.insert(trackQuery.value(0).toLongLong());
    }

    return { };
}

} // namespace linernotes::library::detail
