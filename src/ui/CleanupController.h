// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace linernotes::core {
class Clock;
class Settings;
} // namespace linernotes::core

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::ai {
class AiConfig;
class JobQueue;
class PromptLibrary;
} // namespace linernotes::ai

namespace linernotes::ui {

class CleanupController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CleanupController)

public:
    enum class Step : std::uint8_t {
        None,
        Mojibake,
        Credit,
        Merge,
        MbMatch,
        CoverArt,
        VersionSuffix,
        VersionLink,
        Translate,
        Fingerprint,
        Duplicates,
    };
    Q_ENUM(Step)

    // Health report
    Q_PROPERTY(bool checking READ isChecking NOTIFY checkingChanged)
    Q_PROPERTY(bool healthReady READ isHealthReady NOTIFY healthChanged)
    Q_PROPERTY(int mojibakeGroups READ mojibakeGroups NOTIFY healthChanged)
    Q_PROPERTY(int creditValues READ creditValues NOTIFY healthChanged)
    Q_PROPERTY(int mergeClusters READ mergeClusters NOTIFY healthChanged)
    Q_PROPERTY(int mbMatchAlbums READ mbMatchAlbums NOTIFY healthChanged)
    Q_PROPERTY(int coverArtAlbums READ coverArtAlbums NOTIFY healthChanged)
    Q_PROPERTY(int versionTracks READ versionTracks NOTIFY healthChanged)
    Q_PROPERTY(int versionSuffixes READ versionSuffixes NOTIFY healthChanged)
    Q_PROPERTY(int translateTexts READ translateTexts NOTIFY healthChanged)
    Q_PROPERTY(int duplicateCandidates READ duplicateCandidates NOTIFY healthChanged)
    Q_PROPERTY(int fingerprintPending READ fingerprintPending NOTIFY healthChanged)
    Q_PROPERTY(int duplicateGroups READ duplicateGroups NOTIFY healthChanged)
    Q_PROPERTY(int mojibakeTokens READ mojibakeTokens NOTIFY healthChanged)
    Q_PROPERTY(int creditTokens READ creditTokens NOTIFY healthChanged)
    Q_PROPERTY(int mergeTokens READ mergeTokens NOTIFY healthChanged)
    Q_PROPERTY(int versionTokens READ versionTokens NOTIFY healthChanged)
    Q_PROPERTY(int translateTokens READ translateTokens NOTIFY healthChanged)

    // Run / Progress
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(bool automatic READ isAutomatic NOTIFY runningChanged)
    Q_PROPERTY(Step currentStep READ currentStep NOTIFY currentStepChanged)
    Q_PROPERTY(int stepDone READ stepDone NOTIFY progressChanged)
    Q_PROPERTY(int stepTotal READ stepTotal NOTIFY progressChanged)
    Q_PROPERTY(int stepFailed READ stepFailed NOTIFY progressChanged)
    Q_PROPERTY(qint64 currentJobId READ currentJobId NOTIFY currentJobIdChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)

    // Settings
    Q_PROPERTY(double autoAcceptThreshold READ autoAcceptThreshold WRITE setAutoAcceptThreshold
            NOTIFY autoAcceptThresholdChanged)
    Q_PROPERTY(bool llmConfigured READ isLlmConfigured NOTIFY llmConfiguredChanged)

    struct HealthReportData {
        int mojibakeGroups = 0;
        QStringList mojibakeItems;
        int creditValues = 0;
        QStringList creditItems;
        int mergeClusters = 0;
        QStringList mergeItems;
        int mbMatchAlbums = 0;
        QStringList mbMatchItems;
        int coverArtAlbums = 0;
        QStringList coverArtItems;
        int versionTracks = 0;
        int versionSuffixes = 0;
        QStringList versionSuffixItems;
        int translateTexts = 0;
        QStringList translateItems;
        int duplicateCandidates = 0;
        int fingerprintPending = 0;
        int duplicateGroups = 0;
    };

    CleanupController(library::Database &db, const core::Clock &clock, ai::JobQueue &jobs,
        const ai::PromptLibrary &prompts, const ai::AiConfig &aiConfig, core::Settings &settings,
        QObject *parent = nullptr);
    ~CleanupController() override;

    [[nodiscard]] bool isChecking() const;
    [[nodiscard]] bool isHealthReady() const;
    [[nodiscard]] int mojibakeGroups() const;
    [[nodiscard]] int creditValues() const;
    [[nodiscard]] int mergeClusters() const;
    [[nodiscard]] int mbMatchAlbums() const;
    [[nodiscard]] int coverArtAlbums() const;
    [[nodiscard]] int versionTracks() const;
    [[nodiscard]] int versionSuffixes() const;
    [[nodiscard]] int translateTexts() const;
    [[nodiscard]] int duplicateCandidates() const;
    [[nodiscard]] int fingerprintPending() const;
    [[nodiscard]] int duplicateGroups() const;
    [[nodiscard]] int mojibakeTokens() const;
    [[nodiscard]] int creditTokens() const;
    [[nodiscard]] int mergeTokens() const;
    [[nodiscard]] int versionTokens() const;
    [[nodiscard]] int translateTokens() const;

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isAutomatic() const;
    [[nodiscard]] Step currentStep() const;
    [[nodiscard]] int stepDone() const;
    [[nodiscard]] int stepTotal() const;
    [[nodiscard]] int stepFailed() const;
    [[nodiscard]] qint64 currentJobId() const;
    [[nodiscard]] bool isPaused() const;

    [[nodiscard]] double autoAcceptThreshold() const;
    void setAutoAcceptThreshold(double threshold);
    [[nodiscard]] bool isLlmConfigured() const;

    Q_INVOKABLE void checkHealth();
    Q_INVOKABLE void run(bool mojibake, bool credit, bool merge, bool mbMatch, bool versions,
        bool translate = false, bool duplicates = false);
    Q_INVOKABLE void runAutomatic();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();

