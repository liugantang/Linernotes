// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CleanupController.h"

#include "AppSettings.h"
#include "UiLogging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QtConcurrent/QtConcurrent>

#include <ai/AiConfig.h>
#include <ai/JobQueue.h>
#include <butler/ArtistMergeSource.h>
#include <butler/ArtistSplitSource.h>
#include <butler/MojibakeSource.h>
#include <core/Clock.h>
#include <core/Settings.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>

namespace linernotes::ui {

namespace {

int countSplitValues(const QStringList &items)
{
    int count = 0;
    for (const auto &key : items) {
        const auto doc = QJsonDocument::fromJson(key.toUtf8());
        if (doc.isArray()) {
            count += static_cast<int>(doc.array().size());
        }
    }
    return count;
}

int countMergeClusters(const QStringList &items)
{
    int count = 0;
    for (const auto &key : items) {
        const auto doc = QJsonDocument::fromJson(key.toUtf8());
        if (doc.isObject()) {
            const auto obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();
            if (type == QLatin1StringView("cluster")) {
                count += 1;
            } else if (type == QLatin1StringView("fuzzy")) {
                count += static_cast<int>(obj.value(QStringLiteral("pairs")).toArray().size());
            }
        }
    }
    return count;
}

int queryMissingAlbumTracks(library::Database &db)
{
    auto connRes = db.connection();
    if (!connRes.ok()) {
        return 0;
    }
    QSqlQuery q(connRes.value());
    const QString sql = QStringLiteral("SELECT COUNT(*) FROM tracks t "
                                       "JOIN files f ON t.file_id = f.id "
                                       "LEFT JOIN effective_metadata em ON t.id = em.track_id "
                                       "WHERE f.missing_since IS NULL "
                                       "AND (em.album IS NULL OR trim(em.album) = '')");
    if (q.exec(sql) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

} // namespace

CleanupController::CleanupController(library::Database &db, const core::Clock &clock,
    ai::JobQueue &jobs, const ai::AiConfig &aiConfig, core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
    , m_jobs(jobs)
    , m_aiConfig(aiConfig)
    , m_settings(settings)
{
    m_llmConfigured = m_aiConfig.resolve(ai::Purpose::Cleanup).has_value();

    connect(&m_healthWatcher, &QFutureWatcher<HealthReportData>::finished, this,
        &CleanupController::onHealthCheckFinished);
    connect(&m_stepWatcher, &QFutureWatcher<StepItemData>::finished, this,
        &CleanupController::onStepItemsReady);
    connect(&m_jobs, &ai::JobQueue::jobChanged, this, &CleanupController::onJobChanged);
}

CleanupController::~CleanupController()
{
    m_healthWatcher.cancel();
    m_healthWatcher.waitForFinished();
    m_stepWatcher.cancel();
    m_stepWatcher.waitForFinished();
}

bool CleanupController::isChecking() const
{
    return m_checking;
}

bool CleanupController::isHealthReady() const
{
    return m_healthReady;
}

int CleanupController::mojibakeGroups() const
{
    return m_mojibakeGroups;
}

int CleanupController::splitValues() const
{
    return m_splitValues;
}

int CleanupController::mergeClusters() const
{
    return m_mergeClusters;
}

int CleanupController::missingAlbumTracks() const
{
    return m_missingAlbumTracks;
}

int CleanupController::mojibakeTokens() const
{
    return m_mojibakeTokens;
}

int CleanupController::splitTokens() const
{
    return m_splitTokens;
}

int CleanupController::mergeTokens() const
{
    return m_mergeTokens;
}

bool CleanupController::isRunning() const
{
    return m_running;
}

CleanupController::Step CleanupController::currentStep() const
{
    return m_currentStep;
}

int CleanupController::stepDone() const
{
    return m_stepDone;
}

int CleanupController::stepTotal() const
{
    return m_stepTotal;
}

int CleanupController::stepFailed() const
{
    return m_stepFailed;
}

qint64 CleanupController::currentJobId() const
{
    return m_currentJobId;
}

bool CleanupController::isPaused() const
{
    if (m_currentJobId > 0) {
        if (const auto info = m_jobs.job(m_currentJobId); info.has_value()) {
            return info->state == ai::JobState::Paused;
        }
    }
    return false;
}

double CleanupController::autoAcceptThreshold() const
{
    return m_settings.value(kButlerAutoAccept);
}

void CleanupController::setAutoAcceptThreshold(double threshold)
{
    if (qFuzzyCompare(1.0 + autoAcceptThreshold(), 1.0 + threshold)) {
        return;
    }
    m_settings.setValue(kButlerAutoAccept, threshold);
    emit autoAcceptThresholdChanged();
}

bool CleanupController::useMusicBrainz() const
{
    return m_settings.value(kButlerUseMusicBrainz);
}

void CleanupController::setUseMusicBrainz(bool use)
{
    if (useMusicBrainz() == use) {
        return;
    }
    m_settings.setValue(kButlerUseMusicBrainz, use);
    emit useMusicBrainzChanged();
}

bool CleanupController::isLlmConfigured() const
{
    return m_llmConfigured;
}

void CleanupController::refreshLlmConfigured()
{
    const bool configured = m_aiConfig.resolve(ai::Purpose::Cleanup).has_value();
    if (m_llmConfigured != configured) {
        m_llmConfigured = configured;
        emit llmConfiguredChanged();
    }
}

void CleanupController::checkHealth()
{
    if (m_checking) {
        return;
    }
    m_checking = true;
    emit checkingChanged();

    auto future = QtConcurrent::run([&db = m_db]() -> HealthReportData {
        HealthReportData report;

        const butler::MojibakeSource mojibakeSource(db);
        if (auto res = mojibakeSource.findGroups(); res.ok()) {
            report.mojibakeItems = res.value();
            report.mojibakeGroups = static_cast<int>(report.mojibakeItems.size());
        }

        const butler::ArtistSplitSource splitSource(db);
        if (auto res = splitSource.findItems(); res.ok()) {
            report.splitItems = res.value();
            report.splitValues = countSplitValues(report.splitItems);
        }

        const butler::ArtistMergeSource mergeSource(db);
        if (auto res = mergeSource.findItems(false); res.ok()) {
            report.mergeItems = res.value();
            report.mergeClusters = countMergeClusters(report.mergeItems);
        }

        report.missingAlbumTracks = queryMissingAlbumTracks(db);
        return report;
    });

    m_healthWatcher.setFuture(future);
}

void CleanupController::onHealthCheckFinished()
{
    const auto data = m_healthWatcher.result();
    m_mojibakeGroups = data.mojibakeGroups;
    m_splitValues = data.splitValues;
    m_mergeClusters = data.mergeClusters;
    m_missingAlbumTracks = data.missingAlbumTracks;

    if (auto est = m_jobs.estimate(QStringLiteral("butler.mojibake"), data.mojibakeItems);
        est.ok()) {
        m_mojibakeTokens = est.value().promptTokens + est.value().completionTokens;
    } else {
        m_mojibakeTokens = 0;
    }

    if (auto est = m_jobs.estimate(QStringLiteral("butler.artist_split"), data.splitItems);
        est.ok()) {
        m_splitTokens = est.value().promptTokens + est.value().completionTokens;
    } else {
        m_splitTokens = 0;
    }

    if (auto est = m_jobs.estimate(QStringLiteral("butler.artist_merge"), data.mergeItems);
        est.ok()) {
        m_mergeTokens = est.value().promptTokens + est.value().completionTokens;
    } else {
        m_mergeTokens = 0;
    }

    m_healthReady = true;
    m_checking = false;
    emit checkingChanged();
    emit healthChanged();
}

void CleanupController::run(bool mojibake, bool split, bool merge, bool useMusicBrainz)
{
    if (m_running) {
        return;
    }

    m_pendingSteps.clear();
    if (mojibake) {
        m_pendingSteps.append(Step::Mojibake);
    }
    if (split) {
        m_pendingSteps.append(Step::Split);
    }
    if (merge) {
        m_pendingSteps.append(Step::Merge);
    }
    m_useMusicBrainzForRun = useMusicBrainz;

    if (m_pendingSteps.isEmpty()) {
        return;
    }

    m_running = true;
    emit runningChanged();
    startNextStep();
}

void CleanupController::startNextStep()
{
    if (m_pendingSteps.isEmpty()) {
        m_running = false;
        m_currentStep = Step::None;
        m_currentJobId = 0;
        m_stepDone = 0;
        m_stepTotal = 0;
        m_stepFailed = 0;
        emit runningChanged();
        emit currentStepChanged();
        emit currentJobIdChanged();
        emit progressChanged();
        emit pausedChanged();
        return;
    }

    m_currentStep = m_pendingSteps.takeFirst();
    m_currentJobId = 0;
    m_stepDone = 0;
    m_stepTotal = 0;
    m_stepFailed = 0;
    emit currentStepChanged();
    emit currentJobIdChanged();
    emit progressChanged();
    emit pausedChanged();

    const Step step = m_currentStep;
    const bool useMb = m_useMusicBrainzForRun;

    auto future = QtConcurrent::run([&db = m_db, step, useMb]() -> StepItemData {
        StepItemData res;
        res.step = step;
        if (step == Step::Mojibake) {
            const butler::MojibakeSource src(db);
            if (auto r = src.findGroups(); r.ok()) {
                res.items = r.value();
            }
        } else if (step == Step::Split) {
            const butler::ArtistSplitSource src(db);
            if (auto r = src.findItems(); r.ok()) {
                res.items = r.value();
            }
        } else if (step == Step::Merge) {
            const butler::ArtistMergeSource src(db);
            if (auto r = src.findItems(useMb); r.ok()) {
                res.items = r.value();
            }
        }
        return res;
    });

    m_stepWatcher.setFuture(future);
}

void CleanupController::onStepItemsReady()
{
    if (!m_running) {
        return;
    }
    const auto data = m_stepWatcher.result();
    if (data.step != m_currentStep) {
        return;
    }

    if (data.items.isEmpty()) {
        startNextStep();
        return;
    }

    executeStepWithItems(data.step, data.items);
}

void CleanupController::executeStepWithItems(Step step, const QStringList &items)
{
    QString jobKind;
    QString title;
    library::CorrectionKind batchKind = library::CorrectionKind::Manual;

    if (step == Step::Mojibake) {
        jobKind = QStringLiteral("butler.mojibake");
        title = QStringLiteral("Fix garbled tags");
        batchKind = library::CorrectionKind::Mojibake;
    } else if (step == Step::Split) {
        jobKind = QStringLiteral("butler.artist_split");
        title = QStringLiteral("Split multi-artist credits");
        batchKind = library::CorrectionKind::ArtistSplit;
    } else if (step == Step::Merge) {
        jobKind = QStringLiteral("butler.artist_merge");
        title = QStringLiteral("Merge duplicate artists");
        batchKind = library::CorrectionKind::ArtistMerge;
    } else {
        startNextStep();
        return;
    }

    library::CorrectionStore store(m_db, m_clock);
    auto batchRes = store.createBatch(batchKind, title);
    if (!batchRes.ok()) {
        qCWarning(lcUi, "Failed to create correction batch for step: %s",
            qPrintable(batchRes.error().toString()));
        startNextStep();
        return;
    }

    const qint64 batchId = batchRes.value();
    QJsonObject params;
    params.insert(QStringLiteral("batchId"), batchId);
    const double threshold = autoAcceptThreshold();
    if (threshold > 0.0) {
        params.insert(QStringLiteral("autoAccept"), threshold);
    }
    if (step == Step::Merge) {
        params.insert(QStringLiteral("useMusicBrainz"), m_useMusicBrainzForRun);
    }

    auto enqueueRes = m_jobs.enqueue(jobKind, title, items, params);
    if (!enqueueRes.ok()) {
        qCWarning(
            lcUi, "Failed to enqueue job for step: %s", qPrintable(enqueueRes.error().toString()));
        startNextStep();
        return;
    }

    m_currentJobId = enqueueRes.value();
    emit currentJobIdChanged();

    if (const auto jobInfo = m_jobs.job(m_currentJobId); jobInfo.has_value()) {
        m_stepDone = jobInfo->done;
        m_stepTotal = jobInfo->total;
        m_stepFailed = jobInfo->failed;
        emit progressChanged();
        emit pausedChanged();

        if (jobInfo->state == ai::JobState::Completed) {
            emit batchesChanged();
            startNextStep();
        } else if (jobInfo->state == ai::JobState::Cancelled) {
            cancel();
        }
    }
}

void CleanupController::onJobChanged(qint64 jobId)
{
    if (!m_running || jobId != m_currentJobId) {
        return;
    }
    const auto jobInfo = m_jobs.job(jobId);
    if (!jobInfo.has_value()) {
        return;
    }
    m_stepDone = jobInfo->done;
    m_stepTotal = jobInfo->total;
    m_stepFailed = jobInfo->failed;
    emit progressChanged();
    emit pausedChanged();

    if (jobInfo->state == ai::JobState::Completed) {
        emit batchesChanged();
        startNextStep();
    } else if (jobInfo->state == ai::JobState::Cancelled) {
        m_pendingSteps.clear();
        m_running = false;
        m_currentStep = Step::None;
        m_currentJobId = 0;
        emit runningChanged();
        emit currentStepChanged();
        emit currentJobIdChanged();
        emit pausedChanged();
    }
}

void CleanupController::pause()
{
    if (m_currentJobId > 0) {
        m_jobs.pause(m_currentJobId);
    }
}

void CleanupController::resume()
{
    if (m_currentJobId > 0) {
        m_jobs.resume(m_currentJobId);
    }
}

void CleanupController::cancel()
{
    m_pendingSteps.clear();
    if (m_currentJobId > 0) {
        m_jobs.cancel(m_currentJobId);
    }
    m_running = false;
    m_currentStep = Step::None;
    m_currentJobId = 0;
    emit runningChanged();
    emit currentStepChanged();
    emit currentJobIdChanged();
    emit pausedChanged();
}

} // namespace linernotes::ui
