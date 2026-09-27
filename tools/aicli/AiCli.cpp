// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AiCli.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

#include <ai/AiEnumNames.h>
#include <ai/ChatTypes.h>
#include <library/Migrator.h>

#include <iostream>
#include <utility>

namespace linernotes::aicli {

AiCli::AiCli(AiCliConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

AiCli::~AiCli() = default;

bool AiCli::init()
{
    if (m_config.dbPath.isEmpty()) {
        m_tempDir = std::make_unique<QTemporaryDir>();
        if (!m_tempDir->isValid()) {
            std::cerr << "Failed to create temporary directory\n";
            return false;
        }
        m_dbPath = m_tempDir->filePath(QStringLiteral("aicli.db"));
        m_settingsPath = m_tempDir->filePath(QStringLiteral("settings.ini"));
    } else {
        m_dbPath = m_config.dbPath;
        m_tempDir = std::make_unique<QTemporaryDir>();
        if (!m_tempDir->isValid()) {
            std::cerr << "Failed to create temporary directory for settings\n";
            return false;
        }
        m_settingsPath = m_tempDir->filePath(QStringLiteral("settings.ini"));
    }

    m_settings = std::make_unique<core::Settings>(m_settingsPath);
    m_db = std::make_unique<library::Database>(m_dbPath);

    const library::Migrator migrator;
    const auto openRes = m_db->open(migrator);
    if (!openRes.ok()) {
        std::cerr << "Database error: " << qPrintable(openRes.error().toString()) << "\n";
        return false;
    }

    m_network = std::make_unique<QNetworkAccessManager>();
    m_aiConfig = std::make_unique<ai::AiConfig>(*m_settings);
    m_secrets = std::make_unique<ai::MemorySecretStore>();
    m_client = std::make_unique<ai::LlmClient>(*m_network);
    m_cache = std::make_unique<ai::LlmCache>(*m_db, m_clock);
    m_usage = std::make_unique<ai::UsageStore>(*m_db);
    m_privacy = std::make_unique<ai::PrivacyGuard>(*m_settings);
    m_debugLog = std::make_unique<ai::LlmDebugLog>(*m_settings);
    m_service = std::make_unique<ai::LlmService>(
        *m_aiConfig, *m_secrets, *m_client, *m_cache, *m_usage, *m_privacy, *m_debugLog, m_clock);

    ai::ServiceProfile profile;
    profile.name = QStringLiteral("CLI Service");
    profile.baseUrl = m_config.baseUrl;
    profile.defaultModel = m_config.model;
    profile.timeoutMs = m_config.timeoutMs;
    profile.requestsPerMinute = m_config.requestsPerMinute;
    profile.maxConcurrent = 2; // 与产品默认一致，--concurrent 超过 2 时可观察调度排队

    const QString serviceId = m_aiConfig->saveService(profile);
    m_aiConfig->setDefaultServiceId(serviceId);

    const QString apiKey = qEnvironmentVariable("LINERNOTES_LLM_KEY");
    if (!apiKey.isEmpty()) {
        m_secrets->write(serviceId, apiKey, nullptr, nullptr);
    }

    return true;
}

ai::LlmCall AiCli::createLlmCall() const
{
    ai::LlmCall call;
    call.purpose = ai::Purpose::Query;
    call.cachePolicy = m_config.cachePolicy;
    call.stream = m_config.stream;

    if (m_config.systemPrompt.has_value() && !m_config.systemPrompt->isEmpty()) {
        call.request.messages.append(ai::makeMessage(ai::Role::System, *m_config.systemPrompt));
    }
    call.request.messages.append(ai::makeMessage(ai::Role::User, m_config.prompt));

    if (m_config.structured) {
        ai::StructuredSpec spec;
        spec.name = QStringLiteral("output");
        spec.description = QStringLiteral("Output word and confidence");
        const char *schemaRaw
            = R"({"type":"object","properties":{"word":{"type":"string"},"confidence":{"type":"number"}},"required":["word","confidence"]})";
        const auto doc = QJsonDocument::fromJson(QByteArray(schemaRaw));
        spec.schema = doc.object();
        call.structured = spec;
    }

    return call;
}

void AiCli::start()
{
    if (m_config.concurrentCount > 0) {
        m_activeConcurrentTasks = m_config.concurrentCount;
        m_concurrentTasks.reserve(static_cast<std::size_t>(m_config.concurrentCount));
        for (int i = 0; i < m_config.concurrentCount; ++i) {
            auto ctx = std::make_unique<ConcurrentTaskContext>();
            ctx->index = i + 1;
            ctx->timer.start();
            ai::LlmCall call = createLlmCall();
            ctx->task = m_service->start(std::move(call));
            ConcurrentTaskContext *rawCtx = ctx.get();
            if (m_config.stream) {
                connect(rawCtx->task.get(), &ai::LlmTask::delta, this,
                    [](const QString &text) { std::cout << text.toStdString() << std::flush; });
            }
            connect(rawCtx->task.get(), &ai::LlmTask::finished, this,
                [this, rawCtx]() { onConcurrentTaskFinished(rawCtx); });
            m_concurrentTasks.push_back(std::move(ctx));
        }
    } else {
        m_currentRepeatIndex = 0;
        runNextSequential();
    }
}

void AiCli::runNextSequential()
{
    if (m_currentRepeatIndex >= m_config.repeatCount) {
        finishAll();
        return;
    }

    ai::LlmCall call = createLlmCall();
    m_sequentialTimer.start();
    m_sequentialTask = m_service->start(std::move(call));

    if (m_config.stream) {
        connect(m_sequentialTask.get(), &ai::LlmTask::delta, this,
            [](const QString &text) { std::cout << text.toStdString() << std::flush; });
    }
    connect(m_sequentialTask.get(), &ai::LlmTask::finished, this,
        [this]() { onSequentialTaskFinished(); });
}

void AiCli::onSequentialTaskFinished()
{
    if (m_sequentialTask == nullptr) {
        return;
    }

    const qint64 elapsedMs = m_sequentialTimer.elapsed();
    const int callIndex = m_currentRepeatIndex + 1;

    printTaskResult(callIndex, elapsedMs, m_sequentialTask->result());

    if (m_sequentialTask->result().ok()) {
        m_successCount++;
    } else {
        m_failureCount++;
    }

    m_sequentialTask.reset();
    m_currentRepeatIndex++;
    runNextSequential();
}

void AiCli::onConcurrentTaskFinished(ConcurrentTaskContext *ctx)
{
    if (ctx == nullptr || ctx->task == nullptr) {
        return;
    }

    const qint64 elapsedMs = ctx->timer.elapsed();
    const int callIndex = ctx->index;

    printTaskResult(callIndex, elapsedMs, ctx->task->result());

    if (ctx->task->result().ok()) {
        m_successCount++;
    } else {
        m_failureCount++;
    }

    m_activeConcurrentTasks--;
    if (m_activeConcurrentTasks <= 0) {
        finishAll();
    }
}

void AiCli::printTaskResult(int index, qint64 elapsedMs, const core::Result<ai::LlmResult> &res)
{
    if (m_config.stream) {
        std::cout << "\n";
    }

    if (res.ok()) {
        const auto &val = res.value();
        const QString model = !val.model.isEmpty() ? val.model : m_config.model;
        const QString summaryLine
            = QStringLiteral(
                "#%1 ok fromCache=%2 attempts=%3 model=%4 prompt=%5 completion=%6 elapsed=%7ms")
                  .arg(QString::number(index), QString::number(val.fromCache ? 1 : 0),
                      QString::number(val.attempts), model,
                      QString::number(val.totalUsage.promptTokens),
                      QString::number(val.totalUsage.completionTokens), QString::number(elapsedMs));

        std::cout << summaryLine.toStdString() << "\n";

        if (val.structured.has_value()) {
            QByteArray jsonBytes;
            if (val.structured->isObject()) {
                jsonBytes
                    = QJsonDocument(val.structured->toObject()).toJson(QJsonDocument::Compact);
            } else if (val.structured->isArray()) {
                jsonBytes = QJsonDocument(val.structured->toArray()).toJson(QJsonDocument::Compact);
            } else {
                jsonBytes = val.structured->toVariant().toString().toUtf8();
            }
            std::cout << "  " << jsonBytes.constData() << "\n";
        } else if (!m_config.stream) {
            QString content = val.response.content;
            if (content.size() > 500) {
                content = content.first(500) + QStringLiteral("... (truncated)");
            }
            const QStringList lines = content.split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                std::cout << "  " << line.toStdString() << "\n";
            }
        }
    } else {
        const auto &err = res.error();
        const QString summaryLine = QStringLiteral(
            "#%1 ERR %2 fromCache=0 attempts=0 model=%3 prompt=0 completion=0 elapsed=%4ms")
                                        .arg(QString::number(index), err.code, m_config.model,
                                            QString::number(elapsedMs));

        std::cout << summaryLine.toStdString() << "\n";
        std::cout << "  Error: " << err.message.toStdString() << "\n";
        if (!err.detail.isEmpty()) {
            std::cout << "  Detail: " << err.detail.toStdString() << "\n";
        }
    }
}

void AiCli::printUsageSummary()
{
    if (m_usage == nullptr) {
        return;
    }
    const qint64 nowMs = m_clock.nowMs();
    constexpr qint64 kOneDayMs = 24LL * 60 * 60 * 1000;
    const auto summaryRes = m_usage->summarize(nowMs - kOneDayMs, nowMs + 1000);
    if (!summaryRes.ok()) {
        std::cerr << "Failed to query usage summary: " << qPrintable(summaryRes.error().toString())
                  << "\n";
        return;
    }

    std::cout << "\n=== Usage Summary (last 24h) ===\n";
    const auto &summaries = summaryRes.value();
    if (summaries.isEmpty()) {
        std::cout << "  (no usage recorded)\n";
        return;
    }

    for (const auto &s : summaries) {
        std::cout << "  purpose=" << purposeName(s.purpose).toStdString()
                  << " model=" << s.model.toStdString() << " requests=" << s.requests
                  << " cache_hits=" << s.cacheHits << " failures=" << s.failures
                  << " prompt_tokens=" << s.promptTokens
                  << " completion_tokens=" << s.completionTokens << "\n";
    }
}

void AiCli::finishAll()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    printUsageSummary();
    const int exitCode = (m_failureCount > 0) ? 1 : 0;
    QCoreApplication::exit(exitCode);
}

} // namespace linernotes::aicli
