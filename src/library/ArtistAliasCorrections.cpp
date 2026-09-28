// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistAliasCorrections.h"

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
#include <library/SearchIndex.h>
#include <library/TrackCorrections.h>

#include <cmath>

namespace linernotes::library::detail {

namespace {

core::Result<void> validateAliasProposals(const QSqlDatabase &conn,
    const QList<ArtistAliasProposal> &proposals, const std::optional<double> &autoAcceptThreshold)
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

    QSqlQuery check(conn);
    check.prepare(QStringLiteral("SELECT 1 FROM artists WHERE id = ?"));

    for (const auto &p : proposals) {
        if (p.canonicalArtistId <= 0 || p.alias.trimmed().isEmpty() || std::isnan(p.confidence)
            || p.confidence < 0.0 || p.confidence > 1.0) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("Invalid artist alias proposal"),
                .detail = p.alias,
            };
        }
        check.bindValue(0, p.canonicalArtistId);
        if (!check.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = check.lastError().text(),
                .detail = QString::number(p.canonicalArtistId),
            };
        }
        if (!check.next()) {
            return core::Error {
                .code = QString(errc::kCorrectionInvalid),
                .message = QStringLiteral("Canonical artist not found"),
                .detail = QString::number(p.canonicalArtistId),
            };
        }
    }
    return { };
}

core::Result<void> collectArtistTracks(
    const QSqlDatabase &conn, qint64 artistId, QSet<qint64> &affectedTrackIds)
{
    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("SELECT DISTINCT track_id FROM track_artists WHERE artist_id = ? "
                       "UNION "
                       "SELECT DISTINCT t.id FROM tracks t "
                       "JOIN album_artists aa ON t.album_id = aa.album_id WHERE aa.artist_id = ?"));
    q.addBindValue(artistId);
    q.addBindValue(artistId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(artistId),
        };
    }
    while (q.next()) {
        affectedTrackIds.insert(q.value(0).toLongLong());
    }
    return { };
}

core::Result<void> migrateVariantArtists(const QSqlDatabase &conn, qint64 canonicalArtistId,
    const QString &alias, QSet<qint64> &affectedTrackIds)
{
    QSqlQuery findVariant(conn);
    findVariant.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ? AND id != ?"));
    findVariant.addBindValue(alias);
    findVariant.addBindValue(canonicalArtistId);
    if (!findVariant.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = findVariant.lastError().text(),
            .detail = alias,
        };
    }

    QSqlQuery moveFavs(conn);
    moveFavs.prepare(
        QStringLiteral("INSERT OR IGNORE INTO favorites (entity_type, entity_id, created_at) "
                       "SELECT 'artist', ?, created_at FROM favorites WHERE entity_type = 'artist' "
                       "AND entity_id = ?"));

    QSqlQuery moveAliases(conn);
    moveAliases.prepare(
        QStringLiteral("UPDATE OR IGNORE artist_aliases SET artist_id = ? WHERE artist_id = ?"));

    while (findVariant.next()) {
        const qint64 variantId = findVariant.value(0).toLongLong();
        moveFavs.bindValue(0, canonicalArtistId);
        moveFavs.bindValue(1, variantId);
        if (auto res = execWrite(moveFavs, QString::number(variantId)); !res.ok()) {
            return res;
        }

        moveAliases.bindValue(0, canonicalArtistId);
        moveAliases.bindValue(1, variantId);
        if (auto res = execWrite(moveAliases, QString::number(variantId)); !res.ok()) {
            return res;
        }

        if (auto res = collectArtistTracks(conn, variantId, affectedTrackIds); !res.ok()) {
            return res;
        }
    }
    return { };
}

