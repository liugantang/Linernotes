// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CorrectionReviewController.h"

#include "UiLogging.h"

#include <QSqlQuery>

#include <library/EnumNames.h>

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

void CorrectionReviewController::selectBatch(qint64 batchId)
{
    m_currentBatchId = batchId;
    m_currentBatchKind = library::CorrectionKind::Manual;

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
        m_errorText = res.error().message.isEmpty() ? res.error().toString() : res.error().message;
        qCWarning(
            lcUi, "Failed to accept selected corrections: %s", qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    refresh();
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
        m_errorText = res.error().message.isEmpty() ? res.error().toString() : res.error().message;
        qCWarning(
            lcUi, "Failed to reject selected corrections: %s", qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
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
        m_errorText = res.error().message.isEmpty() ? res.error().toString() : res.error().message;
        qCWarning(lcUi, "Failed to accept all pending corrections: %s",
            qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    refresh();
    emit libraryModified();
}

void CorrectionReviewController::acceptEdited(qint64 correctionId, const QString &value)
{
    auto res = m_store.acceptEdited(correctionId, value);
    if (!res.ok()) {
        m_errorText = res.error().message.isEmpty() ? res.error().toString() : res.error().message;
        qCWarning(lcUi, "Failed to accept edited correction %lld: %s", correctionId,
            qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    refresh();
    emit libraryModified();
}

void CorrectionReviewController::revertBatch(qint64 batchId)
{
    auto res = m_store.revertBatch(batchId);
    if (!res.ok()) {
        m_errorText = res.error().message.isEmpty() ? res.error().toString() : res.error().message;
        qCWarning(
            lcUi, "Failed to revert batch %lld: %s", batchId, qPrintable(res.error().toString()));
        emit errorTextChanged();
        return;
    }

    m_errorText.clear();
    emit errorTextChanged();
    refresh();
    emit libraryModified();
}

void CorrectionReviewController::refresh()
{
    m_batchModel.refresh();
    if (m_currentBatchId > 0) {
        selectBatch(m_currentBatchId);
    } else {
        m_listModel.setBatch(0, library::CorrectionKind::Manual);
    }
}

} // namespace linernotes::ui
