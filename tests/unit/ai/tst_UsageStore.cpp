// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/UsageStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::ai::Purpose;
using linernotes::ai::TokenUsage;
using linernotes::ai::UsageRecord;
using linernotes::ai::UsageStore;
using linernotes::library::Database;
using linernotes::library::Migrator;

class TstUsageStore : public QObject {
    Q_OBJECT

private slots:
    void recordAndSummarizeGroupsAndTotals();
    void timeRangeFiltering();
    void skipsUnknownPurposeInDatabase();
};

void TstUsageStore::recordAndSummarizeGroupsAndTotals()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    UsageStore store(db);

    // Group 1: Cleanup, gpt-4o-mini
    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1000,
                .purpose = Purpose::Cleanup,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o-mini"),
                .usage = TokenUsage { .promptTokens = 10, .completionTokens = 5 },
                .cached = false,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 100,
            })
            .ok());

    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1100,
                .purpose = Purpose::Cleanup,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o-mini"),
                .usage = TokenUsage { .promptTokens = 0, .completionTokens = 0 },
                .cached = true,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 10,
            })
            .ok());

    // Group 2: Query, deepseek-chat
    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1200,
                .purpose = Purpose::Query,
                .serviceId = QStringLiteral("svc2"),
                .model = QStringLiteral("deepseek-chat"),
                .usage = TokenUsage { .promptTokens = 500, .completionTokens = 300 },
                .cached = false,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 150,
            })
            .ok());

    // Group 3: Query, gpt-4o (multiple requests, cache hit, and failure)
    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1300,
                .purpose = Purpose::Query,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o"),
                .usage = TokenUsage { .promptTokens = 100, .completionTokens = 50 },
                .cached = false,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 200,
            })
            .ok());

    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1400,
                .purpose = Purpose::Query,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o"),
                .usage = TokenUsage { .promptTokens = 0, .completionTokens = 0 },
                .cached = true,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 5,
            })
            .ok());

    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1500,
                .purpose = Purpose::Query,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o"),
                .usage = TokenUsage { .promptTokens = 0, .completionTokens = 0 },
                .cached = false,
                .ok = false,
                .errorCode = QStringLiteral("ai.http_500"),
                .elapsedMs = 50,
            })
            .ok());

    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1600,
                .purpose = Purpose::Query,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o"),
                .usage = TokenUsage { .promptTokens = 200, .completionTokens = 80 },
                .cached = false,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 220,
            })
            .ok());

    const auto sumRes = store.summarize(500, 3000);
    QVERIFY(sumRes.ok());
    const auto &summaries = sumRes.value();
    QCOMPARE(summaries.size(), 3);

    // Purpose ordering: Cleanup (0) before Query (1)
    // Within Query: deepseek-chat before gpt-4o
    const auto &s0 = summaries.at(0);
    QCOMPARE(s0.purpose, Purpose::Cleanup);
    QCOMPARE(s0.model, QStringLiteral("gpt-4o-mini"));
    QCOMPARE(s0.requests, 1);
    QCOMPARE(s0.cacheHits, 1);
    QCOMPARE(s0.failures, 0);
    QCOMPARE(s0.promptTokens, 10);
    QCOMPARE(s0.completionTokens, 5);

    const auto &s1 = summaries.at(1);
    QCOMPARE(s1.purpose, Purpose::Query);
    QCOMPARE(s1.model, QStringLiteral("deepseek-chat"));
    QCOMPARE(s1.requests, 1);
    QCOMPARE(s1.cacheHits, 0);
    QCOMPARE(s1.failures, 0);
    QCOMPARE(s1.promptTokens, 500);
    QCOMPARE(s1.completionTokens, 300);

    const auto &s2 = summaries.at(2);
    QCOMPARE(s2.purpose, Purpose::Query);
    QCOMPARE(s2.model, QStringLiteral("gpt-4o"));
    QCOMPARE(s2.requests, 3);
    QCOMPARE(s2.cacheHits, 1);
    QCOMPARE(s2.failures, 1);
    QCOMPARE(s2.promptTokens, 300);
    QCOMPARE(s2.completionTokens, 130);
}

void TstUsageStore::timeRangeFiltering()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    UsageStore store(db);

    const auto recordAt = [&](qint64 t, int promptTokens) {
        return store.record(UsageRecord {
            .createdAtMs = t,
            .purpose = Purpose::Query,
            .serviceId = QStringLiteral("svc1"),
            .model = QStringLiteral("gpt-4o"),
            .usage = TokenUsage { .promptTokens = promptTokens, .completionTokens = 0 },
            .cached = false,
            .ok = true,
            .errorCode = QString(),
            .elapsedMs = 50,
        });
    };

    QVERIFY(recordAt(500, 50).ok());
    QVERIFY(recordAt(1000, 100).ok());
    QVERIFY(recordAt(1500, 150).ok());
    QVERIFY(recordAt(1999, 200).ok());
    QVERIFY(recordAt(2000, 250).ok());
    QVERIFY(recordAt(3000, 300).ok());

    // [1000, 2000): includes 1000, 1500, 1999 -> promptTokens = 100 + 150 + 200 = 450, requests = 3
    {
        const auto res = store.summarize(1000, 2000);
        QVERIFY(res.ok());
        const auto &list = res.value();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).requests, 3);
        QCOMPARE(list.at(0).promptTokens, 450);
    }

    // [0, 500): empty (500 is excluded)
    {
        const auto res = store.summarize(0, 500);
        QVERIFY(res.ok());
        QVERIFY(res.value().isEmpty());
    }

    // [4000, 5000): empty
    {
        const auto res = store.summarize(4000, 5000);
        QVERIFY(res.ok());
        QVERIFY(res.value().isEmpty());
    }
}

void TstUsageStore::skipsUnknownPurposeInDatabase()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    UsageStore store(db);

    // Insert valid record
    QVERIFY(store
            .record(UsageRecord {
                .createdAtMs = 1000,
                .purpose = Purpose::Guide,
                .serviceId = QStringLiteral("svc1"),
                .model = QStringLiteral("gpt-4o"),
                .usage = TokenUsage { .promptTokens = 40, .completionTokens = 20 },
                .cached = false,
                .ok = true,
                .errorCode = QString(),
                .elapsedMs = 80,
            })
            .ok());

    // Insert record with unknown purpose directly via SQL
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO llm_usage (created_at, purpose, service_id, model, prompt_tokens, "
            "completion_tokens, cached, ok, error_code, elapsed_ms) "
            "VALUES (1100, 'unknown_unrecognized_purpose', 'svc1', 'gpt-4o', 999, 999, 0, 1, NULL, "
            "50);")));
    }

    const auto res = store.summarize(500, 2000);
    QVERIFY(res.ok());
    const auto &list = res.value();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.at(0).purpose, Purpose::Guide);
    QCOMPARE(list.at(0).promptTokens, 40);
    QCOMPARE(list.at(0).completionTokens, 20);
}

} // namespace

QTEST_GUILESS_MAIN(TstUsageStore)

#include "tst_UsageStore.moc"
