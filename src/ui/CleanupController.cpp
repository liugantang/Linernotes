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
#include <butler/DuplicateFinder.h>
#include <butler/DuplicateSource.h>
#include <butler/MojibakeSource.h>
#include <butler/TranslationSource.h>
#include <butler/VersionLinker.h>
#include <butler/VersionSuffixSource.h>
#include <core/Clock.h>
#include <core/Settings.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>

namespace linernotes::ui {

namespace {

using HealthReportData = CleanupController::HealthReportData;

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

void collectMojibake(library::Database &db, HealthReportData &report)
{
    const butler::MojibakeSource mojibakeSource(db);
    if (auto res = mojibakeSource.findGroups(); res.ok()) {
        report.mojibakeItems = res.value();
        report.mojibakeGroups = static_cast<int>(report.mojibakeItems.size());
    }
}

void collectCredit(library::Database &db, int promptVersion, HealthReportData &report)
{
    const butler::ArtistCreditSource creditSource(db);
    if (auto res = creditSource.findItems(promptVersion); res.ok()) {
        report.creditItems = res.value();
        report.creditValues = countCreditValues(report.creditItems);
    }
}

void collectMerge(library::Database &db, HealthReportData &report)
{
    const butler::ArtistMergeSource mergeSource(db);
    if (auto res = mergeSource.findItems(); res.ok()) {
        report.mergeItems = res.value();
        report.mergeClusters = countMergeClusters(report.mergeItems);
    }
}

void collectVersions(
    library::Database &db, const core::Clock &clock, int promptVersion, HealthReportData &report)
{
    const butler::VersionLinker versionLinker(db, clock);
    if (auto res = versionLinker.countPending(); res.ok()) {
        report.versionTracks = res.value();
    }

    const butler::VersionSuffixSource versionSuffixSource(db);
    if (auto res = versionSuffixSource.findItems(promptVersion); res.ok()) {
        report.versionSuffixItems = res.value();
    }
    if (auto res = versionSuffixSource.countPending(promptVersion); res.ok()) {
        report.versionSuffixes = res.value();
    }
}

void collectTranslations(library::Database &db, int promptVersion, HealthReportData &report)
{
    const butler::TranslationSource translationSource(db);
    if (auto res = translationSource.findItems(promptVersion); res.ok()) {
        report.translateItems = res.value();
    }
    if (auto res = translationSource.countPending(promptVersion); res.ok()) {
        report.translateTexts = res.value();
    }
}

void collectDuplicates(library::Database &db, const core::Clock &clock, HealthReportData &report)
{
    const butler::DuplicateSource dupSource(db, clock);
    if (auto tracksRes = dupSource.loadTracks(false); tracksRes.ok()) {
        const auto clusters = butler::candidateClusters(tracksRes.value());
        for (const auto &cluster : clusters) {
            report.duplicateCandidates += static_cast<int>(cluster.size());
        }
    }
    if (auto fpCandRes = dupSource.fingerprintCandidates(); fpCandRes.ok()) {
        report.fingerprintPending = static_cast<int>(fpCandRes.value().size());
    }
    if (auto grpRes = dupSource.countGroups(); grpRes.ok()) {
        for (const int count : grpRes.value()) {
            report.duplicateGroups += count;
        }
    }
}

bool isCachedCreditItem(const QString &itemKey)
{
    const auto doc = QJsonDocument::fromJson(itemKey.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    return doc.object().value(QStringLiteral("type")).toString() == QLatin1StringView("cached");
}

int estimateTokens(ai::JobQueue &jobs, const QString &kind, const QStringList &items)
{
    if (auto est = jobs.estimate(kind, items); est.ok()) {
        return est.value().promptTokens + est.value().completionTokens;
    }
    return 0;
}

/// 结果或空列表（查询失败时记日志由各 Source 负责）。
QStringList valueOrEmpty(core::Result<QStringList> res)
{
    return res.ok() ? std::move(res).value() : QStringList { };
}

QStringList idsOrEmpty(const core::Result<QList<qint64>> &res)
{
    QStringList items;
    if (res.ok()) {
        for (const qint64 id : res.value()) {
            items.append(QString::number(id));
        }
    }
    return items;
}

/// 自动整理只处理已有 LLM 缓存的署名，不触发新的 LLM 调用。
QStringList creditItems(library::Database &db, int promptVersion, bool automatic)
{
    QStringList items = valueOrEmpty(butler::ArtistCreditSource(db).findItems(promptVersion));
    if (automatic) {
        items.removeIf([](const QString &item) { return !isCachedCreditItem(item); });
    }
    return items;
}

QStringList collectStepItems(library::Database &db, const core::Clock &clock,
    CleanupController::Step step, int promptVersion, bool automatic = false)
{
    using Step = CleanupController::Step;
    switch (step) {
    case Step::Mojibake:
        return valueOrEmpty(butler::MojibakeSource(db).findGroups());
    case Step::Credit:
        return creditItems(db, promptVersion, automatic);
    case Step::Merge:
        return valueOrEmpty(butler::ArtistMergeSource(db).findItems());
    case Step::VersionSuffix:
        return valueOrEmpty(butler::VersionSuffixSource(db).findItems(promptVersion));
    case Step::Translate:
        return valueOrEmpty(butler::TranslationSource(db).findItems(promptVersion));
    case Step::Fingerprint:
        return idsOrEmpty(butler::DuplicateSource(db, clock).fingerprintCandidates());
    case Step::VersionLink:
    case Step::Duplicates:
        return QStringList { QStringLiteral("all") };
    case Step::None:
        break;
    }
    return { };
}

int loadPromptVersion(const ai::PromptLibrary &prompts, const QString &name)
{
    const auto promptRes = prompts.load(name);
    if (!promptRes.ok()) {
        qCWarning(lcUi, "Failed to load %s prompt: %s", qPrintable(name),
            qPrintable(promptRes.error().toString()));
        return 0;
    }
    return promptRes.value().version;
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

int CleanupController::versionTracks() const
{
    return m_versionTracks;
}

int CleanupController::versionSuffixes() const
{
    return m_versionSuffixes;
}

int CleanupController::translateTexts() const
{
    return m_translateTexts;
}

int CleanupController::duplicateCandidates() const
{
    return m_duplicateCandidates;
}

int CleanupController::fingerprintPending() const
{
    return m_fingerprintPending;
}

int CleanupController::duplicateGroups() const
{
    return m_duplicateGroups;
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

int CleanupController::versionTokens() const
{
    return m_versionTokens;
}

int CleanupController::translateTokens() const
{
    return m_translateTokens;
}

bool CleanupController::isRunning() const
{
    return m_running;
}

bool CleanupController::isAutomatic() const
{
    return m_automatic;
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

    const int creditPromptVersion
        = loadPromptVersion(m_prompts, QStringLiteral("cleanup/artist_credit"));
    const int versionPromptVersion
        = loadPromptVersion(m_prompts, QStringLiteral("cleanup/version_suffix"));
    const int translatePromptVersion
        = loadPromptVersion(m_prompts, QStringLiteral("cleanup/translate_titles"));

    auto future = QtConcurrent::run(
        [&db = m_db, &clock = m_clock, creditPromptVersion, versionPromptVersion,
            translatePromptVersion]() -> HealthReportData {
            HealthReportData report;
            collectMojibake(db, report);
            collectCredit(db, creditPromptVersion, report);
            collectMerge(db, report);
            collectVersions(db, clock, versionPromptVersion, report);
            collectTranslations(db, translatePromptVersion, report);
            collectDuplicates(db, clock, report);
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
    m_versionTracks = data.versionTracks;
    m_versionSuffixes = data.versionSuffixes;
    m_translateTexts = data.translateTexts;
    m_duplicateCandidates = data.duplicateCandidates;
    m_fingerprintPending = data.fingerprintPending;
    m_duplicateGroups = data.duplicateGroups;

    m_mojibakeTokens
        = estimateTokens(m_jobs, QStringLiteral("butler.mojibake"), data.mojibakeItems);
    m_creditTokens
        = estimateTokens(m_jobs, QStringLiteral("butler.artist_credit"), data.creditItems);
    m_mergeTokens = estimateTokens(m_jobs, QStringLiteral("butler.artist_merge"), data.mergeItems);
    m_versionTokens
        = estimateTokens(m_jobs, QStringLiteral("butler.version_suffix"), data.versionSuffixItems);
    m_translateTokens
        = estimateTokens(m_jobs, QStringLiteral("butler.translate"), data.translateItems);

    m_healthReady = true;
    m_checking = false;
    emit checkingChanged();
    emit healthChanged();
}

void CleanupController::run(
    bool mojibake, bool credit, bool merge, bool versions, bool translate, bool duplicates)
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
    if (versions) {
        m_pendingSteps.append(Step::VersionSuffix);
        m_pendingSteps.append(Step::VersionLink);
    }
    if (translate) {
        m_pendingSteps.append(Step::Translate);
    }
    if (duplicates) {
        m_pendingSteps.append(Step::Fingerprint);
        m_pendingSteps.append(Step::Duplicates);
    }

    if (m_pendingSteps.isEmpty()) {
        return;
    }

    m_automatic = false;
    m_running = true;
    emit runningChanged();
    startNextStep();
}

void CleanupController::runAutomatic()
{
    if (m_running) {
        return;
    }

    m_pendingSteps.clear();
    m_pendingSteps.append(Step::Mojibake);
    m_pendingSteps.append(Step::Credit);
    m_pendingSteps.append(Step::VersionLink);

    m_automatic = true;
    m_running = true;
    emit runningChanged();
    startNextStep();
}

void CleanupController::startNextStep()
{
    if (m_pendingSteps.isEmpty()) {
        m_running = false;
        m_automatic = false;
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
    } else if (step == Step::VersionSuffix) {
        if (const auto promptRes = m_prompts.load(QStringLiteral("cleanup/version_suffix"));
            promptRes.ok()) {
            promptVersion = promptRes.value().version;
        } else {
            qCWarning(lcUi, "Failed to load cleanup/version_suffix prompt: %s",
                qPrintable(promptRes.error().toString()));
        }
    } else if (step == Step::Translate) {
        if (const auto promptRes = m_prompts.load(QStringLiteral("cleanup/translate_titles"));
            promptRes.ok()) {
            promptVersion = promptRes.value().version;
        } else {
            qCWarning(lcUi, "Failed to load cleanup/translate_titles prompt: %s",
                qPrintable(promptRes.error().toString()));
        }
    }

    const bool automatic = m_automatic;
    auto future = QtConcurrent::run(
        [&db = m_db, &clock = m_clock, step, promptVersion, automatic]() -> StepItemData {
            return StepItemData {
                .step = step,
                .items = collectStepItems(db, clock, step, promptVersion, automatic),
            };
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
    case Step::VersionSuffix:
        jobKind = QStringLiteral("butler.version_suffix");
        title = QStringLiteral("Classify title suffixes");
        break;
    case Step::VersionLink:
        jobKind = QStringLiteral("butler.version_link");
        title = QStringLiteral("Group song versions");
        break;
    case Step::Translate:
        jobKind = QStringLiteral("butler.translate");
        title = QStringLiteral("Translate foreign titles");
        break;
    case Step::Fingerprint:
        jobKind = QStringLiteral("butler.fingerprint");
        title = QStringLiteral("Compute audio fingerprints");
        break;
    case Step::Duplicates:
        jobKind = QStringLiteral("butler.duplicates");
        title = QStringLiteral("Find duplicate songs");
        break;
    case Step::None:
    default:
        startNextStep();
        return;
    }

    if (m_automatic) {
        title = QStringLiteral("Automatic: ") + title;
    }

    QJsonObject params;
    if (m_automatic && step == Step::Mojibake) {
        params.insert(QStringLiteral("useLlm"), false);
    }

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
        m_automatic = false;
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
    m_automatic = false;
    m_currentStep = Step::None;
    m_currentJobId = 0;
    emit runningChanged();
    emit currentStepChanged();
    emit currentJobIdChanged();
    emit pausedChanged();
}

} // namespace linernotes::ui
