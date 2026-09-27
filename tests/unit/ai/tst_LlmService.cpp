// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <ai/AiConfig.h>
#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/RetryPolicy.h>
#include <ai/SecretStore.h>
#include <ai/StructuredOutput.h>
#include <ai/UsageStore.h>
#include <common/FakeLlmServer.h>
#include <common/ManualClock.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>

using linernotes::ai::AiConfig;
using linernotes::ai::CachePolicy;
using linernotes::ai::Capabilities;
using linernotes::ai::ChatResponse;
using linernotes::ai::DataCategory;
using linernotes::ai::LlmCache;
using linernotes::ai::LlmCall;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmDebugLog;
using linernotes::ai::LlmService;
using linernotes::ai::LlmTask;
using linernotes::ai::makeMessage;
using linernotes::ai::MemorySecretStore;
using linernotes::ai::PrivacyGuard;
using linernotes::ai::Purpose;
using linernotes::ai::RetryPolicy;
using linernotes::ai::Role;
using linernotes::ai::ServiceProfile;
using linernotes::ai::StructuredMode;
using linernotes::ai::StructuredSpec;
using linernotes::ai::UsageStore;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::FakeLlmServer;
using linernotes::test::ManualClock;

namespace {

FakeLlmServer::Response makeJsonResponse(const QString &content,
    const QString &model = QStringLiteral("gpt-4o"), int promptTokens = 10,
    int completionTokens = 5)
{
    QJsonObject respJson;
    respJson.insert(QStringLiteral("model"), model);

    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msgObj.insert(QStringLiteral("content"), content);

    QJsonObject choice0;
    choice0.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));
    choice0.insert(QStringLiteral("message"), msgObj);

    respJson.insert(QStringLiteral("choices"), QJsonArray { choice0 });

    QJsonObject usageObj;
    usageObj.insert(QStringLiteral("prompt_tokens"), promptTokens);
    usageObj.insert(QStringLiteral("completion_tokens"), completionTokens);
    respJson.insert(QStringLiteral("usage"), usageObj);

    return FakeLlmServer::json(respJson, 200);
}

struct Fixture {
    FakeLlmServer server;
    QTemporaryDir tempDir;
    Settings settings;
    AiConfig config;
    MemorySecretStore secrets;
    QNetworkAccessManager nam;
    LlmClient client;
    Database db;
    Migrator migrator;
    ManualClock clock;
    LlmCache cache;
    UsageStore usage;
    PrivacyGuard privacy;
    LlmDebugLog debugLog;
    LlmService service;

    explicit Fixture(
        bool setupService = true, const std::optional<Capabilities> &capabilities = std::nullopt)
        : settings(tempDir.filePath(QStringLiteral("settings.ini")))
        , config(settings)
        , client(nam)
        , db(tempDir.filePath(QStringLiteral("test.db")))
        , clock(1000)
        , cache(db, clock)
        , usage(db)
        , privacy(settings)
        , debugLog(settings)
        , service(config, secrets, client, cache, usage, privacy, debugLog, clock)
    {
        const auto openRes = db.open(migrator);
        Q_ASSERT(openRes.ok());

        if (setupService) {
            ServiceProfile profile;
            profile.id = QStringLiteral("svc1");
            profile.name = QStringLiteral("Test Service");
            profile.baseUrl = server.baseUrl();
            profile.defaultModel = QStringLiteral("gpt-4o");
            profile.timeoutMs = 5000;
            profile.capabilities = capabilities;
            profile.maxConcurrent = 10;
            profile.requestsPerMinute = 0;

            config.saveService(profile);
            secrets.write(
                QStringLiteral("svc1"), QStringLiteral("secret-token-123"), nullptr, nullptr);
        }
    }
};

class TstLlmService : public QObject {
    Q_OBJECT

private slots:
    void notConfiguredFinishesAsynchronously();
    void normalCallWithKeyAndModelAndSubsequentCacheHit();
    void cachePolicyBypassAndRefresh();
    void cacheExpirationTriggersNewRequest();
    void structuredOutputRepairRetryAndDoubleFailure();
    void streamingDeltaForwardingAndCacheHitFullContent();
    void destroyInFlightTaskDoesNotCrashOrEmitSignals();
    void usageRecordedOnStructuredRetryAndCacheHit();
    void retry500To500To200();
    void retry429WithRetryAfter0();
    void noRetryOn401();
    void concurrencyLimitBlocksSecondCall();
    void privacyBlockedSendsNoRequest();
    void debugLogRecordsAttemptsAndReplayBypassesCache();
};