core::Result<void> executeAcceptArtistAlias(const QSqlDatabase &conn, qint64 canonicalArtistId,
    const QString &alias, const std::optional<QString> &locale, CorrectionSource source, qint64 now,
    QSet<qint64> &affectedTrackIds)
{
    QSqlQuery checkArtist(conn);
    checkArtist.prepare(QStringLiteral("SELECT 1 FROM artists WHERE id = ?"));
    checkArtist.addBindValue(canonicalArtistId);
    if (!checkArtist.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = checkArtist.lastError().text(),
            .detail = QString::number(canonicalArtistId),
        };
    }
    if (!checkArtist.next()) {
        return core::Error {
            .code = QString(errc::kCorrectionNotFound),
            .message = QStringLiteral("Canonical artist not found"),
            .detail = QString::number(canonicalArtistId),
        };
    }

    const QString kind = (locale.has_value() && !locale->isEmpty()) ? QStringLiteral("translation")
                                                                    : QStringLiteral("variant");
    const QString srcStr = correctionSourceToString(source);

    QSqlQuery insertAlias(conn);
    insertAlias.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO artist_aliases (artist_id, alias, locale, kind, source, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    insertAlias.addBindValue(canonicalArtistId);
    insertAlias.addBindValue(alias);
    insertAlias.addBindValue(
        locale.has_value() ? QVariant(*locale) : QVariant(QMetaType(QMetaType::QString)));
    insertAlias.addBindValue(kind);
    insertAlias.addBindValue(srcStr);
    insertAlias.addBindValue(now);

    if (auto res = execWrite(insertAlias, alias); !res.ok()) {
        return res;
    }

    return migrateVariantArtists(conn, canonicalArtistId, alias, affectedTrackIds);
}

core::Result<bool> shouldSkipProposal(qint64 artistId, const QString &alias, QSqlQuery &nameStmt,
    QSqlQuery &corrStmt, QSqlQuery &aliasStmt)
{
    nameStmt.bindValue(0, artistId);
    if (!nameStmt.exec() || !nameStmt.next()) {
        return core::Error {
            .code = QString(errc::kCorrectionInvalid),
            .message = QStringLiteral("Canonical artist not found"),
            .detail = QString::number(artistId),
        };
    }
    if (alias == nameStmt.value(0).toString()) {
        return true;
    }

    corrStmt.bindValue(0, artistId);
    corrStmt.bindValue(1, alias);
    if (!corrStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = corrStmt.lastError().text(),
            .detail = alias,
        };
    }
    if (corrStmt.next()) {
        return true;
    }

    aliasStmt.bindValue(0, artistId);
    aliasStmt.bindValue(1, alias);
    if (!aliasStmt.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = aliasStmt.lastError().text(),
            .detail = alias,
        };
    }
    return aliasStmt.next();
}

core::Result<void> insertArtistCorrection(QSqlQuery &stmt, qint64 batchId,
    const ArtistAliasProposal &p, const QString &alias, bool autoAccept, qint64 now)
{
    const CorrectionStatus status
        = autoAccept ? CorrectionStatus::Accepted : CorrectionStatus::Pending;
    stmt.bindValue(0, p.canonicalArtistId);
    stmt.bindValue(1, alias);
    stmt.bindValue(
        2, p.locale.has_value() ? QVariant(*p.locale) : QVariant(QMetaType(QMetaType::QString)));
    stmt.bindValue(3, correctionSourceToString(p.source));
    stmt.bindValue(4, p.confidence);
    stmt.bindValue(5, correctionStatusToString(status));
    stmt.bindValue(6, p.reason);
    stmt.bindValue(7, batchId);
    stmt.bindValue(8, now);
    stmt.bindValue(9, autoAccept ? QVariant(now) : QVariant(QMetaType(QMetaType::LongLong)));
    return execWrite(stmt, alias);
}

