// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CorrectionStore.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <core/Clock.h>
#include <library/ArtistAliasCorrections.h>
#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/MetadataRefresh.h>
#include <library/SearchIndex.h>
#include <library/TrackCorrections.h>

#include <utility>

namespace linernotes::library {

namespace detail {

core::Result<std::pair<QList<qint64>, QList<qint64>>> splitPendingByEntity(
    const QSqlDatabase &conn, const QList<qint64> &ids)
{
    QSqlQuery findStmt(conn);
    findStmt.prepare(QStringLiteral(
        "SELECT id, entity_type FROM corrections WHERE id = ? AND status = 'pending'"));

    QList<qint64> trackCorrectionIds;
    QList<qint64> artistCorrectionIds;

    for (const qint64 id : ids) {
        findStmt.bindValue(0, id);
        if (!findStmt.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = findStmt.lastError().text(),
                .detail = QString::number(id),
            };
        }
        if (findStmt.next()) {
            const QString entityType = findStmt.value(1).toString();
            if (entityType == QStringLiteral("track")) {
                trackCorrectionIds.append(id);
            } else if (entityType == QStringLiteral("artist")) {
                artistCorrectionIds.append(id);
            }
        }
    }

    return std::make_pair(trackCorrectionIds, artistCorrectionIds);
}

} // namespace detail

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
    return detail::addTrackProposals(m_db, m_clock, batchId, proposals, autoAcceptThreshold);
}

core::Result<void> CorrectionStore::addArtistAliasProposals(qint64 batchId,
    const QList<ArtistAliasProposal> &proposals, std::optional<double> autoAcceptThreshold)
{
    return detail::addArtistAliasProposals(m_db, m_clock, batchId, proposals, autoAcceptThreshold);
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
    return detail::fetchTrackCorrections(m_db, batchId, filter);
}

core::Result<QList<ArtistAliasRow>> CorrectionStore::artistAliasCorrections(qint64 batchId) const
{
    return detail::fetchArtistAliasCorrections(m_db, batchId);
}

core::Result<AcceptOutcome> CorrectionStore::accept(const QList<qint64> &correctionIds)
{
    if (correctionIds.isEmpty()) {
        return AcceptOutcome { };
    }

    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(
        m_db, [&](const QSqlDatabase &conn) -> core::Result<AcceptOutcome> {
            auto splitRes = detail::splitPendingByEntity(conn, correctionIds);
            if (!splitRes.ok()) {
                return splitRes.error();
            }

            const auto &[trackCorrectionIds, artistCorrectionIds] = splitRes.value();

            if (trackCorrectionIds.isEmpty() && artistCorrectionIds.isEmpty()) {
                return AcceptOutcome { };
            }

            QSet<qint64> affectedTrackIds;
            AcceptOutcome outcome;

            if (!trackCorrectionIds.isEmpty()) {
                auto res = detail::acceptTrackCorrections(
                    conn, trackCorrectionIds, now, affectedTrackIds);
                if (!res.ok()) {
                    return res.error();
                }
                outcome.accepted += res.value().accepted;
                outcome.skippedStale += res.value().skippedStale;
            }

            if (!artistCorrectionIds.isEmpty()) {
                auto res = detail::acceptArtistAliasCorrections(
                    conn, artistCorrectionIds, now, affectedTrackIds);
                if (!res.ok()) {
                    return res.error();
                }
                outcome.accepted += res.value().accepted;
                outcome.skippedStale += res.value().skippedStale;
            }

            if (!affectedTrackIds.isEmpty()) {
                if (auto res = detail::relinkTracks(conn, affectedTrackIds.values()); !res.ok()) {
                    return res.error();
                }
            }

            SearchIndex searchIndex(conn);
            if (auto res = searchIndex.flushDirty(); !res.ok()) {
                return res.error();
            }

            return outcome;
        });
}

core::Result<void> CorrectionStore::reject(const QList<qint64> &correctionIds)
{
    if (correctionIds.isEmpty()) {
        return { };
    }

    const qint64 now = m_clock.nowMs();

    return detail::inTransaction(m_db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        QSqlQuery updateStmt(conn);
        updateStmt.prepare(
            QStringLiteral("UPDATE corrections SET status = 'rejected', decided_at = ? "
                           "WHERE id = ? AND status = 'pending'"));

        for (const qint64 id : correctionIds) {
            updateStmt.bindValue(0, now);
            updateStmt.bindValue(1, id);
            if (auto res = detail::execWrite(updateStmt, QString::number(id)); !res.ok()) {
                return res;
            }
        }

        return { };
    });
}

core::Result<void> CorrectionStore::acceptEdited(qint64 correctionId, const QString &value)
{
    return detail::acceptEditedTrackCorrection(m_db, m_clock, correctionId, value);
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

        QSet<qint64> affectedTrackIds;

        if (auto res = detail::revertTrackCorrections(conn, batchId, affectedTrackIds); !res.ok()) {
            return res;
        }

        if (auto res = detail::revertArtistAliasCorrections(conn, batchId, affectedTrackIds);
            !res.ok()) {
            return res;
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

        SearchIndex searchIndex(conn);
        if (auto res = searchIndex.flushDirty(); !res.ok()) {
            return res.error();
        }

        return { };
    });
}

} // namespace linernotes::library
