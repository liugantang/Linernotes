// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CorrectionListModel.h"

#include "Format.h"
#include "UiLogging.h"

#include <QCoreApplication>
#include <QtMath>

namespace linernotes::ui {

CorrectionListModel::CorrectionListModel(
    library::Database &db, const core::Clock &clock, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(db, clock)
{
}

int CorrectionListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_filteredRows.size());
}

QVariant CorrectionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filteredRows.size()) {
        return { };
    }

    const auto &item = m_filteredRows.at(index.row());
    switch (role) {
    case CorrectionIdRole:
        return item.correctionId;
    case EntityRole:
        return QVariant::fromValue(item.entity);
    case SubjectRole:
        return item.subject;
    case DetailRole:
        return item.detail;
    case FieldRole:
        return item.field;
    case OldValueRole:
        return item.oldValue;
    case NewValueRole:
        return item.newValue;
    case ConfidenceRole:
        return item.confidence;
    case ReasonRole:
        return QCoreApplication::translate("butler", item.reason.toUtf8().constData());
    case SourceRole:
        return QVariant::fromValue(item.source);
    case StatusRole:
        return QVariant::fromValue(item.status);
    default:
        return { };
    }
}

QHash<int, QByteArray> CorrectionListModel::roleNames() const
{
    return {
        { CorrectionIdRole, "correctionId" },
        { EntityRole, "entity" },
        { SubjectRole, "subject" },
        { DetailRole, "detail" },
        { FieldRole, "field" },
        { OldValueRole, "oldValue" },
        { NewValueRole, "newValue" },
        { ConfidenceRole, "confidence" },
        { ReasonRole, "reason" },
        { SourceRole, "source" },
        { StatusRole, "status" },
    };
}

CorrectionListModel::StatusFilter CorrectionListModel::statusFilter() const
{
    return m_statusFilter;
}

void CorrectionListModel::setStatusFilter(StatusFilter filter)
{
    if (m_statusFilter == filter) {
        return;
    }
    m_statusFilter = filter;
    emit filterChanged();
    applyFilter();
}

double CorrectionListModel::minConfidence() const
{
    return m_minConfidence;
}

void CorrectionListModel::setMinConfidence(double minConfidence)
{
    if (qFuzzyCompare(m_minConfidence, minConfidence)) {
        return;
    }
    m_minConfidence = minConfidence;
    emit filterChanged();
    applyFilter();
}

linernotes::ui::RowSelection *CorrectionListModel::selection()
{
    return &m_selection;
}

const linernotes::ui::RowSelection *CorrectionListModel::selection() const
{
    return &m_selection;
}

int CorrectionListModel::count() const
{
    return static_cast<int>(m_filteredRows.size());
}

int CorrectionListModel::pendingCount() const
{
    int pending = 0;
    for (const auto &item : m_allRows) {
        if (item.status == library::CorrectionStatus::Pending) {
            ++pending;
        }
    }
    return pending;
}

qint64 CorrectionListModel::batchId() const
{
    return m_batchId;
}

library::CorrectionKind CorrectionListModel::batchKind() const
{
    return m_batchKind;
}

const QList<CorrectionListModel::UnifiedCorrectionItem> &CorrectionListModel::filteredRows() const
{
    return m_filteredRows;
}

const QList<CorrectionListModel::UnifiedCorrectionItem> &CorrectionListModel::allRows() const
{
    return m_allRows;
}