void TstLlmService::notConfiguredFinishesAsynchronously()
{
    Fixture fix(false);

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("hi")));

    auto task = fix.service.start(call);
    QVERIFY(!task->isFinished());

    QSignalSpy spy(task.get(), &LlmTask::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(task->isFinished());
    QVERIFY(!task->result().ok());
    QCOMPARE(task->result().error().code, linernotes::ai::errc::kNotConfigured);
}

void TstLlmService::normalCallWithKeyAndModelAndSubsequentCacheHit()
{
    Fixture fix;

    fix.server.enqueue(
        makeJsonResponse(QStringLiteral("Hello world"), QStringLiteral("gpt-4o"), 12, 6));

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("What is music?")));

    // First call: misses cache, sends network request
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->isFinished());
        QVERIFY(task->result().ok());

        const auto &res = task->result().value();
        QCOMPARE(res.response.content, QStringLiteral("Hello world"));
        QCOMPARE(res.model, QStringLiteral("gpt-4o"));
        QCOMPARE(res.fromCache, false);
        QCOMPARE(res.attempts, 1);
        QCOMPARE(res.totalUsage.promptTokens, 12);
        QCOMPARE(res.totalUsage.completionTokens, 6);

        const auto reqs = fix.server.requests();
        QCOMPARE(reqs.size(), 1);
        QCOMPARE(reqs.at(0).headers.value("authorization"), QByteArray("Bearer secret-token-123"));

        const QJsonDocument sentDoc = QJsonDocument::fromJson(reqs.at(0).body);
        QCOMPARE(
            sentDoc.object().value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o"));
    }

    // Second call: same request, hits cache! FakeLlmServer should NOT receive any new requests.
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->isFinished());
        QVERIFY(task->result().ok());

        const auto &res = task->result().value();
        QCOMPARE(res.response.content, QStringLiteral("Hello world"));
        QCOMPARE(res.model, QStringLiteral("gpt-4o"));
        QCOMPARE(res.fromCache, true);
        QCOMPARE(res.attempts, 0);
        QCOMPARE(res.totalUsage.promptTokens, 0);
        QCOMPARE(res.totalUsage.completionTokens, 0);

        const auto reqs = fix.server.requests();
        QCOMPARE(reqs.size(), 1);
    }
}

void TstLlmService::cachePolicyBypassAndRefresh()
{
    Fixture fix;

    // Bypass test: doesn't read, doesn't write
    {
        fix.server.enqueue(makeJsonResponse(QStringLiteral("Bypass result")));
        LlmCall call;
        call.purpose = Purpose::Dj;
        call.cachePolicy = CachePolicy::Bypass;
        call.request.messages.append(makeMessage(Role::User, QStringLiteral("play track")));

        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, false);
        QCOMPARE(task->result().value().response.content, QStringLiteral("Bypass result"));

        const QString cacheKey = LlmCache::makeKey(fix.server.baseUrl(), QStringLiteral("gpt-4o"),
            call.request, std::nullopt, StructuredMode::Prompt);
        QVERIFY(!fix.cache.get(cacheKey).has_value());
    }

    // Refresh test: doesn't read existing cache, but writes new response
    {
        ChatResponse oldResp;
        oldResp.content = QStringLiteral("Old cached");
        oldResp.model = QStringLiteral("gpt-4o");

        LlmCall call;
        call.purpose = Purpose::Query;
        call.cachePolicy = CachePolicy::Refresh;
        call.request.messages.append(makeMessage(Role::User, QStringLiteral("refresh query")));

        const QString cacheKey = LlmCache::makeKey(fix.server.baseUrl(), QStringLiteral("gpt-4o"),
            call.request, std::nullopt, StructuredMode::Prompt);
        QVERIFY(
            fix.cache.put(cacheKey, QStringLiteral("gpt-4o"), Purpose::Query, oldResp, std::nullopt)
                .ok());

        fix.server.enqueue(makeJsonResponse(QStringLiteral("Fresh from server")));

        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, false);
        QCOMPARE(task->result().value().response.content, QStringLiteral("Fresh from server"));

        const auto updated = fix.cache.get(cacheKey);
        QVERIFY(updated.has_value());
        if (!updated.has_value()) {
            return;
        }
        QCOMPARE(updated->content, QStringLiteral("Fresh from server"));
    }
}

