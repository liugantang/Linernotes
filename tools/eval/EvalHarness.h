// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
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

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QTemporaryDir;

namespace linernotes::eval {

struct EvalConfig {
    QUrl baseUrl;
    QString model { };
    QString corpusPath { };
    QString libraryPath { };
    QString keepDbPath { };
    bool useMusicBrainz = false;
    int timeoutMs = 60000;
    int requestsPerMinute = 0;
    bool verbose = false;
};

qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"));
qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path);
qint64 insertTrack(const QSqlDatabase &db, qint64 fileId);
bool insertRawTag(const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value);
bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000);

class EvalHarness : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(EvalHarness)

public:
    explicit EvalHarness(EvalConfig config, QObject *parent = nullptr);
    ~EvalHarness() override;

    [[nodiscard]] const EvalConfig &config() const { return m_config; }
    [[nodiscard]] core::SystemClock &clock() { return m_clock; }
    [[nodiscard]] core::Settings &settings() { return *m_settings; }
    [[nodiscard]] library::Database &db() { return *m_db; }
    [[nodiscard]] ai::JobQueue &jobs() { return *m_jobs; }
    [[nodiscard]] ai::PromptLibrary &prompts() { return *m_prompts; }
    [[nodiscard]] ai::UsageStore &usage() { return *m_usage; }

    bool init();
    bool runJob(const QString &kind, const QString &title, const QStringList &items,
        const QJsonObject &params, std::function<void()> onFinished);

    void printLlmUsage();
    [[nodiscard]] qint64 elapsedMs() const;

private slots:
    void onJobChanged(qint64 jobId);

private:
    bool setupDatabase();
    bool copyLibraryDb(const QString &sourcePath, const QString &destPath);
    void initAiStack();

    EvalConfig m_config;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QString m_dbPath { };
    QString m_settingsPath { };

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

    qint64 m_jobId = 0;
    std::function<void()> m_onFinished;
    QElapsedTimer m_timer;
    bool m_finished = false;
};

} // namespace linernotes::eval
