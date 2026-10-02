// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TasteSeeds.h"

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <library/Database.h>
#include <rec/Errors.h>
#include <rec/SessionSeeds.h>

#include <algorithm>
#include <cmath>

namespace linernotes::rec {

namespace {

struct PlaySeedCandidate {
    qint64 trackId = 0;
    double weight = 0.0;
};

} // namespace

core::Result<QList<Seed>> tasteSeeds(library::Database &db, qint64 nowMs)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT track_id, started_at, played_ms, track_duration_ms, completed, skipped "
        "FROM play_events "
        "WHERE track_id IS NOT NULL AND started_at >= ? "
        "ORDER BY started_at DESC, id DESC;"));
    q.addBindValue(nowMs - kTasteLookbackMs);

    if (!q.exec()) {
        return core::Error {
            .code = QString::fromLatin1(errc::kRecDbFailed),
            .message = QStringLiteral("Failed to query play events for taste seeds"),
            .detail = q.lastError().text(),
        };
    }

    constexpr double kMsPerDay = 24.0 * 3600.0 * 1000.0;
    QHash<qint64, double> playWeights;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const qint64 startedAt = q.value(1).toLongLong();
        const qint64 playedMs = q.value(2).toLongLong();
        const auto durationVar = q.value(3);
        const bool completed = q.value(4).toInt() != 0;
        const bool skipped = q.value(5).toInt() != 0;

        const double c = calculateCompletionRate(playedMs, durationVar, completed);
        if (skipped && c < 0.5) {
            continue;
        }

        const double daysAgo
            = static_cast<double>(std::max<qint64>(0, nowMs - startedAt)) / kMsPerDay;
        const double decay = std::pow(0.5, daysAgo / kTasteHalfLifeDays);
        const double weight = c * decay;

        const double prevWeight = playWeights.value(trackId, 0.0);
        playWeights.insert(trackId, prevWeight + weight);
    }

    QList<PlaySeedCandidate> playCandidates;
    playCandidates.reserve(playWeights.size());
    for (auto it = playWeights.cbegin(); it != playWeights.cend(); ++it) {
        if (it.value() > 1e-9) {
            playCandidates.append(PlaySeedCandidate {
                .trackId = it.key(),
                .weight = it.value(),
            });
        }
    }

    std::ranges::sort(playCandidates, [](const PlaySeedCandidate &a, const PlaySeedCandidate &b) {
        if (std::abs(a.weight - b.weight) > 1e-9) {
            return a.weight > b.weight;
        }
        return a.trackId < b.trackId;
    });

    if (playCandidates.size() > kTasteMaxPlaySeeds) {
        playCandidates.resize(kTasteMaxPlaySeeds);
    }

    QHash<qint64, double> combinedWeights;
    for (const auto &cand : playCandidates) {
        combinedWeights.insert(cand.trackId, cand.weight);
    }

    QSqlQuery qFav(conn);
    qFav.prepare(QStringLiteral("SELECT entity_id "
                                "FROM favorites "
                                "WHERE entity_type = 'track' "
                                "ORDER BY created_at DESC, entity_id DESC "
                                "LIMIT ?;"));
    qFav.addBindValue(kTasteMaxFavoriteSeeds);

    if (!qFav.exec()) {
        return core::Error {
            .code = QString::fromLatin1(errc::kRecDbFailed),
            .message = QStringLiteral("Failed to query favorites for taste seeds"),
            .detail = qFav.lastError().text(),
        };
    }

    while (qFav.next()) {
        const qint64 trackId = qFav.value(0).toLongLong();
        const double prev = combinedWeights.value(trackId, 0.0);
        combinedWeights.insert(trackId, prev + kTasteFavoriteWeight);
    }

    QList<Seed> seeds;
    seeds.reserve(combinedWeights.size());
    for (auto it = combinedWeights.cbegin(); it != combinedWeights.cend(); ++it) {
        if (std::abs(it.value()) > 1e-9) {
            seeds.append(Seed {
                .trackId = it.key(),
                .weight = it.value(),
            });
        }
    }

    return seeds;
}

} // namespace linernotes::rec