void TstLlmService::cacheExpirationTriggersNewRequest()
{
    Fixture fix;

    LlmCall call;
    call.purpose = Purpose::Query;
    call.cacheTtlMs = 1000;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("expiring query")));

    fix.server.enqueue(makeJsonResponse(QStringLiteral("Resp 1")));

    // 1st call at t=1000: sends request
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, false);
        QCOMPARE(fix.server.requests().size(), 1);
    }

    // Advance clock to t=1500 (still valid)
    fix.clock.advance(500);

    // 2nd call: hits cache
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, true);
        QCOMPARE(fix.server.requests().size(), 1);
    }

    // Advance clock to t=2500 (expired!)
    fix.clock.advance(1000);
    fix.server.enqueue(makeJsonResponse(QStringLiteral("Resp 2")));

    // 3rd call: misses cache, sends request
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, false);
        QCOMPARE(task->result().value().response.content, QStringLiteral("Resp 2"));
        QCOMPARE(fix.server.requests().size(), 2);
    }
}

void TstLlmService::structuredOutputRepairRetryAndDoubleFailure()
{
    Fixture fix;

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    QJsonObject props;
    props.insert(QStringLiteral("ok"),
        QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } });
    schema.insert(QStringLiteral("properties"), props);
    schema.insert(QStringLiteral("required"), QJsonArray { QStringLiteral("ok") });
    schema.insert(QStringLiteral("additionalProperties"), false);

    StructuredSpec spec {
        .name = QStringLiteral("result_spec"),
        .description = QStringLiteral("Test result spec"),
        .schema = schema,
    };

    // Part A: Retry succeeds
    {
        fix.server.enqueue(makeJsonResponse(
            QStringLiteral("{\"ok\": \"not_boolean\"}"), QStringLiteral("gpt-4o"), 10, 5));
        fix.server.enqueue(
            makeJsonResponse(QStringLiteral("{\"ok\": true}"), QStringLiteral("gpt-4o"), 20, 5));

        LlmCall call;
        call.purpose = Purpose::Query;
        call.structured = spec;
        call.request.messages.append(makeMessage(Role::User, QStringLiteral("Please check")));

        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());

        const auto &res = task->result().value();
        QCOMPARE(res.attempts, 2);
        QCOMPARE(res.fromCache, false);
        QCOMPARE(res.totalUsage.promptTokens, 30);
        QCOMPARE(res.totalUsage.completionTokens, 10);
        QVERIFY(res.structured.has_value());
        if (!res.structured.has_value()) {
            return;
        }
        QVERIFY(res.structured->isObject());
        QCOMPARE(res.structured->toObject().value(QStringLiteral("ok")).toBool(), true);

        const auto reqs = fix.server.requests();
        QCOMPARE(reqs.size(), 2);

        const QJsonDocument req2Doc = QJsonDocument::fromJson(reqs.at(1).body);
        const QJsonArray msgs2 = req2Doc.object().value(QStringLiteral("messages")).toArray();
        QVERIFY(msgs2.size() >= 3);
        const QJsonObject lastMsg = msgs2.at(msgs2.size() - 1).toObject();
        QCOMPARE(lastMsg.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        QVERIFY(lastMsg.value(QStringLiteral("content"))
                .toString()
                .contains(QStringLiteral("The previous output is invalid")));
    }

    // Part B: Double failure returns kSchemaMismatch
    {
        fix.server.enqueue(makeJsonResponse(QStringLiteral("{\"ok\": 123}")));
        fix.server.enqueue(makeJsonResponse(QStringLiteral("{\"ok\": 456}")));

        LlmCall call;
        call.purpose = Purpose::Query;
        call.cachePolicy = CachePolicy::Bypass;
        call.structured = spec;
        call.request.messages.append(makeMessage(Role::User, QStringLiteral("Check again")));

        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(!task->result().ok());
        QCOMPARE(task->result().error().code, linernotes::ai::errc::kSchemaMismatch);
    }
}

