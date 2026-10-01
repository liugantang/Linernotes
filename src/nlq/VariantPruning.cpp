// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "VariantPruning.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Errors.h>
#include <library/SmartRule.h>
#include <library/SmartRuleSql.h>
#include <nlq/Errors.h>

#include <iterator>
#include <optional>
#include <utility>

namespace linernotes::nlq {

namespace {

core::Result<int> countMatchingTracks(
    const QSqlDatabase &qDb, const library::SmartCondition &cond, const QString &variant)
{
    library::SmartCondition singleCond = cond;
    singleCond.value = variant;
    singleCond.value2 = QVariant();

    library::SmartRule singleRule;
    singleRule.conditions = { singleCond };

    QList<QVariant> binds;
    const QString whereSql = library::detail::buildSmartRuleWhereSql(singleRule, binds);
    const QString sql
        = QStringLiteral("SELECT COUNT(*) FROM track_sort ts WHERE ts.visible = 1%1").arg(whereSql);

    QSqlQuery q(qDb);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to count variants for text condition"),
            .detail = q.lastError().text(),
        };
    }

    if (q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

bool isPrunableCondition(const library::SmartCondition &cond)
{
    if (library::smartFieldKind(cond.field) != library::SmartFieldKind::Text) {
        return false;
    }
    if (cond.op != library::SmartOp::Contains && cond.op != library::SmartOp::Is
        && cond.op != library::SmartOp::StartsWith) {
        return false;
    }
    return library::isSmartTextList(cond.value);
}

core::Result<std::optional<PrunedVariants>> pruneCondition(
    const QSqlDatabase &qDb, library::SmartCondition &cond, int conditionIndex)
{
    if (!isPrunableCondition(cond)) {
        return std::optional<PrunedVariants> { };
    }

    const QStringList variants = library::smartTextValues(cond.value);
    if (variants.size() <= 1) {
        return std::optional<PrunedVariants> { };
    }

    QStringList kept;
    QStringList dropped;
    for (const auto &v : variants) {
        auto countRes = countMatchingTracks(qDb, cond, v);
        if (!countRes.ok()) {
            return countRes.error();
        }
        if (countRes.value() > 0) {
            kept.append(v);
        } else {
            dropped.append(v);
        }
    }

    if (kept.isEmpty() || dropped.isEmpty()) {
        return std::optional<PrunedVariants> { };
    }

    cond.value = kept;
    return std::optional<PrunedVariants>(PrunedVariants {
        .conditionIndex = conditionIndex,
        .kept = std::move(kept),
        .dropped = std::move(dropped),
    });
}

} // namespace

core::Result<Query> pruneTextVariants(
    library::Database &db, const Query &query, QList<PrunedVariants> *report)
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
    const QSqlDatabase &qDb = connRes.value();

    Query updatedQuery = query;

    for (int i = 0; i < updatedQuery.rule.conditions.size(); ++i) {
        auto pruneRes = pruneCondition(qDb, *std::next(updatedQuery.rule.conditions.begin(), i), i);
        if (!pruneRes.ok()) {
            return pruneRes.error();
        }
        const auto &pruned = pruneRes.value();
        if (pruned.has_value() && report != nullptr) {
            report->append(pruned.value());
        }
    }

    return updatedQuery;
}

} // namespace linernotes::nlq
