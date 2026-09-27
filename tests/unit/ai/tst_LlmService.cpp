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
#include <ai/LlmService.h>
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
using linernotes::ai::LlmCache;
using linernotes::ai::LlmCall;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmService;
using linernotes::ai::LlmTask;
using linernotes::ai::makeMessage;
using linernotes::ai::MemorySecretStore;
using linernotes::ai::Purpose;
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
        , service(config, secrets, client, cache, usage, clock)
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

} // namespace

QTEST_GUILESS_MAIN(TstLlmService)

#include "tst_LlmService.moc"
