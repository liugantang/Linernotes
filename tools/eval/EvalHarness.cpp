// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EvalHarness.h"

#include <QCoreApplication>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <butler/ArtistCreditJobHandler.h>
#include <butler/ArtistMergeJobHandler.h>
#include <library/Migrator.h>

#include <iostream>
#include <utility>

namespace linernotes::eval {

qint64 insertRoot(const QSqlDatabase &db, const QString &path)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
    q.addBindValue(path);
    q.addBindValue(1000);
    return q.exec() ? q.lastInsertId().toLongLong() : -1;
}

qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
{
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
                       "VALUES (?, ?, 1048576, 2000, 2000, 2000);"));
    q.addBindValue(rootId);
    q.addBindValue(path);
    return q.exec() ? q.lastInsertId().toLongLong() : -1;
}

qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
        "VALUES (?, NULL, NULL, 0, 3000);"));
    q.addBindValue(fileId);
    return q.exec() ? q.lastInsertId().toLongLong() : -1;
}

bool insertRawTag(const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
{
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
                       "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
    q.addBindValue(trackId);
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
    q.addBindValue(timestamp);
    q.addBindValue(trackId);
    return q.exec();
}

EvalHarness::EvalHarness(EvalConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

EvalHarness::~EvalHarness() = default;

bool EvalHarness::init()
{
    if (!setupDatabase()) {
        return false;
    }
    initAiStack();
    return true;
}

bool EvalHarness::copyLibraryDb(const QString &sourcePath, const QString &destPath)
{
    if (QFile::exists(destPath)) {
        if (!QFile::remove(destPath)) {
            std::cerr << "Failed to remove existing destination database: " << qPrintable(destPath)
                      << "\n";
            return false;
        }
    }

    library::Database srcDb(sourcePath);
    const auto connRes = srcDb.connection();
    if (!connRes.ok()) {
        std::cerr << "Failed to open source library database: "
                  << qPrintable(connRes.error().toString()) << "\n";
        return false;
    }

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("VACUUM INTO :dest;"));
    q.bindValue(QStringLiteral(":dest"), destPath);
    if (!q.exec()) {
        std::cerr << "Failed to copy database via VACUUM INTO: " << qPrintable(q.lastError().text())
                  << "\n";
        return false;
    }

    return true;
}

bool EvalHarness::setupDatabase()
{
    if (m_config.keepDbPath.isEmpty()) {
        m_tempDir = std::make_unique<QTemporaryDir>();
        if (!m_tempDir->isValid()) {
            std::cerr << "Failed to create temporary directory\n";
            return false;
        }
        m_dbPath = m_tempDir->filePath(QStringLiteral("eval.db"));
        m_settingsPath = m_tempDir->filePath(QStringLiteral("settings.ini"));
    } else {
        m_dbPath = m_config.keepDbPath;
        m_tempDir = std::make_unique<QTemporaryDir>();
        if (!m_tempDir->isValid()) {
            std::cerr << "Failed to create temporary directory for settings\n";
            return false;
        }
        m_settingsPath = m_tempDir->filePath(QStringLiteral("settings.ini"));
    }

    if (!m_config.libraryPath.isEmpty()) {
        if (!copyLibraryDb(m_config.libraryPath, m_dbPath)) {
            return false;
        }
    }

    m_settings = std::make_unique<core::Settings>(m_settingsPath);
    m_db = std::make_unique<library::Database>(m_dbPath);

    const library::Migrator migrator;
    const auto openRes = m_db->open(migrator);
    if (!openRes.ok()) {
        std::cerr << "Database error: " << qPrintable(openRes.error().toString()) << "\n";
        return false;
    }

    return true;
}

void EvalHarness::initAiStack()
{
    m_network = std::make_unique<QNetworkAccessManager>();
    m_aiConfig = std::make_unique<ai::AiConfig>(*m_settings);
    m_secrets = std::make_unique<ai::MemorySecretStore>();
    m_client = std::make_unique<ai::LlmClient>(*m_network);
    m_cache = std::make_unique<ai::LlmCache>(*m_db, m_clock);
    m_usage = std::make_unique<ai::UsageStore>(*m_db);
    m_privacy = std::make_unique<ai::PrivacyGuard>(*m_settings);
    m_prompts = std::make_unique<ai::PromptLibrary>(QStringList { QStringLiteral(":/prompts") });
    m_debugLog = std::make_unique<ai::LlmDebugLog>(*m_settings);
    m_llm = std::make_unique<ai::LlmService>(
        *m_aiConfig, *m_secrets, *m_client, *m_cache, *m_usage, *m_privacy, *m_debugLog, m_clock);
    m_mbClient = std::make_unique<butler::MusicBrainzClient>(*m_network, *m_db, m_clock);
    m_jobs = std::make_unique<ai::JobQueue>(*m_db, m_clock);

    m_jobs->registerHandler(std::make_unique<butler::ArtistMergeJobHandler>(
        *m_db, *m_llm, *m_prompts, *m_mbClient, m_clock));
    m_jobs->registerHandler(
        std::make_unique<butler::ArtistCreditJobHandler>(*m_db, *m_llm, *m_prompts, m_clock));

    ai::ServiceProfile profile;
    profile.name = QStringLiteral("Eval Service");
    profile.baseUrl = m_config.baseUrl;
    profile.defaultModel = m_config.model;
    profile.timeoutMs = m_config.timeoutMs;
    profile.requestsPerMinute = m_config.requestsPerMinute;
    profile.maxConcurrent = 2;

    const QString serviceId = m_aiConfig->saveService(profile);
    m_aiConfig->setDefaultServiceId(serviceId);

    const QString apiKey = qEnvironmentVariable("LINERNOTES_LLM_KEY");
    if (!apiKey.isEmpty()) {
        m_secrets->write(serviceId, apiKey, nullptr, nullptr);
    }

    connect(m_jobs.get(), &ai::JobQueue::jobChanged, this, &EvalHarness::onJobChanged);
}

bool EvalHarness::runJob(const QString &kind, const QString &title, const QStringList &items,
    const QJsonObject &params, std::function<void()> onFinished)
{
    m_timer.start();
    m_onFinished = std::move(onFinished);
    m_finished = false;

    const auto enqueueRes = m_jobs->enqueue(kind, title, items, params);
    if (!enqueueRes.ok()) {
        std::cerr << "Failed to enqueue job: " << qPrintable(enqueueRes.error().toString()) << "\n";
        return false;
    }
    m_jobId = enqueueRes.value();

    if (const auto jobInfo = m_jobs->job(m_jobId); jobInfo.has_value()) {
        if (jobInfo->state == ai::JobState::Completed) {
            m_finished = true;
            if (m_onFinished) {
                m_onFinished();
            }
        } else if (jobInfo->state == ai::JobState::Paused) {
            std::cerr << "Job paused with error: " << qPrintable(jobInfo->lastError) << "\n";
            m_finished = true;
            QCoreApplication::exit(2);
        } else if (jobInfo->state == ai::JobState::Cancelled) {
            std::cerr << "Job cancelled\n";
            m_finished = true;
            QCoreApplication::exit(2);
        }
    }

    return true;
}

void EvalHarness::onJobChanged(qint64 jobId)
{
    if (m_finished || jobId != m_jobId) {
        return;
    }

    const auto jobInfo = m_jobs->job(jobId);
    if (!jobInfo.has_value()) {
        return;
    }

    if (m_config.verbose) {
        std::cout << "Job progress: " << jobInfo->done << "/" << jobInfo->total
                  << " (failed: " << jobInfo->failed << ")\n";
    }

    if (jobInfo->state == ai::JobState::Completed) {
        m_finished = true;
        if (m_onFinished) {
            m_onFinished();
        }
    } else if (jobInfo->state == ai::JobState::Paused) {
        std::cerr << "Job paused with error: " << qPrintable(jobInfo->lastError) << "\n";
        m_finished = true;
        QCoreApplication::exit(2);
    } else if (jobInfo->state == ai::JobState::Cancelled) {
        std::cerr << "Job cancelled\n";
        m_finished = true;
        QCoreApplication::exit(2);
    }
}

void EvalHarness::printLlmUsage()
{
    if (m_usage == nullptr) {
        return;
    }
    const auto usageSummaryRes = m_usage->summarize(0, m_clock.nowMs() + 1000);
    if (!usageSummaryRes.ok()) {
        return;
    }
    const auto &summaries = usageSummaryRes.value();
    int totalReqs = 0;
    qint64 totalPrompt = 0;
    qint64 totalComp = 0;
    int totalHits = 0;
    int totalFails = 0;
    for (const auto &s : summaries) {
        totalReqs += s.requests;
        totalPrompt += s.promptTokens;
        totalComp += s.completionTokens;
        totalHits += s.cacheHits;
        totalFails += s.failures;
    }
    std::cout << "\n--- LLM & Token Usage ---\n";
    std::cout << "  LLM Requests:      " << totalReqs << " (cache hits: " << totalHits
              << ", failures: " << totalFails << ")\n";
    std::cout << "  Prompt Tokens:     " << totalPrompt << "\n";
    std::cout << "  Completion Tokens: " << totalComp << "\n";
    std::cout << "  Total Tokens:      " << (totalPrompt + totalComp) << "\n";
}

qint64 EvalHarness::elapsedMs() const
{
    return m_timer.elapsed();
}

} // namespace linernotes::eval