void TstLlmService::streamingDeltaForwardingAndCacheHitFullContent()
{
    Fixture fix;

    const QList<QByteArray> payloads
        = { QByteArray(R"({"choices":[{"delta":{"content":"Hello "}}],"model":"gpt-4o"})"),
              QByteArray(R"({"choices":[{"delta":{"content":"streaming world!"}}]})"),
              QByteArray(R"({"choices":[],"usage":{"prompt_tokens":5,"completion_tokens":3}})") };
    fix.server.enqueue(FakeLlmServer::sse(payloads));

    LlmCall call;
    call.purpose = Purpose::Guide;
    call.stream = true;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("stream test")));

    // 1st call (streaming from network)
    {
        auto task = fix.service.start(call);
        QSignalSpy deltaSpy(task.get(), &LlmTask::delta);
        QSignalSpy finishSpy(task.get(), &LlmTask::finished);

        QVERIFY(finishSpy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, false);
        QCOMPARE(task->result().value().response.content, QStringLiteral("Hello streaming world!"));
        QCOMPARE(deltaSpy.count(), 2);
        QCOMPARE(deltaSpy.at(0).at(0).toString(), QStringLiteral("Hello "));
        QCOMPARE(deltaSpy.at(1).at(0).toString(), QStringLiteral("streaming world!"));
    }

    // 2nd call (cached stream: emits full content in single delta)
    {
        auto task = fix.service.start(call);
        QSignalSpy deltaSpy(task.get(), &LlmTask::delta);
        QSignalSpy finishSpy(task.get(), &LlmTask::finished);

        QVERIFY(finishSpy.wait(2000));
        QVERIFY(task->result().ok());
        QCOMPARE(task->result().value().fromCache, true);
        QCOMPARE(task->result().value().response.content, QStringLiteral("Hello streaming world!"));
        QCOMPARE(deltaSpy.count(), 1);
        QCOMPARE(deltaSpy.at(0).at(0).toString(), QStringLiteral("Hello streaming world!"));
    }
}

void TstLlmService::destroyInFlightTaskDoesNotCrashOrEmitSignals()
{
    Fixture fix;

    FakeLlmServer::Response hangResp;
    hangResp.hang = true;
    fix.server.enqueue(hangResp);

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("hang test")));

    auto task = fix.service.start(call);
    QSignalSpy finishSpy(task.get(), &LlmTask::finished);

    // Wait for the request to reach the server
    QTRY_COMPARE_WITH_TIMEOUT(fix.server.requests().size(), 1, 1000);

    // Destroy task while request is in flight
    task.reset();

    // Verify finished was never emitted
    QCOMPARE(finishSpy.count(), 0);
}

void TstLlmService::usageRecordedOnStructuredRetryAndCacheHit()
{
    Fixture fix;

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    QJsonObject props;
    props.insert(QStringLiteral("ok"),
        QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } });
    schema.insert(QStringLiteral("properties"), props);
    schema.insert(QStringLiteral("required"), QJsonArray { QStringLiteral("ok") });
    schema.insert(QStringLiteral("additionalProperties"), false);

    const StructuredSpec spec {
        .name = QStringLiteral("result_spec"),
        .description = QStringLiteral("Test result spec"),
        .schema = schema,
    };

    // First attempt fails validation (ok is string instead of boolean), promptTokens=10,
    // completionTokens=5
    fix.server.enqueue(makeJsonResponse(
        QStringLiteral("{\"ok\": \"not_boolean\"}"), QStringLiteral("gpt-4o"), 10, 5));
    // Second attempt succeeds, promptTokens=20, completionTokens=5
    fix.server.enqueue(
        makeJsonResponse(QStringLiteral("{\"ok\": true}"), QStringLiteral("gpt-4o"), 20, 5));

    LlmCall call;
    call.purpose = Purpose::Query;
    call.structured = spec;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("Please check")));

    // 1st structured call: attempt 1 fails schema validation -> repair retry succeeds
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());

        const auto &res = task->result().value();
        QCOMPARE(res.attempts, 2);
        QCOMPARE(res.fromCache, false);
        QCOMPARE(res.totalUsage.promptTokens, 30);
        QCOMPARE(res.totalUsage.completionTokens, 10);
    }

    // 2nd call: same request, hits cache
    {
        auto task = fix.service.start(call);
        QSignalSpy spy(task.get(), &LlmTask::finished);
        QVERIFY(spy.wait(2000));
        QVERIFY(task->result().ok());

        const auto &res = task->result().value();
        QCOMPARE(res.attempts, 0);
        QCOMPARE(res.fromCache, true);
    }

    // Direct DB count check: 3 rows in llm_usage
    {
        const auto connRes = fix.db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM llm_usage;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 3);
    }

    // summarize result check: requests=2, cacheHits=1, tokens sum of two requests
    {
        const auto summaryRes = fix.usage.summarize(0, 100000);
        QVERIFY(summaryRes.ok());
        const auto &summaries = summaryRes.value();
        QCOMPARE(summaries.size(), 1);

        const auto &summary = summaries.at(0);
        QCOMPARE(summary.purpose, Purpose::Query);
        QCOMPARE(summary.model, QStringLiteral("gpt-4o"));
        QCOMPARE(summary.requests, 2);
        QCOMPARE(summary.cacheHits, 1);
        QCOMPARE(summary.failures, 0);
        QCOMPARE(summary.promptTokens, 30);
        QCOMPARE(summary.completionTokens, 10);
    }
}

