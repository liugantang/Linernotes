// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "WritebackController.h"

#include "UiLogging.h"

#include <QJsonObject>
#include <QStringList>

#include <ai/JobQueue.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/Scanner.h>

namespace linernotes::ui {

WritebackController::WritebackController(library::Database &db, const core::Clock &clock,
    ai::JobQueue &jobs, library::Scanner &scanner, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
    , m_jobs(jobs)
    , m_scanner(scanner)
    , m_store(db, clock)
{
    connect(&m_jobs, &ai::JobQueue::jobChanged, this, &WritebackController::onJobChanged);
}

bool WritebackController::isRunning() const
{
    return m_running;
}

int WritebackController::done() const
{
    return m_done;
}

int WritebackController::total() const
{
    return m_total;
}

int WritebackController::failed() const
{
    return m_failed;
}

QString WritebackController::summary() const
{
    return m_summary;
}

int WritebackController::writableFileCount(qint64 batchId)
{
    if (batchId <= 0) {
        return 0;
    }
    const auto planRes = m_store.plan(batchId);
    if (!planRes.ok()) {
        return 0;
    }
    return static_cast<int>(planRes.value().files.size());
}

bool WritebackController::hasActiveWriteback(qint64 batchId)
{
    if (batchId <= 0) {
        return false;
    }
    return m_store.activeWriteback(batchId).has_value();
}

void WritebackController::startWriteback(qint64 batchId)
{
    if (m_running || batchId <= 0) {
        return;
    }
    const auto planRes = m_store.plan(batchId);
    if (!planRes.ok()) {
        qCWarning(lcUi, "Failed to plan writeback for batch %lld: %s", batchId,
            qPrintable(planRes.error().toString()));
        return;
    }
    const auto &plan = planRes.value();
    if (plan.files.isEmpty()) {
        return;
    }

    const auto createRes = m_store.create(batchId, plan, m_clock.nowMs());
    if (!createRes.ok()) {
        qCWarning(lcUi, "Failed to create writeback record for batch %lld: %s", batchId,
            qPrintable(createRes.error().toString()));
        return;
    }
    m_currentWritebackId = createRes.value();
    m_isRevertJob = false;

    QStringList itemKeys;
    itemKeys.reserve(plan.files.size());
    for (const auto &pf : plan.files) {
        itemKeys.append(QString::number(pf.fileId));
    }

    QJsonObject params;
    params.insert(QStringLiteral("writebackId"), m_currentWritebackId);

    const auto enqueueRes = m_jobs.enqueue(
        QStringLiteral("library.writeback"), tr("Write tags to files"), itemKeys, params);
    if (!enqueueRes.ok()) {
        qCWarning(
            lcUi, "Failed to enqueue writeback job: %s", qPrintable(enqueueRes.error().toString()));
        return;
    }

    m_currentJobId = enqueueRes.value();
    m_running = true;
    m_done = 0;
    m_total = static_cast<int>(itemKeys.size());
    m_failed = 0;
    m_summary.clear();
    emit runningChanged();
    emit progressChanged();
    emit summaryChanged();

    if (const auto jobInfo = m_jobs.job(m_currentJobId); jobInfo.has_value()) {
        m_done = jobInfo->done;
        m_total = jobInfo->total;
        m_failed = jobInfo->failed;
        emit progressChanged();

        if (jobInfo->state == ai::JobState::Completed
            || jobInfo->state == ai::JobState::Cancelled) {
            finishCurrentJob(jobInfo->state == ai::JobState::Cancelled);
        }
    }
}

void WritebackController::startRevert(qint64 batchId)
{
    if (m_running || batchId <= 0) {
        return;
    }
    const auto wbOpt = m_store.activeWriteback(batchId);
    if (!wbOpt.has_value()) {
        return;
    }
    m_currentWritebackId = wbOpt.value();
    m_isRevertJob = true;

    const auto writtenRes = m_store.writtenFileIds(m_currentWritebackId);
    if (!writtenRes.ok() || writtenRes.value().isEmpty()) {
        return;
    }
    const auto &writtenIds = writtenRes.value();

    QStringList itemKeys;
    itemKeys.reserve(writtenIds.size());
    for (const qint64 fId : writtenIds) {
        itemKeys.append(QString::number(fId));
    }

    QJsonObject params;
    params.insert(QStringLiteral("writebackId"), m_currentWritebackId);

    const auto enqueueRes = m_jobs.enqueue(
        QStringLiteral("library.writeback_revert"), tr("Revert tag writeback"), itemKeys, params);
    if (!enqueueRes.ok()) {
        qCWarning(
            lcUi, "Failed to enqueue revert job: %s", qPrintable(enqueueRes.error().toString()));
        return;
    }

    m_currentJobId = enqueueRes.value();
    m_running = true;
    m_done = 0;
    m_total = static_cast<int>(itemKeys.size());
    m_failed = 0;
    m_summary.clear();
    emit runningChanged();
    emit progressChanged();
    emit summaryChanged();

    if (const auto jobInfo = m_jobs.job(m_currentJobId); jobInfo.has_value()) {
        m_done = jobInfo->done;
        m_total = jobInfo->total;
        m_failed = jobInfo->failed;
        emit progressChanged();

        if (jobInfo->state == ai::JobState::Completed
            || jobInfo->state == ai::JobState::Cancelled) {
            finishCurrentJob(jobInfo->state == ai::JobState::Cancelled);
        }
    }
}

void WritebackController::onJobChanged(qint64 jobId)
{
    if (!m_running || jobId != m_currentJobId) {
        return;
    }
    const auto jobInfo = m_jobs.job(jobId);
    if (!jobInfo.has_value()) {
        return;
    }
    m_done = jobInfo->done;
    m_total = jobInfo->total;
    m_failed = jobInfo->failed;
    emit progressChanged();

    if (jobInfo->state == ai::JobState::Completed || jobInfo->state == ai::JobState::Cancelled) {
        finishCurrentJob(jobInfo->state == ai::JobState::Cancelled);
    }
}

void WritebackController::finishCurrentJob(bool /*cancelled*/)
{
    const qint64 wbId = m_currentWritebackId;
    const bool isRevert = m_isRevertJob;

    if (isRevert) {
        static_cast<void>(m_store.finishRevert(wbId, m_clock.nowMs()));
    }

    const auto countsRes = m_store.counts(wbId);
    if (countsRes.ok()) {
        const auto &c = countsRes.value();
        if (isRevert) {
            m_summary = tr("%1 file(s) reverted, %2 failed").arg(c.reverted).arg(c.revertFailed);
        } else {
            m_summary = tr("%1 file(s) written, %2 failed, %3 skipped")
                            .arg(c.written)
                            .arg(c.failed)
                            .arg(c.skipped);
        }
    }

    const auto dirsRes = m_store.affectedDirectories(wbId);
    if (dirsRes.ok() && !dirsRes.value().isEmpty()) {
        m_scanner.startPaths(dirsRes.value());
    }

    emit libraryModified();

    m_running = false;
    m_currentJobId = 0;
    emit runningChanged();
    emit summaryChanged();
}

} // namespace linernotes::ui
