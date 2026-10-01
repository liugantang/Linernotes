// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Relaxation.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

#include <library/Errors.h>

#include <algorithm>

namespace linernotes::nlq {

core::Result<EmptyResultAnalysis> analyzeEmptyResult(
    library::Database &db, const QueryRunner &runner, const Query &query)
{
    if (!db.isOpen()) {
        return core::Error {
            .code = QString(library::errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &qDb = connRes.value();

    EmptyResultAnalysis analysis;

    // 1. Query earliest play_events
    QSqlQuery q(qDb);
    if (!q.exec(QStringLiteral("SELECT MIN(started_at) FROM play_events;"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query play events"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next() && !q.value(0).isNull()) {
        const qint64 minStarted = q.value(0).toLongLong();
        analysis.firstPlayed
            = QDateTime::fromMSecsSinceEpoch(minStarted, QTimeZone::systemTimeZone()).date();
    }

    // 2. Check windowBeforeHistory
    if (analysis.firstPlayed.has_value() && query.rule.playedTo.has_value()) {
        if (query.rule.playedTo.value() < analysis.firstPlayed.value()) {
            analysis.windowBeforeHistory = true;
        }
    }

    // 3. Relaxations
    // 3.1 Try removing single condition (only when match != SmartMatch::Any)
    if (query.rule.match != library::SmartMatch::Any) {
        for (int i = 0; i < static_cast<int>(query.rule.conditions.size()); ++i) {
            Query relaxed = query;
            relaxed.rule.conditions.removeAt(i);
            const auto res = runner.run(relaxed);
            if (!res.ok()) {
                return res.error();
            }
            const auto count = static_cast<int>(res.value().size());
            if (count > 0) {
                analysis.relaxations.append(Relaxation {
                    .kind = RelaxKind::RemoveCondition,
                    .conditionIndex = i,
                    .resultCount = count,
                });
            }
        }
    }

    // 3.2 Try removing play window
    if (query.rule.playedFrom.has_value() || query.rule.playedTo.has_value()) {
        Query relaxed = query;
        relaxed.rule.playedFrom = std::nullopt;
        relaxed.rule.playedTo = std::nullopt;
        const auto res = runner.run(relaxed);
        if (!res.ok()) {
            return res.error();
        }
        const auto count = static_cast<int>(res.value().size());
        if (count > 0) {
            analysis.relaxations.append(Relaxation {
                .kind = RelaxKind::RemovePlayWindow,
                .conditionIndex = -1,
                .resultCount = count,
            });
        }
    }

    // 3.3 Sort relaxations by resultCount descending
    std::ranges::stable_sort(analysis.relaxations,
        [](const Relaxation &a, const Relaxation &b) { return a.resultCount > b.resultCount; });

    return analysis;
}

} // namespace linernotes::nlq
