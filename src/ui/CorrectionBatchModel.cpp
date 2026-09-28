// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CorrectionBatchModel.h"

#include "Format.h"
#include "UiLogging.h"

#include <algorithm>

namespace linernotes::ui {

CorrectionBatchModel::CorrectionBatchModel(
    library::Database &db, const core::Clock &clock, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(db, clock)
{
}

int CorrectionBatchModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_batches.size());
}

QVariant CorrectionBatchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_batches.size()) {
        return { };
    }

    const auto &batch = m_batches.at(index.row());
    switch (role) {
    case BatchIdRole:
        return batch.id;
    case KindRole:
        return QVariant::fromValue(batch.kind);
    case DescriptionRole:
        return batch.description;
    case CreatedAtRole:
        return batch.createdAt;
    case CreatedTextRole:
        return formatDateTime(batch.createdAt);
    case PendingCountRole:
        return batch.pending;
    case AcceptedCountRole:
        return batch.accepted;
    case RejectedCountRole:
        return batch.rejected;
    case RevertedCountRole:
        return batch.reverted;
    case RevertedRole:
        return batch.revertedAt.has_value();
    default:
        return { };
    }
}

QHash<int, QByteArray> CorrectionBatchModel::roleNames() const
{
    return {
        { BatchIdRole, "batchId" },
        { KindRole, "kind" },
        { DescriptionRole, "description" },
        { CreatedAtRole, "createdAt" },
        { CreatedTextRole, "createdText" },
        { PendingCountRole, "pendingCount" },
        { AcceptedCountRole, "acceptedCount" },
        { RejectedCountRole, "rejectedCount" },
        { RevertedCountRole, "revertedCount" },
        { RevertedRole, "reverted" },
    };
}

int CorrectionBatchModel::count() const
{
    return static_cast<int>(m_batches.size());
}

bool CorrectionBatchModel::hasDecided() const
{
    return std::ranges::any_of(m_batches, [](const auto &b) { return b.pending == 0; });
}

const QList<library::CorrectionBatchInfo> &CorrectionBatchModel::batches() const
{
    return m_batches;
}

void CorrectionBatchModel::refresh()
{
    auto res = m_store.batches();
    if (!res.ok()) {
        qCWarning(
            lcUi, "Failed to fetch correction batches: %s", qPrintable(res.error().toString()));
        return;
    }

    beginResetModel();
    m_batches = res.value();
    endResetModel();

    emit countChanged();
}

qint64 CorrectionBatchModel::batchIdAt(int row) const
{
    if (row < 0 || row >= m_batches.size()) {
        return 0;
    }
    return m_batches.at(row).id;
}

} // namespace linernotes::ui
