// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SimilarQuery.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/SmartRuleSql.h>
#include <nlq/Errors.h>
#include <nlq/QueryRunner.h>
#include <rec/Recommender.h>

#include <algorithm>
#include <utility>

namespace linernotes::nlq {

core::Result<qint64> resolveSimilarSeed(
    library::Database &db, const SimilarTo &similarTo, std::optional<qint64> currentTrackId)
{
    if (similarTo.current) {
        if (currentTrackId.has_value() && currentTrackId.value() > 0) {
            return currentTrackId.value();
        }
        return core::Error {
            .code = QString(errc::kSimilarSeedMissing),
            .message = QStringLiteral("Nothing is playing"),
            .detail = QString(),
        };
    }

    if (similarTo.titles.isEmpty()) {
        return core::Error {
            .code = QString(errc::kSimilarSeedNotFound),
            .message = QStringLiteral("Seed track not found: empty title"),
            .detail = QString(),
        };
    }

    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const QSqlDatabase &qDb = connRes.value();

    QStringList whereClauses;
    whereClauses.append(QStringLiteral("ts.visible = 1"));

    QList<QVariant> binds;

    QStringList titleClauses;
    for (const auto &t : similarTo.titles) {
        titleClauses.append(QStringLiteral("ts.title = ? COLLATE NOCASE"));
        binds.append(t);
    }
    whereClauses.append(QStringLiteral("(%1)").arg(titleClauses.join(QStringLiteral(" OR "))));

    if (!similarTo.artists.isEmpty()) {
        QStringList artistClauses;
        for (const auto &a : similarTo.artists) {
            artistClauses.append(QStringLiteral("COALESCE(ts.artist, '') LIKE ? ESCAPE '\\'"));
            binds.append(QStringLiteral("%%%1%%").arg(library::detail::escapeLikePattern(a)));
        }
        whereClauses.append(QStringLiteral("(%1)").arg(artistClauses.join(QStringLiteral(" OR "))));
    }

    const QString sql
        = QStringLiteral("SELECT ts.track_id FROM track_sort ts "
                         "LEFT JOIN track_play_stats tps ON tps.track_id = ts.track_id "
                         "WHERE %1 "
                         "ORDER BY COALESCE(tps.play_count, 0) DESC, ts.track_id ASC "
                         "LIMIT 1;")
              .arg(whereClauses.join(QStringLiteral(" AND ")));

    QSqlQuery q(qDb);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to resolve similar seed track"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return q.value(0).toLongLong();
    }

    return core::Error {
        .code = QString(errc::kSimilarSeedNotFound),
        .message = QStringLiteral("Seed track not found: %1").arg(similarTo.titles.first()),
        .detail = QString(),
    };
}

core::Result<QList<qint64>> runSimilarQuery(const QueryRunner &runner,
    rec::Recommender &recommender, const Query &query, qint64 seedTrackId)
{
    const auto valRes = query.validate();
    if (!valRes.ok()) {
        return valRes.error();
    }

    rec::RecRequest request;
    request.seeds = { rec::Seed { .trackId = seedTrackId, .weight = 1.0 } };
    request.count = 500;
    request.exclude = { };
    request.recentExcludeMs = 0;
    request.freshnessWeight = 0.0;
    request.randomness = 0.0;
    request.randomSeed = 0;
    request.maxPerArtist = 0;
    request.maxPerAlbum = 0;

    const auto recRes = recommender.recommend(request);
    if (!recRes.ok()) {
        return recRes.error();
    }
    QList<qint64> recIds = recRes.value();

    if (!query.rule.conditions.isEmpty() || query.rule.playedFrom.has_value()
        || query.rule.playedTo.has_value()) {
        const auto matchRes = runner.matchingTrackIds(query);
        if (!matchRes.ok()) {
            return matchRes.error();
        }

        const auto &allowed = matchRes.value();
        QList<qint64> filtered;
        filtered.reserve(recIds.size());
        for (const qint64 id : recIds) {
            if (allowed.contains(id)) {
                filtered.append(id);
            }
        }
        recIds = std::move(filtered);
    }

    if (recIds.size() > query.limit) {
        recIds = recIds.mid(0, query.limit);
    }

    return recIds;
}

} // namespace linernotes::nlq
