// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>

#include <ai/AiConfig.h>
#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/JsonSchema.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmReply.h>
#include <ai/RequestScheduler.h>
#include <ai/RetryPolicy.h>
#include <ai/SecretStore.h>
#include <ai/StructuredOutput.h>
#include <ai/UsageStore.h>
#include <core/Clock.h>
#include <core/Result.h>

#include <cstdint>
#include <memory>
#include <optional>

class QTimer;

namespace linernotes::ai {

class LlmService;
class PrivacyGuard;

enum class CachePolicy : std::uint8_t {
    Use, // 命中则直接返回；未命中请求后写入
    Refresh, // 不读缓存，请求成功后覆盖写入
    Bypass, // 不读不写（如 DJ 这类要求每次不同的场景）
};

struct LlmCall {
    Purpose purpose = Purpose::Query;
    ChatRequest request;
    std::optional<StructuredSpec> structured; // 设置时走结构化流程，结果放在 LlmResult::structured
    bool stream = false; // 与 structured 同时设置时视为 false（结构化结果要完整才能校验）
    CachePolicy cachePolicy = CachePolicy::Use;
    std::optional<qint64> cacheTtlMs; // 为空用 LlmService 的默认 TTL
    QSet<DataCategory> dataCategories;
};

struct LlmResult {
    ChatResponse response; // 最后一次（成功的）响应
    std::optional<QJsonValue> structured; // 结构化调用时为校验通过的值
    bool fromCache = false;
    int attempts = 0; // 实际发出的请求数（缓存命中为 0）
    TokenUsage totalUsage; // 各次请求用量之和（缓存命中为 0）
    QString model; // 实际使用的模型
    bool operator==(const LlmResult &) const = default;
};

/// 一次调用的进行中状态，调用方持有。finished 恰好一次；finished 的槽里可以直接销毁它。
/// 销毁未完成的 LlmTask 会中止底层请求，不再发信号。
/// LlmService 生命周期长于它创建的 task。
class LlmTask : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LlmTask)

public:
    ~LlmTask() override;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] const core::Result<LlmResult> &result() const; // finished 之后
    void abort(); // 以 errc::kAborted 结束

signals:
    void delta(const QString &text); // 仅流式；缓存命中的流式调用发一次完整 content
    void finished();

private:
    friend class LlmService;

    LlmTask(LlmService &service, LlmCall call, QObject *parent = nullptr);

    void start();
    void run();
    void onSecretRead(const core::Result<QString> &keyRes);
    void handleExecution();
    bool tryReturnFromCache();
    void acquireAndSend(const ChatRequest &req);
    void sendAttempt(const ChatRequest &req);
    void onReplyFinished();
    void handleStructuredReply(const ChatResponse &resp);
    void handleNormalReply(const ChatResponse &resp);
    void writeCacheIfEligible(const ChatResponse &response);
    void recordUsage(const UsageRecord &record);
    void recordAttempt(const core::Result<ChatResponse> &res, qint64 elapsedMs);
    void recordCacheHit();
    void finishWithSuccess(LlmResult result);
    void finishWithError(core::Error error);

    LlmService &m_service;
    LlmCall m_call;

    bool m_finished = false;
    core::Result<LlmResult> m_result { core::Error { } };

    ResolvedService m_resolved;
    ServiceConfig m_serviceConfig;
    StructuredMode m_mode = StructuredMode::Prompt;
    StructuredSpec m_spec;
    JsonSchema m_compiledSchema;
    QString m_cacheKey;
    bool m_stream = false;

    int m_attempts = 0;
    int m_retriesDone = 0;
    TokenUsage m_totalUsage;
    ChatRequest m_currentSentRequest;
    std::unique_ptr<LlmReply> m_currentReply;
    std::unique_ptr<RequestScheduler::Ticket> m_currentTicket;
    QTimer *m_retryTimer = nullptr;
    QElapsedTimer m_timer;
    QElapsedTimer m_attemptTimer;
};

class LlmService : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LlmService)

public:
    friend class LlmTask;

    /// 依赖都由调用方持有，生命周期长于 LlmService。
    LlmService(AiConfig &config, SecretStore &secrets, LlmClient &client, LlmCache &cache,
        UsageStore &usage, PrivacyGuard &privacy, const core::Clock &clock,
        QObject *parent = nullptr);
    ~LlmService() override = default;

    void setDefaultCacheTtlMs(std::optional<qint64> ttlMs); // 默认 30 天
    void setRetryPolicy(RetryPolicy policy);
    const RetryPolicy &retryPolicy() const;
    RequestScheduler &scheduler();
    std::unique_ptr<LlmTask> start(LlmCall call);

private:
    AiConfig &m_config;
    SecretStore &m_secrets;
    LlmClient &m_client;
    LlmCache &m_cache;
    UsageStore &m_usage;
    PrivacyGuard &m_privacy;
    const core::Clock &m_clock;
    std::optional<qint64> m_defaultCacheTtlMs;
    RetryPolicy m_retryPolicy;
    RequestScheduler m_scheduler;
};

} // namespace linernotes::ai
