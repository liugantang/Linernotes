// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AudioAnalysisController.h"

#include "UiLogging.h"

#include <QFile>
#include <QStringList>

#include <ai/JobQueue.h>
#include <audio/TrackEmbedding.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>

#include <utility>

namespace linernotes::ui {

AudioAnalysisController::AudioAnalysisController(library::Database &db, const core::Clock &clock,
    ai::JobQueue &jobs, QString modelPath, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
    , m_jobs(jobs)
    , m_modelPath(std::move(modelPath))
{
    connect(&m_jobs, &ai::JobQueue::jobChanged, this, &AudioAnalysisController::onJobChanged);
    findExistingJob();
    refresh();
}

bool AudioAnalysisController::isModelAvailable() const
{
    return QFile::exists(m_modelPath);
}

QString AudioAnalysisController::modelPath() const
{
    return m_modelPath;
}

int AudioAnalysisController::analyzedCount() const
{
    return m_analyzedCount;
}

int AudioAnalysisController::pendingCount() const
{
    return m_pendingCount;
}

bool AudioAnalysisController::isRunning() const
{
    return m_running;
}

bool AudioAnalysisController::isPaused() const
{
    return m_paused;
}

int AudioAnalysisController::done() const
{
    return m_done;
}

int AudioAnalysisController::total() const
{
    return m_total;
}

void AudioAnalysisController::findExistingJob()
{
    const auto allJobs = m_jobs.jobs();
    for (const auto &job : allJobs) {
        if (job.kind == QLatin1StringView("audio.embed")
            && (job.state == ai::JobState::Running || job.state == ai::JobState::Paused)) {
            m_currentJobId = job.id;
            m_running = (job.state == ai::JobState::Running);
            m_paused = (job.state == ai::JobState::Paused);
            m_done = job.done;
            m_total = job.total;
            return;
        }
    }
}

void AudioAnalysisController::refresh()
{
    findExistingJob();

    const library::EmbeddingStore store(m_db, m_clock);
    const QString modelId
        = QString::fromLatin1(audio::kEmbeddingModelId.data(), audio::kEmbeddingModelId.size());

    int newAnalyzed = 0;
    if (const auto res = store.analyzedCount(modelId); res.ok()) {
        newAnalyzed = res.value();
    }
    int newPending = 0;
    if (const auto res = store.pendingTrackIds(modelId); res.ok()) {
        newPending = static_cast<int>(res.value().size());
    }

    if (newAnalyzed != m_analyzedCount || newPending != m_pendingCount) {
        m_analyzedCount = newAnalyzed;
        m_pendingCount = newPending;
        emit countsChanged();
    }
}

void AudioAnalysisController::start()
{
    if (!isModelAvailable()) {
        return;
    }

    const auto allJobs = m_jobs.jobs();
    for (const auto &job : allJobs) {
        if (job.kind == QLatin1StringView("audio.embed")
            && (job.state == ai::JobState::Paused || job.state == ai::JobState::Running)) {
            m_currentJobId = job.id;
            if (job.state == ai::JobState::Paused) {
                m_jobs.resume(m_currentJobId);
            }
            return;
        }
    }

    const library::EmbeddingStore store(m_db, m_clock);
    const QString modelId
        = QString::fromLatin1(audio::kEmbeddingModelId.data(), audio::kEmbeddingModelId.size());
    const auto pendingRes = store.pendingTrackIds(modelId);
    if (!pendingRes.ok() || pendingRes.value().isEmpty()) {
        return;
    }

    QStringList items;
    items.reserve(pendingRes.value().size());
    for (const qint64 trackId : pendingRes.value()) {
        items.append(QString::number(trackId));
    }

    auto enqueueRes = m_jobs.enqueue(
        QStringLiteral("audio.embed"), QStringLiteral("Analyze audio embeddings"), items);
    if (!enqueueRes.ok()) {
        qCWarning(lcUi, "Failed to enqueue audio embedding job: %s",
            qPrintable(enqueueRes.error().toString()));
        return;
    }

    m_currentJobId = enqueueRes.value();
    onJobChanged(m_currentJobId);
}

void AudioAnalysisController::pause()
{
    if (m_currentJobId > 0) {
        m_jobs.pause(m_currentJobId);
    }
}

void AudioAnalysisController::onJobChanged(qint64 jobId)
{
    if (m_currentJobId > 0 && jobId != m_currentJobId) {
        return;
    }

    const auto jobInfo = m_jobs.job(jobId);
    if (!jobInfo.has_value()) {
        if (jobId == m_currentJobId) {
            m_currentJobId = 0;
            m_running = false;
            m_paused = false;
            m_done = 0;
            m_total = 0;
            emit stateChanged();
            emit progressChanged();
            refresh();
        }
        return;
    }

    if (jobInfo->kind != QLatin1StringView("audio.embed")) {
        return;
    }

    m_currentJobId = jobId;
    const bool oldRunning = m_running;
    const bool oldPaused = m_paused;
    const int oldDone = m_done;
    const int oldTotal = m_total;

    m_running = (jobInfo->state == ai::JobState::Running);
    m_paused = (jobInfo->state == ai::JobState::Paused);
    m_done = jobInfo->done;
    m_total = jobInfo->total;

    if (m_running != oldRunning || m_paused != oldPaused) {
        emit stateChanged();
    }
    if (m_done != oldDone || m_total != oldTotal) {
        emit progressChanged();
    }

    if (jobInfo->state == ai::JobState::Completed || jobInfo->state == ai::JobState::Cancelled) {
        m_currentJobId = 0;
        m_running = false;
        m_paused = false;
        emit stateChanged();
        refresh();
    }
}

} // namespace linernotes::ui
