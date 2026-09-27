// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "UsageStore.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <ai/AiEnumNames.h>
#include <ai/AiLogging.h>
#include <library/Errors.h>

#include <algorithm>
#include <type_traits>

namespace linernotes::ai {

UsageStore::UsageStore(library::Database &db)
    : m_db(db)
{
}

core::Result<void> UsageStore::record(const UsageRecord &record)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery query(connRes.value());
    query.prepare(QStringLiteral(
        "INSERT INTO llm_usage (created_at, purpose, service_id, model, prompt_tokens, "
        "completion_tokens, cached, ok, error_code, elapsed_ms) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    query.addBindValue(record.createdAtMs);
    query.addBindValue(purposeName(record.purpose));
    query.addBindValue(record.serviceId);
    query.addBindValue(record.model);
    query.addBindValue(record.usage.promptTokens);
    query.addBindValue(record.usage.completionTokens);
    query.addBindValue(record.cached ? 1 : 0);
    query.addBindValue(record.ok ? 1 : 0);
    if (record.ok || record.errorCode.isEmpty()) {
        query.addBindValue(QVariant(QMetaType::fromType<QString>()));
    } else {
        query.addBindValue(record.errorCode);
    }
    query.addBindValue(record.elapsedMs);

    if (!query.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = query.lastError().text(),
            .detail = query.lastQuery(),
        };
    }

    return { };
}

core::Result<QList<UsageSummary>> UsageStore::summarize(qint64 fromMs, qint64 toMs) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery query(connRes.value());
    query.prepare(QStringLiteral("SELECT purpose, model, "
                                 "SUM(CASE WHEN cached = 0 THEN 1 ELSE 0 END) AS requests, "
                                 "SUM(CASE WHEN cached = 1 THEN 1 ELSE 0 END) AS cache_hits, "
                                 "SUM(CASE WHEN ok = 0 THEN 1 ELSE 0 END) AS failures, "
                                 "SUM(prompt_tokens) AS prompt_tokens, "
                                 "SUM(completion_tokens) AS completion_tokens "
                                 "FROM llm_usage "
                                 "WHERE created_at >= ? AND created_at < ? "
                                 "GROUP BY purpose, model"));

    query.addBindValue(fromMs);
    query.addBindValue(toMs);

    if (!query.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = query.lastError().text(),
            .detail = query.lastQuery(),
        };
    }

    QList<UsageSummary> results;
    while (query.next()) {
        const QString purpStr = query.value(0).toString();
        const auto purpOpt = purposeFromName(purpStr);
        if (!purpOpt.has_value()) {
            continue;
        }

        const UsageSummary summary {
            .purpose = purpOpt.value(),
            .model = query.value(1).toString(),
            .requests = query.value(2).toInt(),
            .cacheHits = query.value(3).toInt(),
            .failures = query.value(4).toInt(),
            .promptTokens = query.value(5).toLongLong(),
            .completionTokens = query.value(6).toLongLong(),
        };
        results.append(summary);
    }

    std::ranges::sort(results, [](const UsageSummary &a, const UsageSummary &b) {
        if (a.purpose != b.purpose) {
            return static_cast<std::underlying_type_t<Purpose>>(a.purpose)
                < static_cast<std::underlying_type_t<Purpose>>(b.purpose);
        }
        return a.model < b.model;
    });

    return results;
}

} // namespace linernotes::ai