signals:
    void checkingChanged();
    void healthChanged();
    void runningChanged();
    void currentStepChanged();
    void progressChanged();
    void currentJobIdChanged();
    void pausedChanged();
    void autoAcceptThresholdChanged();
    void llmConfiguredChanged();
    void batchesChanged();

private:
    struct StepItemData {
        Step step = Step::None;
        QStringList items;
    };

    void onHealthCheckFinished();
    void onStepItemsReady();
    void onJobChanged(qint64 jobId);
    void refreshLlmConfigured();
    void startNextStep();
    void executeStepWithItems(Step step, const QStringList &items);
    void cleanupCurrentBatchIfEmpty();

    library::Database &m_db;
    const core::Clock &m_clock;
    ai::JobQueue &m_jobs;
    const ai::PromptLibrary &m_prompts;
    const ai::AiConfig &m_aiConfig;
    core::Settings &m_settings;

    bool m_checking = false;
    bool m_healthReady = false;
    int m_mojibakeGroups = 0;
    int m_creditValues = 0;
    int m_mergeClusters = 0;
    int m_mbMatchAlbums = 0;
    int m_coverArtAlbums = 0;
    int m_versionTracks = 0;
    int m_versionSuffixes = 0;
    int m_translateTexts = 0;
    int m_duplicateCandidates = 0;
    int m_fingerprintPending = 0;
    int m_duplicateGroups = 0;
    int m_mojibakeTokens = 0;
    int m_creditTokens = 0;
    int m_mergeTokens = 0;
    int m_versionTokens = 0;
    int m_translateTokens = 0;
    QFutureWatcher<HealthReportData> m_healthWatcher;

    bool m_running = false;
    bool m_automatic = false;
    Step m_currentStep = Step::None;
    qint64 m_currentJobId = 0;
    qint64 m_currentBatchId = 0;
    int m_stepDone = 0;
    int m_stepTotal = 0;
    int m_stepFailed = 0;
    QList<Step> m_pendingSteps;
    QFutureWatcher<StepItemData> m_stepWatcher;

    bool m_llmConfigured = false;
};

} // namespace linernotes::ui
