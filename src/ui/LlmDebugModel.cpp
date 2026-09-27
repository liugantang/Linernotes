// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmDebugModel.h"

#include <QDateTime>

#include <ranges>

namespace linernotes::ui {

namespace {

constexpr int kMaxDebugEntries = 200;

} // namespace

LlmDebugModel::LlmDebugModel(ai::LlmDebugLog &log, QObject *parent)
    : QAbstractListModel(parent)
    , m_log(log)
{
    const auto &entries = m_log.entries();
    m_ids.reserve(entries.size());
    for (const auto &entry : std::views::reverse(entries)) {
        m_ids.append(entry.id);
    }

    connect(&m_log, &ai::LlmDebugLog::entryAdded, this, &LlmDebugModel::onEntryAdded);
    connect(&m_log, &ai::LlmDebugLog::cleared, this, &LlmDebugModel::onCleared);
}

int LlmDebugModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_ids.size());
}

QVariant LlmDebugModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size()) {
        return { };
    }

    const quint64 id = m_ids.at(index.row());
    const auto entryOpt = m_log.entry(id);
    if (!entryOpt.has_value()) {
        return { };
    }

    const auto &entry = *entryOpt;

    switch (role) {
    case EntryIdRole:
        return QVariant::fromValue(entry.id);
    case TimeRole:
        if (entry.startedAtMs <= 0) {
            return QStringLiteral("--:--:--");
        }
        return QDateTime::fromMSecsSinceEpoch(entry.startedAtMs)
            .toLocalTime()
            .toString(QStringLiteral("HH:mm:ss"));
    case PurposeRole:
        return QVariant::fromValue(entry.purpose);
    case ModelRole:
        return entry.model;
    case AttemptRole:
        return entry.attempt;
    case FromCacheRole:
        return entry.fromCache;
    case OkRole:
        return entry.errorCode.isEmpty();
    case HttpStatusRole:
        return entry.httpStatus;
    case ErrorCodeRole:
        return entry.errorCode;
    case ElapsedMsRole:
        return entry.elapsedMs;
    case PromptTokensRole:
        return entry.usage.promptTokens;
    case CompletionTokensRole:
        return entry.usage.completionTokens;
    default:
        return { };
    }
}

QHash<int, QByteArray> LlmDebugModel::roleNames() const
{
    return {
        { EntryIdRole, "entryId" },
        { TimeRole, "time" },
        { PurposeRole, "purpose" },
        { ModelRole, "model" },
        { AttemptRole, "attempt" },
        { FromCacheRole, "fromCache" },
        { OkRole, "ok" },
        { HttpStatusRole, "httpStatus" },
        { ErrorCodeRole, "errorCode" },
        { ElapsedMsRole, "elapsedMs" },
        { PromptTokensRole, "promptTokens" },
        { CompletionTokensRole, "completionTokens" },
    };
}

void LlmDebugModel::onEntryAdded(quint64 id)
{
    if (m_ids.size() >= kMaxDebugEntries) {
        beginRemoveRows(
            QModelIndex(), static_cast<int>(m_ids.size() - 1), static_cast<int>(m_ids.size() - 1));
        m_ids.removeLast();
        endRemoveRows();
    }
    beginInsertRows(QModelIndex(), 0, 0);
    m_ids.prepend(id);
    endInsertRows();
}

void LlmDebugModel::onCleared()
{
    beginResetModel();
    m_ids.clear();
    endResetModel();
}

} // namespace linernotes::ui
