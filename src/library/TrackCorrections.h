// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>

#include <core/Result.h>
#include <library/CorrectionStore.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

namespace detail {

core::Result<void> ensureBatchExists(const QSqlDatabase &conn, qint64 batchId);

core::Result<std::pair<QList<qint64>, QList<qint64>>> splitPendingByEntity(
    const QSqlDatabase &conn, const QList<qint64> &ids);

core::Result<void> addTrackProposals(Database &db, const core::Clock &clock, qint64 batchId,
    const QList<CorrectionProposal> &proposals, std::optional<double> autoAcceptThreshold);

core::Result<QList<CorrectionRow>> fetchTrackCorrections(
    Database &db, qint64 batchId, const CorrectionFilter &filter);

core::Result<void> acceptTrackCorrections(const QSqlDatabase &conn,
    const QList<qint64> &correctionIds, qint64 now, QSet<qint64> &affectedTrackIds);

core::Result<void> acceptEditedTrackCorrection(
    Database &db, const core::Clock &clock, qint64 correctionId, const QString &value);

core::Result<void> revertTrackCorrections(
    const QSqlDatabase &conn, qint64 batchId, QSet<qint64> &affectedTrackIds);

} // namespace detail

} // namespace linernotes::library
