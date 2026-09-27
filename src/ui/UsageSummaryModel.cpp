// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "UsageSummaryModel.h"

namespace linernotes::ui {

UsageSummaryModel::UsageSummaryModel(
    ai::UsageStore &usageStore, const core::Clock &clock, QObject *parent)
    : QAbstractListModel(parent)
    , m_usageStore(usageStore)
    , m_clock(clock)
{
}

int UsageSummaryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_summaries.size());
}

QVariant UsageSummaryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_summaries.size()) {
        return { };
    }

    const auto &summary = m_summaries.at(index.row());
    switch (role) {
    case PurposeRole:
        return QVariant::fromValue(summary.purpose);
    case ModelRole:
        return summary.model;
    case RequestsRole:
        return summary.requests;
    case CacheHitsRole:
        return summary.cacheHits;
    case FailuresRole:
        return summary.failures;
    case PromptTokensRole:
        return summary.promptTokens;
    case CompletionTokensRole:
        return summary.completionTokens;
    default:
        return { };
    }
}

QHash<int, QByteArray> UsageSummaryModel::roleNames() const
{
    return {
        { PurposeRole, "purpose" },
        { ModelRole, "model" },
        { RequestsRole, "requests" },
        { CacheHitsRole, "cacheHits" },
        { FailuresRole, "failures" },
        { PromptTokensRole, "promptTokens" },
        { CompletionTokensRole, "completionTokens" },
    };
}

qint64 UsageSummaryModel::totalPromptTokens() const
{
    return m_totalPromptTokens;
}

qint64 UsageSummaryModel::totalCompletionTokens() const
{
    return m_totalCompletionTokens;
}

int UsageSummaryModel::totalRequests() const
{
    return m_totalRequests;
}

void UsageSummaryModel::refresh(int days)
{
    const qint64 now = m_clock.nowMs();
    const qint64 daysMs = static_cast<qint64>(days) * 86400000LL;
    const qint64 fromMs = now - daysMs;
    const auto res = m_usageStore.summarize(fromMs, now);

    beginResetModel();
    if (res.ok()) {
        m_summaries = res.value();
    } else {
        m_summaries.clear();
    }

    m_totalPromptTokens = 0;
    m_totalCompletionTokens = 0;
    m_totalRequests = 0;
    for (const auto &item : m_summaries) {
        m_totalPromptTokens += item.promptTokens;
        m_totalCompletionTokens += item.completionTokens;
        m_totalRequests += item.requests;
    }
    endResetModel();
    emit totalsChanged();
}

} // namespace linernotes::ui
