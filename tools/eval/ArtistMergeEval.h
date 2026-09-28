// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <ai/AiConfig.h>
#include <ai/JobQueue.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/PromptLibrary.h>
#include <ai/SecretStore.h>
#include <ai/UsageStore.h>
#include <butler/MusicBrainzClient.h>
#include <core/Clock.h>
#include <core/Settings.h>
#include <library/Database.h>

#include <memory>

class QNetworkAccessManager;
class QTemporaryDir;

namespace linernotes::eval {

struct EvalConfig {
    QUrl baseUrl;
    QString model;
    QString corpusPath;
    QString keepDbPath;
    bool useMusicBrainz = false;
    int timeoutMs = 60000;
    int requestsPerMinute = 0;
    bool verbose = false;
};

struct CorpusArtist {
    QString name;
    QString entity;
    QStringList albums;
};

struct MisMergeItem {
    QString alias;
    QString canonical;
    QString source;
    double confidence = 0.0;
    QString reason;
    QString aliasEntity;
    QString canonicalEntity;
};

struct MissedPairItem {
    QString nameA;
    QString nameB;
    QString entity;
};

struct SourceReport {
    int proposals = 0;
    int mismerges = 0;
};

class ArtistMergeEval : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ArtistMergeEval)

public:
    explicit ArtistMergeEval(EvalConfig config, QObject *parent = nullptr);
    ~ArtistMergeEval() override;

    bool init();
    void start();

private slots:
    void onJobChanged(qint64 jobId);

private:
    bool loadCorpus();
    bool setupDatabase();
    bool importCorpusToDb();
    void initAiStack();
    void evaluateAndFinish();
    void printReport(const QList<MisMergeItem> &mismerges, const QList<MissedPairItem> &missedPairs,
        const QMap<QString, SourceReport> &sourceStats, int totalProposals, int totalMismerges,
        double mismergeRate, int totalGtPairs, int mergedGtPairs, double recall, qint64 elapsedMs);

    EvalConfig m_config;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QString m_dbPath;
    QString m_settingsPath;

    QList<CorpusArtist> m_corpus;
    QHash<QString, QString> m_nameToEntity;
    QHash<QString, QStringList> m_entityToNames;

    core::SystemClock m_clock;
    std::unique_ptr<core::Settings> m_settings;
    std::unique_ptr<library::Database> m_db;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<ai::AiConfig> m_aiConfig;
    std::unique_ptr<ai::MemorySecretStore> m_secrets;
    std::unique_ptr<ai::LlmClient> m_client;
    std::unique_ptr<ai::LlmCache> m_cache;
    std::unique_ptr<ai::UsageStore> m_usage;
    std::unique_ptr<ai::PrivacyGuard> m_privacy;
    std::unique_ptr<ai::PromptLibrary> m_prompts;
    std::unique_ptr<ai::LlmDebugLog> m_debugLog;
    std::unique_ptr<ai::LlmService> m_llm;
    std::unique_ptr<butler::MusicBrainzClient> m_mbClient;
    std::unique_ptr<ai::JobQueue> m_jobs;

    qint64 m_batchId = 0;
    qint64 m_jobId = 0;
    QElapsedTimer m_evalTimer;
    bool m_finished = false;
};

} // namespace linernotes::eval
