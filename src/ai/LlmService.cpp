// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmService.h"

#include <QTimer>

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
    if (m_retryTimer != nullptr) {
        m_retryTimer->stop();
    }
    if (!m_finished && m_currentReply != nullptr) {
        disconnect(m_currentReply.get(), nullptr, this, nullptr);
        m_currentReply->abort();
        m_currentReply.reset();
    }
    m_currentTicket.reset();
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
    if (m_retryTimer != nullptr) {
        m_retryTimer->stop();
    }
    if (m_currentReply != nullptr) {
        disconnect(m_currentReply.get(), nullptr, this, nullptr);
        m_currentReply->abort();
        m_currentReply.reset();
    }
    m_currentTicket.reset();

    core::Error err;
    err.code = QString(errc::kAborted);
    err.message = QStringLiteral("LLM task was aborted");
    err.detail = QString();
    finishWithError(std::move(err));
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
        core::Error err;
        err.code = QString(errc::kNotConfigured);
        err.message = QStringLiteral("No service configured for purpose: %1")
                          .arg(purposeName(m_call.purpose));
        err.detail = QString();
        finishWithError(std::move(err));
        return;
    }

    m_resolved = resolvedOpt.value();
    m_service.m_secrets.read(m_resolved.profile.id, this,
        [this](const core::Result<QString> &keyRes) { onSecretRead(keyRes); });
}

void LlmTask::onSecretRead(const core::Result<QString> &keyRes)
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

    acquireAndSend(reqToSend);
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
        recordCacheHit();
        LlmResult res;
        res.response = std::move(cached.value());
        res.structured = parseRes.value();
        res.fromCache = true;
        res.attempts = 0;
        res.totalUsage = TokenUsage { };
        res.model = m_serviceConfig.model;
        finishWithSuccess(std::move(res));
        return true;
    }

    recordCacheHit();
    LlmResult res;
    res.response = std::move(cached.value());
    res.structured = std::nullopt;
    res.fromCache = true;
    res.attempts = 0;
    res.totalUsage = TokenUsage { };
    res.model = m_serviceConfig.model;
    if (m_stream) {
        emit delta(res.response.content);
    }
    finishWithSuccess(std::move(res));
    return true;
}

void LlmTask::acquireAndSend(const ChatRequest &req)
{
    if (m_finished) {
        return;
    }
    m_currentTicket
        = m_service.m_scheduler.acquire(m_resolved.profile.id, m_resolved.profile.maxConcurrent,
            m_resolved.profile.requestsPerMinute, [this, req]() { sendAttempt(req); });
}

void LlmTask::sendAttempt(const ChatRequest &req)
{
    if (m_finished) {
        return;
    }
    m_attempts++;
    m_currentSentRequest = req;
    m_attemptTimer.start();

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

    // Release concurrency ticket immediately upon completion of the HTTP request
    m_currentTicket.reset();

    const qint64 attemptElapsedMs = m_attemptTimer.elapsed();
    const auto &replyRes = m_currentReply->result();
    recordAttempt(replyRes, attemptElapsedMs);

    if (!replyRes.ok()) {
        const int httpStatus = m_currentReply->httpStatus();
        const std::optional<qint64> retryAfter = m_currentReply->retryAfterMs();
        const core::Error err = replyRes.error();
        m_currentReply.reset();

        const auto delayOpt
            = retryDelayMs(m_service.m_retryPolicy, err, httpStatus, retryAfter, m_retriesDone);

        if (delayOpt.has_value()) {
            const qint64 delay = delayOpt.value();
            if (err.code == errc::kRateLimited) {
                m_service.m_scheduler.pauseService(m_resolved.profile.id, delay);
            }
            m_retriesDone++;
            if (m_retryTimer == nullptr) {
                m_retryTimer = new QTimer(this);
                m_retryTimer->setSingleShot(true);
            }
            m_retryTimer->disconnect(this);
            connect(m_retryTimer, &QTimer::timeout, this,
                [this]() { acquireAndSend(m_currentSentRequest); });
            m_retryTimer->start(static_cast<int>(delay));
            return;
        }

        finishWithError(err);
        return;
    }

    const ChatResponse resp = replyRes.value();
    m_currentReply.reset();
    m_retriesDone = 0;

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
            acquireAndSend(repairReq);
            return;
        }
        finishWithError(parseRes.error());
        return;
    }

    LlmResult res;
    res.response = resp;
    res.structured = parseRes.value();
    res.fromCache = false;
    res.attempts = m_attempts;
    res.totalUsage = m_totalUsage;
    res.model = resp.model.isEmpty() ? m_serviceConfig.model : resp.model;
    writeCacheIfEligible(res.response);
    finishWithSuccess(std::move(res));
}