void TstLlmService::retry500To500To200()
{
    Fixture fix;
    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1;
    policy.maxDelayMs = 100;
    fix.service.setRetryPolicy(policy);

    // Server returns 500, 500, then 200
    FakeLlmServer::Response r1;
    r1.status = 500;
    FakeLlmServer::Response r2;
    r2.status = 500;
    fix.server.enqueue(r1);
    fix.server.enqueue(r2);
    fix.server.enqueue(makeJsonResponse(QStringLiteral("finally ok")));

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("retry test")));

    auto task = fix.service.start(call);
    QSignalSpy spy(task.get(), &LlmTask::finished);
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(task->isFinished());
    const auto &res = task->result();
    QVERIFY(res.ok());
    QCOMPARE(res.value().attempts, 3);
    QCOMPARE(res.value().response.content, QStringLiteral("finally ok"));
    QCOMPARE(fix.server.requests().size(), 3);
}

void TstLlmService::retry429WithRetryAfter0()
{
    Fixture fix;
    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1;
    policy.maxDelayMs = 100;
    fix.service.setRetryPolicy(policy);

    // Server returns 429 with Retry-After: 0, then 200
    FakeLlmServer::Response resp429;
    resp429.status = 429;
    resp429.headers.append(qMakePair(QByteArray("Retry-After"), QByteArray("0")));
    fix.server.enqueue(resp429);
    fix.server.enqueue(makeJsonResponse(QStringLiteral("after 429")));

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("rate limit test")));

    auto task = fix.service.start(call);
    QSignalSpy spy(task.get(), &LlmTask::finished);

    // Wait for the first request to reach the server
    QTRY_COMPARE(fix.server.requests().size(), 1);

    // Allow time for 429 response to arrive and pause the service
    QTest::qWait(50);

    // Assert: Before advancing the clock, 2nd request has not been sent (service is paused)
    QCOMPARE(fix.server.requests().size(), 1);
    QCOMPARE(spy.count(), 0);

    // Advance clock past the pause duration
    fix.clock.advance(1000);

    // After advancing clock, the second request should proceed and finish
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(task->isFinished());
    const auto &res = task->result();
    QVERIFY(res.ok());
    QCOMPARE(res.value().attempts, 2);
    QCOMPARE(res.value().response.content, QStringLiteral("after 429"));
    QCOMPARE(fix.server.requests().size(), 2);
}

void TstLlmService::noRetryOn401()
{
    Fixture fix;
    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1;
    policy.maxDelayMs = 100;
    fix.service.setRetryPolicy(policy);

    FakeLlmServer::Response r401;
    r401.status = 401;
    fix.server.enqueue(r401);

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("auth fail test")));

    auto task = fix.service.start(call);
    QSignalSpy spy(task.get(), &LlmTask::finished);
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(task->isFinished());
    const auto &res = task->result();
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, QString(linernotes::ai::errc::kAuth));
    QCOMPARE(fix.server.requests().size(), 1);
}