core::Result<void> processAliasProposal(const QSqlDatabase &conn, qint64 batchId,
    const ArtistAliasProposal &p, std::optional<double> autoAcceptThreshold, qint64 now,
    QSet<QPair<qint64, QString>> &seenProposals, QSqlQuery &nameStmt, QSqlQuery &corrStmt,
    QSqlQuery &aliasStmt, QSqlQuery &insertCorrStmt, QSet<qint64> &affectedTrackIds)
{
    const QString alias = p.alias.trimmed();
    const auto pair = qMakePair(p.canonicalArtistId, alias);
    if (seenProposals.contains(pair)) {
        return { };
    }
    seenProposals.insert(pair);

    auto skipRes = shouldSkipProposal(p.canonicalArtistId, alias, nameStmt, corrStmt, aliasStmt);
    if (!skipRes.ok()) {
        return skipRes.error();
    }
    if (skipRes.value()) {
        return { };
    }

    const bool autoAccept
        = autoAcceptThreshold.has_value() && (p.confidence >= *autoAcceptThreshold);

    if (auto res = insertArtistCorrection(insertCorrStmt, batchId, p, alias, autoAccept, now);
        !res.ok()) {
        return res;
    }

    if (autoAccept) {
        if (auto res = executeAcceptArtistAlias(
                conn, p.canonicalArtistId, alias, p.locale, p.source, now, affectedTrackIds);
            !res.ok()) {
            return res;
        }
    }

    return { };
}

} // namespace

core::Result<void> addArtistAliasProposals(Database &db, const core::Clock &clock, qint64 batchId,
    const QList<ArtistAliasProposal> &proposals, std::optional<double> autoAcceptThreshold)
{
    if (proposals.isEmpty()) {
        return { };
    }

    const qint64 now = clock.nowMs();

    return inTransaction(db, [&](const QSqlDatabase &conn) -> core::Result<void> {
        if (auto valRes = validateAliasProposals(conn, proposals, autoAcceptThreshold);
            !valRes.ok()) {
            return valRes;
        }

        if (auto batchRes = ensureBatchExists(conn, batchId); !batchRes.ok()) {
            return batchRes;
        }

        QSqlQuery nameStmt(conn);
        nameStmt.prepare(QStringLiteral("SELECT name FROM artists WHERE id = ?"));

        QSqlQuery corrStmt(conn);
        corrStmt.prepare(QStringLiteral(
            "SELECT 1 FROM corrections WHERE entity_type = 'artist' AND entity_id = ? "
            "AND field = 'alias' AND new_value = ? AND status IN ('pending', 'accepted') LIMIT 1"));

        QSqlQuery aliasStmt(conn);
        aliasStmt.prepare(QStringLiteral(
            "SELECT 1 FROM artist_aliases WHERE artist_id = ? AND alias = ? LIMIT 1"));

        QSqlQuery insertCorrStmt(conn);
        insertCorrStmt.prepare(
            QStringLiteral("INSERT INTO corrections ("
                           "  entity_type, entity_id, field, old_value, new_value, locale, "
                           "  source, confidence, status, reason, batch_id, created_at, decided_at"
                           ") VALUES ("
                           "  'artist', ?, 'alias', NULL, ?, ?, "
                           "  ?, ?, ?, ?, ?, ?, ?"
                           ")"));

        QSet<qint64> affectedTrackIds;
        QSet<QPair<qint64, QString>> seenProposals;

        for (const auto &p : proposals) {
            if (auto res = processAliasProposal(conn, batchId, p, autoAcceptThreshold, now,
                    seenProposals, nameStmt, corrStmt, aliasStmt, insertCorrStmt, affectedTrackIds);
                !res.ok()) {
                return res;
            }
        }

        if (!affectedTrackIds.isEmpty()) {
            return relinkTracks(conn, affectedTrackIds.values());
        }

        SearchIndex searchIndex(conn);
        if (auto res = searchIndex.flushDirty(); !res.ok()) {
            return res.error();
        }

        return { };
    });
}

