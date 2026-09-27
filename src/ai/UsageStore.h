// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <core/Result.h>
#include <library/Database.h>

#include <cstdint>

namespace linernotes::ai {

struct UsageRecord {
    qint64 createdAtMs = 0;
    Purpose purpose = Purpose::Query;
    QString serviceId;
    QString model;
    TokenUsage usage;
    bool cached = false;
    bool ok = true;
    QString errorCode; // ok 为 true 时为空
    qint64 elapsedMs = 0;
};

struct UsageSummary {
    Purpose purpose = Purpose::Query;
    QString model;
    int requests = 0; // 实际发出的请求（cached=0 的行数）
    int cacheHits = 0; // cached=1 的行数
    int failures = 0; // ok=0 的行数
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
    bool operator==(const UsageSummary &) const = default;
};

class UsageStore {
public:
    explicit UsageStore(library::Database &db);
    ~UsageStore() = default;
    Q_DISABLE_COPY_MOVE(UsageStore)

    core::Result<void> record(const UsageRecord &record);
    /// created_at ∈ [fromMs, toMs)，按 (purpose, model) 分组，按 purpose 枚举顺序、再按 model
    /// 排序。 数据库中无法识别的 purpose 名跳过。
    core::Result<QList<UsageSummary>> summarize(qint64 fromMs, qint64 toMs) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::ai