void CorrectionListModel::setBatch(qint64 batchId, library::CorrectionKind kind)
{
    m_batchId = batchId;
    m_batchKind = kind;
    emit batchChanged();

    if (batchId <= 0) {
        m_allRows.clear();
        applyFilter();
        return;
    }

    if (kind == library::CorrectionKind::ArtistMerge) {
        auto res = m_store.artistAliasCorrections(batchId);
        if (!res.ok()) {
            qCWarning(lcUi, "Failed to fetch artist alias corrections for batch %lld: %s", batchId,
                qPrintable(res.error().toString()));
            m_allRows.clear();
            applyFilter();
            return;
        }
        m_allRows.clear();
        m_allRows.reserve(res.value().size());
        for (const auto &row : res.value()) {
            UnifiedCorrectionItem item;
            item.correctionId = row.id;
            item.entity = CorrectionEntity::Artist;
            item.subject = row.artistName;
            item.detail = QString();
            item.field = (row.locale.has_value() && !row.locale->isEmpty())
                ? tr("Alias (%1)").arg(*row.locale)
                : tr("Alias");
            item.oldValue = QString();
            item.newValue = row.alias;
            item.confidence = row.confidence;
            item.reason = row.reason;
            item.source = row.source;
            item.status = row.status;
            m_allRows.append(item);
        }
    } else {
        auto res = m_store.corrections(batchId);
        if (!res.ok()) {
            qCWarning(lcUi, "Failed to fetch corrections for batch %lld: %s", batchId,
                qPrintable(res.error().toString()));
            m_allRows.clear();
            applyFilter();
            return;
        }
        m_allRows.clear();
        m_allRows.reserve(res.value().size());
        for (const auto &row : res.value()) {
            UnifiedCorrectionItem item;
            item.correctionId = row.id;
            item.entity = CorrectionEntity::Track;
            item.subject = row.trackTitle;
            item.detail = row.filePath;
            item.field = formatTagField(row.field);
            item.oldValue = row.oldValue;
            item.newValue = row.newValue;
            item.confidence = row.confidence;
            item.reason = row.reason;
            item.source = row.source;
            item.status = row.status;
            m_allRows.append(item);
        }
    }

    applyFilter();
}

void CorrectionListModel::refresh()
{
    setBatch(m_batchId, m_batchKind);
}

void CorrectionListModel::applyFilter()
{
    beginResetModel();
    m_selection.clear();
    m_filteredRows.clear();

    for (const auto &item : m_allRows) {
        if (m_statusFilter != StatusFilter::All) {
            bool matches = false;
            switch (m_statusFilter) {
            case StatusFilter::Pending:
                matches = (item.status == library::CorrectionStatus::Pending);
                break;
            case StatusFilter::Accepted:
                matches = (item.status == library::CorrectionStatus::Accepted);
                break;
            case StatusFilter::Rejected:
                matches = (item.status == library::CorrectionStatus::Rejected);
                break;
            case StatusFilter::Reverted:
                matches = (item.status == library::CorrectionStatus::Reverted);
                break;
            case StatusFilter::All:
                matches = true;
                break;
            }
            if (!matches) {
                continue;
            }
        }
        if (item.confidence < m_minConfidence) {
            continue;
        }
        m_filteredRows.append(item);
    }
    endResetModel();

    emit countChanged();
}

qint64 CorrectionListModel::correctionIdAt(int row) const
{
    if (row < 0 || row >= m_filteredRows.size()) {
        return 0;
    }
    return m_filteredRows.at(row).correctionId;
}

void CorrectionListModel::selectAll()
{
    m_selection.selectAll(static_cast<int>(m_filteredRows.size()));
}

void CorrectionListModel::clearSelection()
{
    m_selection.clear();
}

QList<qint64> CorrectionListModel::selectedCorrectionIds() const
{
    QList<qint64> ids;
    const auto selected = m_selection.selectedRows();
    for (const int row : selected) {
        if (row >= 0 && row < m_filteredRows.size()) {
            ids.append(m_filteredRows.at(row).correctionId);
        }
    }
    return ids;
}

QList<qint64> CorrectionListModel::allPendingCorrectionIds(double minConfidence) const
{
    QList<qint64> ids;
    for (const auto &item : m_allRows) {
        if (item.status == library::CorrectionStatus::Pending && item.confidence >= minConfidence) {
            ids.append(item.correctionId);
        }
    }
    return ids;
}

} // namespace linernotes::ui