core::Result<QList<ArtistAliasRow>> fetchArtistAliasCorrections(Database &db, qint64 batchId)
{
    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT c.id, c.batch_id, c.entity_id, a.name, c.new_value, c.locale, "
                             "c.source, c.confidence, c.reason, c.status, a.id FROM corrections c "
                             "LEFT JOIN artists a ON a.id = c.entity_id "
                             "WHERE c.batch_id = ? AND c.entity_type = 'artist' AND c.field = "
                             "'alias' ORDER BY c.id ASC"));
    q.addBindValue(batchId);

    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(batchId),
        };
    }

    QList<ArtistAliasRow> rows;
    while (q.next()) {
        const auto status
            = correctionStatusFromString(q.value(9).toString()).value_or(CorrectionStatus::Pending);
        const bool artistExists = !q.value(10).isNull();
        rows.append(ArtistAliasRow {
            .id = q.value(0).toLongLong(),
            .batchId = q.value(1).toLongLong(),
            .artistId = q.value(2).toLongLong(),
            .artistName = q.value(3).toString(),
            .alias = q.value(4).toString(),
            .locale
            = q.value(5).isNull() ? std::nullopt : std::optional<QString>(q.value(5).toString()),
            .source
            = correctionSourceFromString(q.value(6).toString()).value_or(CorrectionSource::Rule),
            .confidence = q.value(7).toDouble(),
            .reason = q.value(8).toString(),
            .status = status,
            .stale = (status == CorrectionStatus::Pending) && !artistExists,
        });
    }
    return rows;
}

core::Result<AcceptOutcome> acceptArtistAliasCorrections(const QSqlDatabase &conn,
    const QList<qint64> &correctionIds, qint64 now, QSet<qint64> &affectedTrackIds)
{
    QSqlQuery selectStmt(conn);
    selectStmt.prepare(QStringLiteral(
        "SELECT c.entity_id, c.new_value, c.locale, c.source, a.id FROM corrections c "
        "LEFT JOIN artists a ON a.id = c.entity_id "
        "WHERE c.id = ? AND c.status = 'pending' AND c.entity_type = 'artist' AND field = "
        "'alias'"));

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

        const bool artistExists = !selectStmt.value(4).isNull();
        if (!artistExists) {
            ++outcome.skippedStale;
            continue;
        }

        const qint64 canonicalArtistId = selectStmt.value(0).toLongLong();
        const QString alias = selectStmt.value(1).toString();
        const auto locale = selectStmt.value(2).isNull()
            ? std::nullopt
            : std::optional<QString>(selectStmt.value(2).toString());
        const auto source = correctionSourceFromString(selectStmt.value(3).toString())
                                .value_or(CorrectionSource::Rule);

        if (auto res = executeAcceptArtistAlias(
                conn, canonicalArtistId, alias, locale, source, now, affectedTrackIds);
            !res.ok()) {
            return res.error();
        }

        updateStmt.bindValue(0, now);
        updateStmt.bindValue(1, id);
        if (auto res = execWrite(updateStmt, QString::number(id)); !res.ok()) {
            return res.error();
        }
        ++outcome.accepted;
    }

    return outcome;
}

core::Result<void> revertArtistAliasCorrections(
    const QSqlDatabase &conn, qint64 batchId, QSet<qint64> &affectedTrackIds)
{
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT entity_id, new_value FROM corrections "
                             "WHERE batch_id = ? AND status = 'accepted' AND entity_type = "
                             "'artist' AND field = 'alias'"));
    q.addBindValue(batchId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(batchId),
        };
    }

    QSqlQuery checkArtist(conn);
    checkArtist.prepare(QStringLiteral("SELECT 1 FROM artists WHERE id = ?"));

    QSqlQuery deleteAlias(conn);
    deleteAlias.prepare(
        QStringLiteral("DELETE FROM artist_aliases WHERE artist_id = ? AND alias = ?"));

    while (q.next()) {
        const qint64 canonicalArtistId = q.value(0).toLongLong();
        const QString alias = q.value(1).toString();

        checkArtist.bindValue(0, canonicalArtistId);
        if (!checkArtist.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = checkArtist.lastError().text(),
                .detail = QString::number(canonicalArtistId),
            };
        }

        // Known limitation: favorites and aliases migrated during accept are not moved back upon
        // revert.
        if (!checkArtist.next()) {
            continue;
        }

        deleteAlias.bindValue(0, canonicalArtistId);
        deleteAlias.bindValue(1, alias);
        if (auto res = execWrite(deleteAlias, alias); !res.ok()) {
            return res;
        }

        if (auto res = collectArtistTracks(conn, canonicalArtistId, affectedTrackIds); !res.ok()) {
            return res;
        }
    }

    return { };
}

} // namespace linernotes::library::detail
