// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CorrectionReviewController.h"

#include "ErrorText.h"
#include "UiLogging.h"

#include <QSqlQuery>

#include <library/EnumNames.h>

#include <algorithm>

namespace linernotes::ui {

CorrectionReviewController::CorrectionReviewController(
    library::Database &db, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
    , m_store(db, clock)
    , m_batchModel(db, clock, this)
    , m_listModel(db, clock, this)
{
}

CorrectionBatchModel *CorrectionReviewController::batchModel()
{
    return &m_batchModel;
}

const CorrectionBatchModel *CorrectionReviewController::batchModel() const
{
    return &m_batchModel;
}

CorrectionListModel *CorrectionReviewController::listModel()
{
    return &m_listModel;
}

const CorrectionListModel *CorrectionReviewController::listModel() const
{
    return &m_listModel;
}

qint64 CorrectionReviewController::currentBatchId() const
{
    return m_currentBatchId;
}

QString CorrectionReviewController::errorText() const
{
    return m_errorText;
}

QString CorrectionReviewController::noticeText() const
{
    return m_noticeText;
}

void CorrectionReviewController::selectBatch(qint64 batchId)
{
    m_currentBatchId = batchId;
    m_currentBatchKind = library::CorrectionKind::Manual;

    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }

    if (batchId > 0) {
        auto connRes = m_db.connection();
        if (connRes.ok()) {
            QSqlQuery q(connRes.value());
            q.prepare(QStringLiteral("SELECT kind FROM correction_batches WHERE id = ?"));
            q.addBindValue(batchId);
            if (q.exec() && q.next()) {
                m_currentBatchKind = library::correctionKindFromString(q.value(0).toString())
                                         .value_or(library::CorrectionKind::Manual);
            }
        }
    }

    m_listModel.setBatch(m_currentBatchId, m_currentBatchKind);
    emit currentBatchChanged();
}

void CorrectionReviewController::acceptSelected()
{
    const auto ids = m_listModel.selectedCorrectionIds();
    if (ids.isEmpty()) {
        return;
    }

    auto res = m_store.accept(ids);
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(
            lcUi, "Failed to accept selected corrections: %s", qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    refresh(); // 会经 selectBatch 清空 noticeText，因此先刷新再设置

    const auto &outcome = res.value();
    if (outcome.skippedStale > 0) {
        m_noticeText = tr("Accepted %n correction(s).", "", outcome.accepted) + QStringLiteral(" ")
            + tr("%n skipped because the artist or track no longer exists.", "",
                outcome.skippedStale);
    } else {
        m_noticeText.clear();
    }
    emit noticeTextChanged();
    emit libraryModified();
}

void CorrectionReviewController::rejectSelected()
{
    const auto ids = m_listModel.selectedCorrectionIds();
    if (ids.isEmpty()) {
        return;
    }

    auto res = m_store.reject(ids);
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(
            lcUi, "Failed to reject selected corrections: %s", qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }
    refresh();
    emit libraryModified();
}

void CorrectionReviewController::acceptAllPending(double minConfidence)
{
    const auto ids = m_listModel.allPendingCorrectionIds(minConfidence);
    if (ids.isEmpty()) {
        return;
    }

    auto res = m_store.accept(ids);
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(lcUi, "Failed to accept all pending corrections: %s",
            qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    refresh(); // 会经 selectBatch 清空 noticeText，因此先刷新再设置

    const auto &outcome = res.value();
    if (outcome.skippedStale > 0) {
        m_noticeText = tr("Accepted %n correction(s).", "", outcome.accepted) + QStringLiteral(" ")
            + tr("%n skipped because the artist or track no longer exists.", "",
                outcome.skippedStale);
    } else {
        m_noticeText.clear();
    }
    emit noticeTextChanged();
    emit libraryModified();
}

void CorrectionReviewController::acceptEdited(qint64 correctionId, const QString &value)
{
    auto res = m_store.acceptEdited(correctionId, value);
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(lcUi, "Failed to accept edited correction %lld: %s", correctionId,
            qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }
    refresh();
    emit libraryModified();
}

void CorrectionReviewController::revertBatch(qint64 batchId)
{
    auto res = m_store.revertBatch(batchId);
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(
            lcUi, "Failed to revert batch %lld: %s", batchId, qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }
    refresh();
    emit libraryModified();
}

void CorrectionReviewController::deleteBatch(qint64 batchId)
{
    auto res = m_store.deleteBatch(batchId);
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(
            lcUi, "Failed to delete batch %lld: %s", batchId, qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }
    refresh();
}

void CorrectionReviewController::deleteDecidedBatches()
{
    auto res = m_store.deleteDecidedBatches();
    if (!res.ok()) {
        m_errorText = userErrorText(res.error());
        if (!m_noticeText.isEmpty()) {
            m_noticeText.clear();
            emit noticeTextChanged();
        }
        qCWarning(lcUi, "Failed to delete decided batches: %s", qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }
    refresh();
}

void CorrectionReviewController::refresh()
{
    m_batchModel.refresh();
    const auto &batches = m_batchModel.batches();
    const bool currentStillExists = (m_currentBatchId > 0)
        && std::ranges::any_of(batches, [this](const auto &b) { return b.id == m_currentBatchId; });

    if (currentStillExists) {
        selectBatch(m_currentBatchId);
    } else if (!batches.isEmpty()) {
        selectBatch(batches.first().id);
    } else {
        selectBatch(0);
    }
}

void CorrectionReviewController::showStale()
{
    m_listModel.setStatusFilter(CorrectionListModel::StatusFilter::Stale);
}

void CorrectionReviewController::clearNotice()
{
    if (!m_noticeText.isEmpty()) {
        m_noticeText.clear();
        emit noticeTextChanged();
    }
}

} // namespace linernotes::ui
