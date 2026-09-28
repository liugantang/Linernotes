// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

/// CorrectionStore::accept 的结果。
struct AcceptOutcome {
    int accepted = 0;
    int skippedStale = 0; // 目标已不存在而跳过的条数，这些条目保持 pending
    bool operator==(const AcceptOutcome &other) const = default;
};

struct ArtistAliasProposal {
    qint64 canonicalArtistId = 0;
    QString alias { }; // 变体写法或其他语言名
    std::optional<QString> locale;
    CorrectionSource source = CorrectionSource::Rule;
    double confidence = 0.0;
    QString reason { };

    bool operator==(const ArtistAliasProposal &other) const = default;
};

struct ArtistAliasRow {
    qint64 id = 0;
    qint64 batchId = 0;
    qint64 artistId = 0;
    QString artistName { }; // 规范艺人当前的 name（艺人已不存在时为空）
    QString alias { };
    std::optional<QString> locale;
    CorrectionSource source = CorrectionSource::Rule;
    double confidence = 0.0;
    QString reason { };
    CorrectionStatus status = CorrectionStatus::Pending;
    bool stale = false;

    bool operator==(const ArtistAliasRow &other) const = default;
};

namespace detail {

core::Result<void> addArtistAliasProposals(Database &db, const core::Clock &clock, qint64 batchId,
    const QList<ArtistAliasProposal> &proposals, std::optional<double> autoAcceptThreshold);

core::Result<QList<ArtistAliasRow>> fetchArtistAliasCorrections(Database &db, qint64 batchId);

core::Result<AcceptOutcome> acceptArtistAliasCorrections(const QSqlDatabase &conn,
    const QList<qint64> &correctionIds, qint64 now, QSet<qint64> &affectedTrackIds);

core::Result<void> revertArtistAliasCorrections(
    const QSqlDatabase &conn, qint64 batchId, QSet<qint64> &affectedTrackIds);

} // namespace detail

} // namespace linernotes::library
