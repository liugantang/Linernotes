// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <ai/AiConfig.h>
#include <ai/AiEnums.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/SecretStore.h>
#include <ai/UsageStore.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <core/Settings.h>
#include <library/Database.h>

#include <memory>
#include <optional>
#include <vector>

class QNetworkAccessManager;
class QTemporaryDir;

namespace linernotes::aicli {

struct AiCliConfig {
    QUrl baseUrl;
    QString model;
    QString dbPath;
    QString prompt { QStringLiteral("Reply with exactly one word: hello") };
    std::optional<QString> systemPrompt;
    bool stream = false;
    bool structured = false;
    int repeatCount = 1;
    int concurrentCount = 0;
    ai::CachePolicy cachePolicy = ai::CachePolicy::Use;
    int timeoutMs = 60000;
    int requestsPerMinute = 0;
    bool verbose = false;
};

class AiCli : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AiCli)

public:
    explicit AiCli(AiCliConfig config, QObject *parent = nullptr);
    ~AiCli() override;

    bool init();
    void start();

private:
    struct ConcurrentTaskContext {
        int index = 0;
        QElapsedTimer timer;
        std::unique_ptr<ai::LlmTask> task;
    };

    void runNextSequential();
    void onSequentialTaskFinished();
    void onConcurrentTaskFinished(ConcurrentTaskContext *ctx);
    void printTaskResult(int index, qint64 elapsedMs, const core::Result<ai::LlmResult> &res);
    void printUsageSummary();
    void finishAll();
    [[nodiscard]] ai::LlmCall createLlmCall() const;

    AiCliConfig m_config;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QString m_dbPath;
    QString m_settingsPath;

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
    std::unique_ptr<ai::LlmDebugLog> m_debugLog;
    std::unique_ptr<ai::LlmService> m_service;

    int m_currentRepeatIndex = 0;
    QElapsedTimer m_sequentialTimer;
    std::unique_ptr<ai::LlmTask> m_sequentialTask;

    std::vector<std::unique_ptr<ConcurrentTaskContext>> m_concurrentTasks;
    int m_activeConcurrentTasks = 0;

    int m_failureCount = 0;
    int m_successCount = 0;
    bool m_finished = false;
};

} // namespace linernotes::aicli