void TstLlmService::concurrencyLimitBlocksSecondCall()
{
    Fixture fix(false);

    ServiceProfile profile;
    profile.id = QStringLiteral("svc1");
    profile.name = QStringLiteral("Single Concurrency Service");
    profile.baseUrl = fix.server.baseUrl();
    profile.defaultModel = QStringLiteral("gpt-4o");
    profile.timeoutMs = 5000;
    profile.maxConcurrent = 1;
    profile.requestsPerMinute = 0;

    fix.config.saveService(profile);
    fix.secrets.write(QStringLiteral("svc1"), QStringLiteral("secret-token-123"), nullptr, nullptr);

    // Call 1 will hang
    FakeLlmServer::Response hangResp;
    hangResp.hang = true;
    fix.server.enqueue(hangResp);

    // Call 2 will return JSON
    fix.server.enqueue(makeJsonResponse(QStringLiteral("call 2 ok")));

    LlmCall call1;
    call1.purpose = Purpose::Query;
    call1.cachePolicy = CachePolicy::Bypass;
    call1.request.messages.append(makeMessage(Role::User, QStringLiteral("call 1")));

    LlmCall call2;
    call2.purpose = Purpose::Query;
    call2.cachePolicy = CachePolicy::Bypass;
    call2.request.messages.append(makeMessage(Role::User, QStringLiteral("call 2")));

    auto task1 = fix.service.start(call1);
    auto task2 = fix.service.start(call2);

    // Wait until server receives call 1
    QTRY_COMPARE(fix.server.requests().size(), 1);

    // Confirm that call 2 is not sent while call 1 is in-flight
    QTest::qWait(50);
    QCOMPARE(fix.server.requests().size(), 1);
    QVERIFY(!task2->isFinished());

    // Abort call 1; call 2 should immediately be sent to server
    task1->abort();
    QTRY_COMPARE(fix.server.requests().size(), 2);
    QTRY_VERIFY(task2->isFinished());
    QVERIFY(task2->result().ok());
    QCOMPARE(task2->result().value().response.content, QStringLiteral("call 2 ok"));
}

void TstLlmService::privacyBlockedSendsNoRequest()
{
    Fixture fix(false);

    // 1. Configure cloud service with unreachable/invalid cloud URL
    ServiceProfile cloudProfile;
    cloudProfile.id = QStringLiteral("cloud_svc");
    cloudProfile.name = QStringLiteral("Cloud Service");
    cloudProfile.baseUrl = QUrl(QStringLiteral("https://cloud.invalid/v1"));
    cloudProfile.defaultModel = QStringLiteral("cloud-model");
    cloudProfile.timeoutMs = 5000;
    fix.config.saveService(cloudProfile);

    // Moments is disabled by default in PrivacyGuard
    QVERIFY(!fix.privacy.isAllowed(DataCategory::Moments));

    LlmCall cloudCall;
    cloudCall.purpose = Purpose::Query;
    cloudCall.dataCategories = { DataCategory::Moments };
    cloudCall.request.messages.append(
        makeMessage(Role::User, QStringLiteral("cloud query with moments")));

    auto cloudTask = fix.service.start(cloudCall);
    QSignalSpy cloudSpy(cloudTask.get(), &LlmTask::finished);
    QVERIFY(cloudSpy.wait(2000));
    QVERIFY(cloudTask->isFinished());
    QVERIFY(!cloudTask->result().ok());
    QCOMPARE(cloudTask->result().error().code, linernotes::ai::errc::kPrivacyBlocked);
    QVERIFY(cloudTask->result().error().detail.contains(QStringLiteral("moments")));
    // Verify no secret reading or network requests occurred
    QCOMPARE(fix.server.requests().size(), 0);

    // 2. Local service with same Moments data category completes normally
    ServiceProfile localProfile;
    localProfile.id = QStringLiteral("local_svc");
    localProfile.name = QStringLiteral("Local Service");
    localProfile.baseUrl = fix.server.baseUrl();
    localProfile.defaultModel = QStringLiteral("local-model");
    localProfile.timeoutMs = 5000;
    fix.config.saveService(localProfile);
    fix.config.setDefaultServiceId(localProfile.id);
    fix.secrets.write(localProfile.id, QStringLiteral("local-token"), nullptr, nullptr);

    fix.server.enqueue(makeJsonResponse(QStringLiteral("local response ok")));

    LlmCall localCall;
    localCall.purpose = Purpose::Query;
    localCall.dataCategories = { DataCategory::Moments };
    localCall.request.messages.append(
        makeMessage(Role::User, QStringLiteral("local query with moments")));

    auto localTask = fix.service.start(localCall);
    QSignalSpy localSpy(localTask.get(), &LlmTask::finished);
    QVERIFY(localSpy.wait(2000));
    QVERIFY(localTask->isFinished());
    QVERIFY(localTask->result().ok());
    QCOMPARE(localTask->result().value().response.content, QStringLiteral("local response ok"));
    QCOMPARE(fix.server.requests().size(), 1);
}

