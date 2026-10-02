// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SessionSeeds.h"

#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <library/Database.h>
#include <rec/Errors.h>

#include <algorithm>
#include <cmath>

namespace linernotes::rec {

double calculateCompletionRate(qint64 playedMs, const QVariant &durationVar, bool completed)
{
    if (!durationVar.isNull() && durationVar.toLongLong() > 0) {
        const auto duration = static_cast<double>(durationVar.toLongLong());
        const auto played = static_cast<double>(playedMs);
        return std::clamp(played / duration, 0.0, 1.0);
    }
    return completed ? 1.0 : 0.5;
}

core::Result<QList<Seed>> sessionSeeds(
    library::Database &db, qint64 nowMs, std::optional<qint64> currentTrackId)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT track_id, played_ms, track_duration_ms, completed, skipped "
                             "FROM play_events "
                             "WHERE track_id IS NOT NULL AND started_at >= ? "
                             "ORDER BY started_at DESC, id DESC "
                             "LIMIT ?;"));
    q.addBindValue(nowMs - kSessionLookbackMs);
    q.addBindValue(kSessionMaxEvents);

    if (!q.exec()) {
        return core::Error {
            .code = QString::fromLatin1(errc::kRecDbFailed),
            .message = QStringLiteral("Failed to query recent play events for session seeds"),
            .detail = q.lastError().text(),
        };
    }

    QHash<qint64, double> trackWeights;
    int index = 0;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const qint64 playedMs = q.value(1).toLongLong();
        const auto durationVar = q.value(2);
        const bool completed = q.value(3).toInt() != 0;
        const bool skipped = q.value(4).toInt() != 0;

        const double c = calculateCompletionRate(playedMs, durationVar, completed);
        const double baseWeight = (skipped && c < 0.5) ? kSessionNegativeWeight : c;
        const double weight = baseWeight * std::pow(kSessionRecencyDecay, index);

        const double prevWeight = trackWeights.value(trackId, 0.0);
        trackWeights.insert(trackId, prevWeight + weight);
        ++index;
    }

    if (currentTrackId.has_value() && *currentTrackId > 0) {
        const qint64 curId = *currentTrackId;
        const double prevWeight = trackWeights.value(curId, 0.0);
        trackWeights.insert(curId, prevWeight + kSessionCurrentTrackWeight);
    }

    QList<Seed> seeds;
    seeds.reserve(trackWeights.size());
    for (auto it = trackWeights.cbegin(); it != trackWeights.cend(); ++it) {
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
