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
#include <ai/PromptLibrary.h>
#include <butler/ArtistCreditSource.h>
#include <butler/ArtistMergeSource.h>
#include <butler/CoverArtSource.h>
#include <butler/MbMatchSource.h>
#include <butler/MojibakeSource.h>
#include <core/Clock.h>
#include <core/Settings.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>

namespace linernotes::ui {

namespace {

int countCreditValues(const QStringList &items)
{
    int count = 0;
    for (const auto &key : items) {
        const auto doc = QJsonDocument::fromJson(key.toUtf8());
        if (doc.isObject()) {
            const auto obj = doc.object();
            count += static_cast<int>(obj.value(QStringLiteral("values")).toArray().size());
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
            if (type == QLatin1StringView("group") || type == QLatin1StringView("cluster")) {
                count += 1;
            } else if (type == QLatin1StringView("confirm")) {
                count += static_cast<int>(obj.value(QStringLiteral("groups")).toArray().size());
            } else if (type == QLatin1StringView("fuzzy")) {
                count += static_cast<int>(obj.value(QStringLiteral("pairs")).toArray().size());
            }
        }
    }
    return count;
}

QStringList collectStepItems(library::Database &db, CleanupController::Step step, int promptVersion)
{
    using Step = CleanupController::Step;
    QStringList items;
    if (step == Step::Mojibake) {
        const butler::MojibakeSource src(db);
        if (auto r = src.findGroups(); r.ok()) {
            items = r.value();
        }
    } else if (step == Step::Credit) {
        const butler::ArtistCreditSource src(db);
        if (auto r = src.findItems(promptVersion); r.ok()) {
            items = r.value();
        }
    } else if (step == Step::Merge) {
        const butler::ArtistMergeSource src(db);
        if (auto r = src.findItems(); r.ok()) {
            items = r.value();
        }
    } else if (step == Step::MbMatch) {
        const butler::MbMatchSource src(db);
        if (auto r = src.pendingAlbums(); r.ok()) {
            for (const qint64 id : r.value()) {
                items.append(QString::number(id));
            }
        }
    } else if (step == Step::CoverArt) {
        const butler::CoverArtSource src(db);
        if (auto r = src.pendingAlbums(); r.ok()) {
            for (const qint64 id : r.value()) {
                items.append(QString::number(id));
            }
        }
    }
    return items;
}

} // namespace

CleanupController::CleanupController(library::Database &db, const core::Clock &clock,
    ai::JobQueue &jobs, const ai::PromptLibrary &prompts, const ai::AiConfig &aiConfig,
    core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
    , m_jobs(jobs)
    , m_prompts(prompts)
    , m_aiConfig(aiConfig)
    , m_settings(settings)
{
    m_llmConfigured = m_aiConfig.resolve(ai::Purpose::Cleanup).has_value();

    connect(&m_healthWatcher, &QFutureWatcher<HealthReportData>::finished, this,
        &CleanupController::onHealthCheckFinished);
    connect(&m_stepWatcher, &QFutureWatcher<StepItemData>::finished, this,
        &CleanupController::onStepItemsReady);
    connect(&m_jobs, &ai::JobQueue::jobChanged, this, &CleanupController::onJobChanged);
    connect(&m_aiConfig, &ai::AiConfig::changed, this, &CleanupController::refreshLlmConfigured);
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

int CleanupController::creditValues() const
{
    return m_creditValues;
}

int CleanupController::mergeClusters() const
{
    return m_mergeClusters;
}

int CleanupController::mbMatchAlbums() const
{
    return m_mbMatchAlbums;
}

int CleanupController::coverArtAlbums() const
{
    return m_coverArtAlbums;
}

int CleanupController::mojibakeTokens() const
{
    return m_mojibakeTokens;
}

int CleanupController::creditTokens() const
{
    return m_creditTokens;
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

    int promptVersion = 0;
    if (const auto promptRes = m_prompts.load(QStringLiteral("cleanup/artist_credit"));
        promptRes.ok()) {
        promptVersion = promptRes.value().version;
    } else {
        qCWarning(lcUi, "Failed to load cleanup/artist_credit prompt: %s",
            qPrintable(promptRes.error().toString()));
    }

    auto future = QtConcurrent::run([&db = m_db, promptVersion]() -> HealthReportData {
        HealthReportData report;

        const butler::MojibakeSource mojibakeSource(db);
        if (auto res = mojibakeSource.findGroups(); res.ok()) {
            report.mojibakeItems = res.value();
            report.mojibakeGroups = static_cast<int>(report.mojibakeItems.size());
        }

        const butler::ArtistCreditSource creditSource(db);
        if (auto res = creditSource.findItems(promptVersion); res.ok()) {
            report.creditItems = res.value();
            report.creditValues = countCreditValues(report.creditItems);
        }

        const butler::ArtistMergeSource mergeSource(db);
        if (auto res = mergeSource.findItems(); res.ok()) {
            report.mergeItems = res.value();
            report.mergeClusters = countMergeClusters(report.mergeItems);
        }

        const butler::MbMatchSource mbMatchSource(db);
        if (auto res = mbMatchSource.pendingAlbums(); res.ok()) {
            for (const qint64 id : res.value()) {
                report.mbMatchItems.append(QString::number(id));
            }
            report.mbMatchAlbums = static_cast<int>(report.mbMatchItems.size());
        }

        const butler::CoverArtSource coverArtSource(db);
        if (auto res = coverArtSource.pendingAlbums(); res.ok()) {
            for (const qint64 id : res.value()) {
                report.coverArtItems.append(QString::number(id));
            }
            report.coverArtAlbums = static_cast<int>(report.coverArtItems.size());
        }

        return report;
    });

    m_healthWatcher.setFuture(future);
}

void CleanupController::onHealthCheckFinished()
{
    const auto data = m_healthWatcher.result();
    m_mojibakeGroups = data.mojibakeGroups;
    m_creditValues = data.creditValues;
    m_mergeClusters = data.mergeClusters;
    m_mbMatchAlbums = data.mbMatchAlbums;
    m_coverArtAlbums = data.coverArtAlbums;

    if (auto est = m_jobs.estimate(QStringLiteral("butler.mojibake"), data.mojibakeItems);
        est.ok()) {
        m_mojibakeTokens = est.value().promptTokens + est.value().completionTokens;
    } else {
        m_mojibakeTokens = 0;
    }

    if (auto est = m_jobs.estimate(QStringLiteral("butler.artist_credit"), data.creditItems);
        est.ok()) {
        m_creditTokens = est.value().promptTokens + est.value().completionTokens;
    } else {
        m_creditTokens = 0;
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

void CleanupController::run(bool mojibake, bool credit, bool merge, bool mbMatch)
{
    if (m_running) {
        return;
    }

    m_pendingSteps.clear();
    if (mojibake) {
        m_pendingSteps.append(Step::Mojibake);
    }
    if (credit) {
        m_pendingSteps.append(Step::Credit);
    }
    if (merge) {
        m_pendingSteps.append(Step::Merge);
    }
    if (mbMatch) {
        m_pendingSteps.append(Step::MbMatch);
        m_pendingSteps.append(Step::CoverArt);
    }

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
        checkHealth(); // 运行结束后刷新体检数字
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

    int promptVersion = 0;
    if (step == Step::Credit) {
        if (const auto promptRes = m_prompts.load(QStringLiteral("cleanup/artist_credit"));
            promptRes.ok()) {
            promptVersion = promptRes.value().version;
        } else {
            qCWarning(lcUi, "Failed to load cleanup/artist_credit prompt: %s",
                qPrintable(promptRes.error().toString()));
        }
    }

    auto future = QtConcurrent::run([&db = m_db, step, promptVersion]() -> StepItemData {
        return StepItemData { .step = step, .items = collectStepItems(db, step, promptVersion) };
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
    std::optional<library::CorrectionKind> batchKind;

    switch (step) {
    case Step::Mojibake:
        jobKind = QStringLiteral("butler.mojibake");
        title = QStringLiteral("Fix garbled tags");
        batchKind = library::CorrectionKind::Mojibake;
        break;
    case Step::Credit:
        jobKind = QStringLiteral("butler.artist_credit");
        title = QStringLiteral("Normalize artist credits");
        batchKind = library::CorrectionKind::ArtistCredit;
        break;
    case Step::Merge:
        jobKind = QStringLiteral("butler.artist_merge");
        title = QStringLiteral("Merge duplicate artists");
        batchKind = library::CorrectionKind::ArtistMerge;
        break;
    case Step::MbMatch:
        jobKind = QStringLiteral("butler.mb_match");
        title = QStringLiteral("Fill in from MusicBrainz");
        batchKind = library::CorrectionKind::MbMatch;
        break;
    case Step::CoverArt:
        jobKind = QStringLiteral("butler.cover_art");
        title = QStringLiteral("Download covers");
        break;
    case Step::None:
    default:
        startNextStep();
        return;
    }

    QJsonObject params;
    if (batchKind.has_value()) {
        library::CorrectionStore store(m_db, m_clock);
        auto batchRes = store.createBatch(batchKind.value(), title);
        if (!batchRes.ok()) {
            qCWarning(lcUi, "Failed to create correction batch for step: %s",
                qPrintable(batchRes.error().toString()));
            startNextStep();
            return;
        }

        m_currentBatchId = batchRes.value();
        params.insert(QStringLiteral("batchId"), m_currentBatchId);
        const double threshold = autoAcceptThreshold();
        if (threshold > 0.0) {
            params.insert(QStringLiteral("autoAccept"), threshold);
        }
    }

    auto enqueueRes = m_jobs.enqueue(jobKind, title, items, params);
    if (!enqueueRes.ok()) {
        qCWarning(
            lcUi, "Failed to enqueue job for step: %s", qPrintable(enqueueRes.error().toString()));
        cleanupCurrentBatchIfEmpty();
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
            cleanupCurrentBatchIfEmpty();
            emit batchesChanged();
            startNextStep();
        } else if (jobInfo->state == ai::JobState::Cancelled) {
            cleanupCurrentBatchIfEmpty();
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
        cleanupCurrentBatchIfEmpty();
        emit batchesChanged();
        startNextStep();
    } else if (jobInfo->state == ai::JobState::Cancelled) {
        cleanupCurrentBatchIfEmpty();
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

void CleanupController::cleanupCurrentBatchIfEmpty()
{
    if (m_currentBatchId <= 0) {
        return;
    }
    const qint64 batchId = m_currentBatchId;
    m_currentBatchId = 0;

    library::CorrectionStore store(m_db, m_clock);
    const auto res = store.deleteBatchIfEmpty(batchId);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to delete empty batch %lld: %s", batchId,
            qPrintable(res.error().toString()));
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
    cleanupCurrentBatchIfEmpty();
    m_running = false;
    m_currentStep = Step::None;
    m_currentJobId = 0;
    emit runningChanged();
    emit currentStepChanged();
    emit currentJobIdChanged();
    emit pausedChanged();
}

} // namespace linernotes::ui
