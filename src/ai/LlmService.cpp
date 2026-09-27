// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmService.h"

#include <ai/AiEnumNames.h>
#include <ai/AiLogging.h>
#include <ai/Errors.h>

#include <utility>

namespace linernotes::ai {

namespace {

constexpr qint64 kDefaultCacheTtlMs = 30LL * 24 * 60 * 60 * 1000;

} // namespace

LlmTask::LlmTask(LlmService &service, LlmCall call, QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_call(std::move(call))
{
}

LlmTask::~LlmTask()
{
    if (!m_finished && m_currentReply != nullptr) {
        disconnect(m_currentReply.get(), nullptr, this, nullptr);
        m_currentReply->abort();
        m_currentReply.reset();
    }
}

bool LlmTask::isFinished() const
{
    return m_finished;
}

const core::Result<LlmResult> &LlmTask::result() const
{
    Q_ASSERT_X(m_finished, "LlmTask::result", "Called result() before task finished");
    return m_result;
}

void LlmTask::abort()
{
    if (m_finished) {
        return;
    }
    if (m_currentReply != nullptr) {
        disconnect(m_currentReply.get(), nullptr, this, nullptr);
        m_currentReply->abort();
        m_currentReply.reset();
    }
    finishWithError(core::Error {
        .code = QString(errc::kAborted),
        .message = QStringLiteral("LLM task was aborted"),
        .detail = QString(),
    });
}

void LlmTask::start()
{
    m_timer.start();
    QMetaObject::invokeMethod(this, &LlmTask::run, Qt::QueuedConnection);
}

void LlmTask::run()
{
    if (m_finished) {
        return;
    }

    const auto resolvedOpt = m_service.m_config.resolve(m_call.purpose);
    if (!resolvedOpt.has_value()) {
        finishWithError(core::Error {
            .code = QString(errc::kNotConfigured),
            .message = QStringLiteral("No service configured for purpose: %1")
                .arg(purposeName(m_call.purpose)),
            .detail = QString(),
        });
        return;
    }

    m_resolved = resolvedOpt.value();
    m_service.m_secrets.read(m_resolved.profile.id, this,
        [this](core::Result<QString> keyRes) { onSecretRead(std::move(keyRes)); });
}

void LlmTask::onSecretRead(core::Result<QString> keyRes)
{
    if (m_finished) {
        return;
    }

    if (!keyRes.ok()) {
        finishWithError(keyRes.error());
        return;
    }

    m_serviceConfig = ServiceConfig {
        .baseUrl = m_resolved.profile.baseUrl,
        .apiKey = keyRes.value(),
        .model = m_resolved.model,
        .timeoutMs = m_resolved.profile.timeoutMs,
    };

    handleExecution();
}

void LlmTask::handleExecution()
{
    if (m_call.structured.has_value()) {
        m_stream = false;
        m_spec = m_call.structured.value();
        const auto schemaRes = JsonSchema::compile(m_spec.schema);
        if (!schemaRes.ok()) {
            finishWithError(schemaRes.error());
            return;
        }
        m_compiledSchema = schemaRes.value();
        m_mode = chooseStructuredMode(m_resolved.profile.capabilities.value_or(Capabilities { }));
    } else {
        m_stream = m_call.stream;
        m_mode = StructuredMode::Prompt;
    }

    m_cacheKey = LlmCache::makeKey(
        m_serviceConfig.baseUrl, m_serviceConfig.model, m_call.request, m_call.structured, m_mode);

    if (m_call.cachePolicy == CachePolicy::Use && tryReturnFromCache()) {
        return;
    }

    ChatRequest reqToSend = m_call.request;
    if (m_call.structured.has_value()) {
        reqToSend = buildStructuredRequest(m_call.request, m_spec, m_mode);
    }

    sendAttempt(reqToSend);
}

bool LlmTask::tryReturnFromCache()
{
    auto cached = m_service.m_cache.get(m_cacheKey);
    if (!cached.has_value()) {
        return false;
    }

    if (m_call.structured.has_value()) {
        auto parseRes = parseStructuredResponse(cached.value(), m_spec, m_compiledSchema, m_mode);
        if (!parseRes.ok()) {
            return false;
        }
        LlmResult res {
            .response = std::move(cached.value()),
            .structured = parseRes.value(),
            .fromCache = true,
            .attempts = 0,
            .totalUsage = TokenUsage { },
            .model = m_serviceConfig.model,
        };
        finishWithSuccess(std::move(res));
        return true;
    }

    LlmResult res {
        .response = std::move(cached.value()),
        .structured = std::nullopt,
        .fromCache = true,
        .attempts = 0,
        .totalUsage = TokenUsage { },
        .model = m_serviceConfig.model,
    };
    if (m_stream) {
        emit delta(res.response.content);
    }
    finishWithSuccess(std::move(res));
    return true;
}

void LlmTask::sendAttempt(const ChatRequest &req)
{
    m_attempts++;
    m_currentSentRequest = req;

    if (m_stream) {
        m_currentReply = m_service.m_client.stream(m_serviceConfig, req);
        connect(m_currentReply.get(), &LlmReply::delta, this, &LlmTask::delta);
    } else {
        m_currentReply = m_service.m_client.complete(m_serviceConfig, req);
    }

    connect(m_currentReply.get(), &LlmReply::finished, this, &LlmTask::onReplyFinished);
}

void LlmTask::onReplyFinished()
{
    if (m_finished || m_currentReply == nullptr) {
        return;
    }

    const auto &replyRes = m_currentReply->result();
    if (!replyRes.ok()) {
        finishWithError(replyRes.error());
        return;
    }

    const ChatResponse resp = replyRes.value();
    m_totalUsage.promptTokens += resp.usage.promptTokens;
    m_totalUsage.completionTokens += resp.usage.completionTokens;

    if (m_call.structured.has_value()) {
        handleStructuredReply(resp);
    } else {
        handleNormalReply(resp);
    }
}

void LlmTask::handleStructuredReply(const ChatResponse &resp)
{
    auto parseRes = parseStructuredResponse(resp, m_spec, m_compiledSchema, m_mode);
    if (!parseRes.ok()) {
        if (m_attempts == 1) {
            const ChatRequest repairReq
                = buildRepairRequest(m_currentSentRequest, resp, parseRes.error(), m_mode);
            m_currentReply.reset();
            sendAttempt(repairReq);
            return;
        }
        finishWithError(parseRes.error());
        return;
    }

    LlmResult res {
        .response = resp,
        .structured = parseRes.value(),
        .fromCache = false,
        .attempts = m_attempts,
        .totalUsage = m_totalUsage,
        .model = resp.model.isEmpty() ? m_serviceConfig.model : resp.model,
    };
    writeCacheIfEligible(res.response);
    finishWithSuccess(std::move(res));
}

void LlmTask::handleNormalReply(const ChatResponse &resp)
{
    LlmResult res {
        .response = resp,
        .structured = std::nullopt,
        .fromCache = false,
        .attempts = m_attempts,
        .totalUsage = m_totalUsage,
        .model = resp.model.isEmpty() ? m_serviceConfig.model : resp.model,
    };
    writeCacheIfEligible(res.response);
    finishWithSuccess(std::move(res));
}

void LlmTask::writeCacheIfEligible(const ChatResponse &response)
{
    if (m_call.cachePolicy == CachePolicy::Bypass) {
        return;
    }
    const auto ttlMs
        = m_call.cacheTtlMs.has_value() ? m_call.cacheTtlMs : m_service.m_defaultCacheTtlMs;
    if (ttlMs.has_value() && *ttlMs == 0) {
        return;
    }
    const auto putRes
        = m_service.m_cache.put(m_cacheKey, m_serviceConfig.model, m_call.purpose, response, ttlMs);
    if (!putRes.ok()) {
        qCWarning(lcAi) << "LlmTask failed to write cache:" << putRes.error().message;
    }
}

void LlmTask::finishWithSuccess(LlmResult result)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_result = std::move(result);

    const qint64 elapsedMs = m_timer.elapsed();
    const auto &res = m_result.value();
    qCInfo(lcAi) << "LLM task finished: purpose=" << purposeName(m_call.purpose)
                 << "model=" << res.model << "fromCache=" << res.fromCache
                 << "attempts=" << res.attempts << "elapsedMs=" << elapsedMs;

    emit finished();
}

void LlmTask::finishWithError(core::Error error)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_result = std::move(error);

    const qint64 elapsedMs = m_timer.elapsed();
    qCWarning(lcAi) << "LLM task failed: purpose=" << purposeName(m_call.purpose)
                    << "model=" << m_serviceConfig.model << "error=" << m_result.error().code
                    << "message=" << m_result.error().message << "elapsedMs=" << elapsedMs;

    emit finished();
}

LlmService::LlmService(
    AiConfig &config, SecretStore &secrets, LlmClient &client, LlmCache &cache, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_secrets(secrets)
    , m_client(client)
    , m_cache(cache)
    , m_defaultCacheTtlMs(kDefaultCacheTtlMs)
{
}

void LlmService::setDefaultCacheTtlMs(std::optional<qint64> ttlMs)
{
    m_defaultCacheTtlMs = ttlMs;
}

std::unique_ptr<LlmTask> LlmService::start(LlmCall call)
{
    auto task = std::unique_ptr<LlmTask>(new LlmTask(*this, std::move(call)));
    task->start();
    return task;
}

} // namespace linernotes::ai
