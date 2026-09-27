// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayCountRule.h"

#include <QSqlQuery>

#include <algorithm>

namespace linernotes::library {

bool PlayCountRule::counts(qint64 playedMs, std::optional<qint64> durationMs) const
{
    const auto norm = normalized();
    if (norm.minSeconds > 0 && playedMs >= static_cast<qint64>(norm.minSeconds) * 1000) {
        return true;
    }
    if (durationMs.has_value() && *durationMs > 0) {
        return playedMs * 100 >= static_cast<qint64>(norm.minPercent) * (*durationMs);
    }
    return false;
}

QString PlayCountRule::sqlCondition()
{
    return QStringLiteral(
        "((:count_min_ms > 0 AND played_ms >= :count_min_ms) OR (track_duration_ms IS NOT NULL AND "
        "track_duration_ms > 0 AND played_ms * 100 >= :count_min_percent * track_duration_ms))");
}

void PlayCountRule::bindSql(QSqlQuery &query) const
{
    const auto norm = normalized();
    query.bindValue(QStringLiteral(":count_min_ms"), static_cast<qint64>(norm.minSeconds) * 1000);
    query.bindValue(QStringLiteral(":count_min_percent"), norm.minPercent);
}

PlayCountRule PlayCountRule::normalized() const
{
    PlayCountRule rule;
    rule.minPercent = std::clamp(minPercent, 1, 100);
    rule.minSeconds = std::clamp(minSeconds, 0, 3600);
    return rule;
}

} // namespace linernotes::library