void TstLlmService::debugLogRecordsAttemptsAndReplayBypassesCache()
{
    Fixture fix;
    fix.debugLog.setEnabled(true);

    RetryPolicy policy;
    policy.maxRetries = 3;
    policy.baseDelayMs = 1;
    policy.maxDelayMs = 100;
    fix.service.setRetryPolicy(policy);

    // 1st attempt: 500 error, 2nd attempt: 200 ok
    FakeLlmServer::Response r500;
    r500.status = 500;
    fix.server.enqueue(r500);
    fix.server.enqueue(makeJsonResponse(QStringLiteral("ok after 500")));

    LlmCall call;
    call.purpose = Purpose::Query;
    call.request.messages.append(makeMessage(Role::User, QStringLiteral("debug log test")));

    auto task = fix.service.start(call);
    QSignalSpy spy(task.get(), &LlmTask::finished);
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(task->isFinished());
    QVERIFY(task->result().ok());
    QCOMPARE(task->result().value().attempts, 2);

    const auto &entries = fix.debugLog.entries();
    QCOMPARE(entries.size(), 2);

    // Entry 1: attempt 1, 500, has error code, empty responseText, requestJson without key
    const auto &e1 = entries.at(0);
    QCOMPARE(e1.attempt, 1);
    QCOMPARE(e1.fromCache, false);
    QCOMPARE(e1.httpStatus, 500);
    QCOMPARE(e1.errorCode, QString(linernotes::ai::errc::kHttp));
    QCOMPARE(e1.responseText, QString());
    const QString req1Str = QString::fromUtf8(QJsonDocument(e1.requestJson).toJson());
    QVERIFY(!req1Str.contains(QStringLiteral("Authorization"), Qt::CaseInsensitive));
    QVERIFY(!req1Str.contains(QStringLiteral("secret-token-123")));

    // Entry 2: attempt 2, 200, ok, responseText contains content, requestJson without key
    const auto &e2 = entries.at(1);
    QCOMPARE(e2.attempt, 2);
    QCOMPARE(e2.fromCache, false);
    QCOMPARE(e2.httpStatus, 200);
    QVERIFY(e2.errorCode.isEmpty());
    QCOMPARE(e2.responseText, QStringLiteral("ok after 500"));
    const QString req2Str = QString::fromUtf8(QJsonDocument(e2.requestJson).toJson());
    QVERIFY(!req2Str.contains(QStringLiteral("Authorization"), Qt::CaseInsensitive));
    QVERIFY(!req2Str.contains(QStringLiteral("secret-token-123")));

    // Replay entry 2: sends a new request even though the response was cached
    fix.server.enqueue(makeJsonResponse(QStringLiteral("fresh replay result")));
    auto replayTask = fix.service.replay(e2);
    QSignalSpy replaySpy(replayTask.get(), &LlmTask::finished);
    QTRY_COMPARE(replaySpy.count(), 1);

    QVERIFY(replayTask->isFinished());
    QVERIFY(replayTask->result().ok());
    QCOMPARE(replayTask->result().value().fromCache, false);
    QCOMPARE(replayTask->result().value().response.content, QStringLiteral("fresh replay result"));
    QCOMPARE(fix.server.requests().size(), 3);

    // Verify 3rd debug entry was recorded for replay
    QCOMPARE(fix.debugLog.entries().size(), 3);
    const auto &e3 = fix.debugLog.entries().at(2);
    QCOMPARE(e3.attempt, 1);
    QCOMPARE(e3.fromCache, false);
    QCOMPARE(e3.responseText, QStringLiteral("fresh replay result"));
}

} // namespace

QTEST_GUILESS_MAIN(TstLlmService)

#include "tst_LlmService.moc"
