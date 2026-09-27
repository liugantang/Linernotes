// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonObject>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/LlmCache.h>
#include <ai/StructuredOutput.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/Migrator.h>

using linernotes::ai::ChatRequest;
using linernotes::ai::ChatResponse;
using linernotes::ai::LlmCache;
using linernotes::ai::makeMessage;
using linernotes::ai::Purpose;
using linernotes::ai::Role;
using linernotes::ai::StructuredMode;
using linernotes::ai::StructuredSpec;
using linernotes::ai::TokenUsage;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

namespace {

class TstLlmCache : public QObject {
    Q_OBJECT

private slots:
    void makeKeyConsistencyAndDifferences();
    void getHitIncrementsHits();
    void purgeExpiredDeletesOnlyExpired();
    void clearPurgesAll();
};

void TstLlmCache::makeKeyConsistencyAndDifferences()
{
    const QUrl baseUrl(QStringLiteral("http://localhost:11434/v1"));
    const QString model1 = QStringLiteral("deepseek-chat");
    const QString model2 = QStringLiteral("gpt-4o");

    ChatRequest req1;
    req1.messages.append(makeMessage(Role::User, QStringLiteral("Hello")));

    ChatRequest req2;
    req2.messages.append(makeMessage(Role::User, QStringLiteral("Hello")));

    ChatRequest req3;
    req3.messages.append(makeMessage(Role::User, QStringLiteral("World")));

    StructuredSpec spec1 {
        .name = QStringLiteral("format1"),
        .description = QStringLiteral("desc1"),
        .schema = QJsonObject { { QStringLiteral("type"), QStringLiteral("object") } },
    };

    StructuredSpec spec2 {
        .name = QStringLiteral("format2"),
        .description = QStringLiteral("desc2"),
        .schema = QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } },
    };

    // Same input produces same key
    const QString k1
        = LlmCache::makeKey(baseUrl, model1, req1, std::nullopt, StructuredMode::Prompt);
    const QString k2
        = LlmCache::makeKey(baseUrl, model1, req2, std::nullopt, StructuredMode::Prompt);
    QCOMPARE(k1, k2);

    // Different model produces different key
    const QString kModelDiff
        = LlmCache::makeKey(baseUrl, model2, req1, std::nullopt, StructuredMode::Prompt);
    QVERIFY(k1 != kModelDiff);

    // Different message content produces different key
    const QString kMsgDiff
        = LlmCache::makeKey(baseUrl, model1, req3, std::nullopt, StructuredMode::Prompt);
    QVERIFY(k1 != kMsgDiff);

    // With structured spec produces different key
    const QString kSpec1 = LlmCache::makeKey(baseUrl, model1, req1, spec1, StructuredMode::Prompt);
    QVERIFY(k1 != kSpec1);

    // Different schema produces different key
    const QString kSpec2 = LlmCache::makeKey(baseUrl, model1, req1, spec2, StructuredMode::Prompt);
    QVERIFY(kSpec1 != kSpec2);

    // Different structured mode produces different key
    const QString kModeTool = LlmCache::makeKey(baseUrl, model1, req1, spec1, StructuredMode::Tool);
    QVERIFY(kSpec1 != kModeTool);
}

void TstLlmCache::getHitIncrementsHits()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    LlmCache cache(db, clock);

    ChatResponse resp;
    resp.content = QStringLiteral("cached result");
    resp.model = QStringLiteral("model-a");
    resp.finishReason = QStringLiteral("stop");
    resp.usage = TokenUsage { .promptTokens = 10, .completionTokens = 5 };

    const QString key = QStringLiteral("cache-key-1");
    const auto putRes
        = cache.put(key, QStringLiteral("model-a"), Purpose::Query, resp, std::nullopt);
    QVERIFY(putRes.ok());

    // Verify initial hits is 0
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT hits FROM llm_cache WHERE key = 'cache-key-1'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    // First get
    const auto got1 = cache.get(key);
    QVERIFY(got1.has_value());
    if (!got1.has_value()) {
        return;
    }
    QCOMPARE(got1->content, QStringLiteral("cached result"));
    QCOMPARE(got1->model, QStringLiteral("model-a"));
    QCOMPARE(got1->usage.promptTokens, 10);
    QCOMPARE(got1->usage.completionTokens, 5);

    // Verify hits incremented to 1
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT hits FROM llm_cache WHERE key = 'cache-key-1'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // Second get
    const auto got2 = cache.get(key);
    QVERIFY(got2.has_value());

    // Verify hits incremented to 2
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT hits FROM llm_cache WHERE key = 'cache-key-1'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
    }
}

void TstLlmCache::purgeExpiredDeletesOnlyExpired()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    LlmCache cache(db, clock);

    ChatResponse resp;
    resp.content = QStringLiteral("test");

    QVERIFY(cache
            .put(QStringLiteral("k_never"), QStringLiteral("m"), Purpose::Query, resp, std::nullopt)
            .ok());
    QVERIFY(cache.put(QStringLiteral("k_exp_2000"), QStringLiteral("m"), Purpose::Query, resp, 1000)
            .ok());
    QVERIFY(cache.put(QStringLiteral("k_exp_5000"), QStringLiteral("m"), Purpose::Query, resp, 4000)
            .ok());

    // At t=1500, nothing expired
    clock.set(1500);
    auto purge1 = cache.purgeExpired();
    QVERIFY(purge1.ok());
    QCOMPARE(purge1.value(), 0);
    QVERIFY(cache.get(QStringLiteral("k_never")).has_value());
    QVERIFY(cache.get(QStringLiteral("k_exp_2000")).has_value());
    QVERIFY(cache.get(QStringLiteral("k_exp_5000")).has_value());

    // At t=2500, k_exp_2000 is expired
    clock.set(2500);
    auto purge2 = cache.purgeExpired();
    QVERIFY(purge2.ok());
    QCOMPARE(purge2.value(), 1);
    QVERIFY(cache.get(QStringLiteral("k_never")).has_value());
    QVERIFY(!cache.get(QStringLiteral("k_exp_2000")).has_value());
    QVERIFY(cache.get(QStringLiteral("k_exp_5000")).has_value());

    // At t=6000, k_exp_5000 is expired
    clock.set(6000);
    auto purge3 = cache.purgeExpired();
    QVERIFY(purge3.ok());
    QCOMPARE(purge3.value(), 1);
    QVERIFY(cache.get(QStringLiteral("k_never")).has_value());
    QVERIFY(!cache.get(QStringLiteral("k_exp_5000")).has_value());
}

void TstLlmCache::clearPurgesAll()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    LlmCache cache(db, clock);

    ChatResponse resp;
    resp.content = QStringLiteral("test");

    QVERIFY(cache.put(QStringLiteral("k1"), QStringLiteral("m"), Purpose::Query, resp, std::nullopt)
            .ok());
    QVERIFY(
        cache.put(QStringLiteral("k2"), QStringLiteral("m"), Purpose::Dj, resp, std::nullopt).ok());

    QVERIFY(cache.get(QStringLiteral("k1")).has_value());
    QVERIFY(cache.get(QStringLiteral("k2")).has_value());

    QVERIFY(cache.clear().ok());

    QVERIFY(!cache.get(QStringLiteral("k1")).has_value());
    QVERIFY(!cache.get(QStringLiteral("k2")).has_value());
}

} // namespace

QTEST_GUILESS_MAIN(TstLlmCache)

#include "tst_LlmCache.moc"