void LlmTask::handleNormalReply(const ChatResponse &resp)
{
    LlmResult res;
    res.response = resp;
    res.structured = std::nullopt;
    res.fromCache = false;
    res.attempts = m_attempts;
    res.totalUsage = m_totalUsage;
    res.model = resp.model.isEmpty() ? m_serviceConfig.model : resp.model;
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

void LlmTask::recordCacheHit()
{
    UsageRecord rec;
    rec.createdAtMs = m_service.m_clock.nowMs();
    rec.purpose = m_call.purpose;
    rec.serviceId = m_resolved.profile.id;
    rec.model = m_serviceConfig.model;
    rec.usage = TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    rec.cached = true;
    rec.ok = true;
    rec.errorCode = QString();
    rec.elapsedMs = m_timer.elapsed();
    recordUsage(rec);
}

void LlmTask::recordAttempt(const core::Result<ChatResponse> &res, qint64 elapsedMs)
{
    UsageRecord rec;
    rec.createdAtMs = m_service.m_clock.nowMs();
    rec.purpose = m_call.purpose;
    rec.serviceId = m_resolved.profile.id;
    rec.cached = false;
    rec.elapsedMs = elapsedMs;

    if (!res.ok()) {
        rec.model = m_serviceConfig.model;
        rec.usage = TokenUsage { .promptTokens = 0, .completionTokens = 0 };
        rec.ok = false;
        rec.errorCode = res.error().code;
    } else {
        const ChatResponse &resp = res.value();
        rec.model = resp.model.isEmpty() ? m_serviceConfig.model : resp.model;
        rec.usage = resp.usage;
        rec.ok = true;
        rec.errorCode = QString();
    }
    recordUsage(rec);
}

void LlmTask::recordUsage(const UsageRecord &record)
{
    const auto res = m_service.m_usage.record(record);
    if (!res.ok()) {
        qCWarning(lcAi) << "LlmTask failed to record usage:" << res.error().message;
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

LlmService::LlmService(AiConfig &config, SecretStore &secrets, LlmClient &client, LlmCache &cache,
    UsageStore &usage, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_secrets(secrets)
    , m_client(client)
    , m_cache(cache)
    , m_usage(usage)
    , m_clock(clock)
    , m_defaultCacheTtlMs(kDefaultCacheTtlMs)
    , m_scheduler(clock, this)
{
}

void LlmService::setDefaultCacheTtlMs(std::optional<qint64> ttlMs)
{
    m_defaultCacheTtlMs = ttlMs;
}

void LlmService::setRetryPolicy(RetryPolicy policy)
{
    m_retryPolicy = policy;
}

const RetryPolicy &LlmService::retryPolicy() const
{
    return m_retryPolicy;
}

RequestScheduler &LlmService::scheduler()
{
    return m_scheduler;
}

std::unique_ptr<LlmTask> LlmService::start(LlmCall call)
{
    auto task = std::unique_ptr<LlmTask>(new LlmTask(*this, std::move(call)));
    task->start();
    return task;
}

} // namespace linernotes::ai
