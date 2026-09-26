// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlayCountRule.h>
#include <library/PlayEventStore.h>

#include <optional>
#include <vector>

Q_DECLARE_METATYPE(linernotes::library::PlayCountRule)
Q_DECLARE_METATYPE(std::optional<qint64>)

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::PlayCountRule;
using linernotes::library::PlayEvent;
using linernotes::library::PlayEventStore;

class TstPlayCountRule : public QObject {
    Q_OBJECT

private slots:
    void counts_data();
    void counts();
    void sqlConditionMatchesCounts();
    void normalized();
};

void TstPlayCountRule::counts_data()
{
    QTest::addColumn<PlayCountRule>("rule");
    QTest::addColumn<qint64>("playedMs");
    QTest::addColumn<std::optional<qint64>>("durationMs");
    QTest::addColumn<bool>("expected");

    const PlayCountRule defaultRule { .minPercent = 50, .minSeconds = 240 };
    const PlayCountRule noDurationRule { .minPercent = 50, .minSeconds = 0 };

    // Exactly 50%
    QTest::newRow("exactly 50 percent")
        << defaultRule << qint64 { 100'000 } << std::optional<qint64> { 200'000 } << true;

    // 1 ms short of 50%
    QTest::newRow("1 ms short of 50 percent")
        << defaultRule << qint64 { 99'999 } << std::optional<qint64> { 200'000 } << false;

    // 1 ms over 50%
    QTest::newRow("1 ms over 50 percent")
        << defaultRule << qint64 { 100'001 } << std::optional<qint64> { 200'000 } << true;

    // Unknown duration, seconds condition met
    QTest::newRow("unknown duration meets seconds")
        << defaultRule << qint64 { 240'000 } << std::optional<qint64> { std::nullopt } << true;

    // Unknown duration, seconds condition not met
    QTest::newRow("unknown duration fails seconds")
        << defaultRule << qint64 { 239'999 } << std::optional<qint64> { std::nullopt } << false;

    // 240s condition alone satisfied (long track, playedMs < 50% but >= 240s)
    QTest::newRow("240s alone satisfied")
        << defaultRule << qint64 { 240'000 } << std::optional<qint64> { 600'000 } << true;

    // 240s condition not met and < 50% (long track, playedMs = 239.999s)
    QTest::newRow("240s condition short by 1ms")
        << defaultRule << qint64 { 239'999 } << std::optional<qint64> { 600'000 } << false;

    // minSeconds = 0 only checks percent (duration met)
    QTest::newRow("minSeconds 0 percent met")
        << noDurationRule << qint64 { 100'000 } << std::optional<qint64> { 200'000 } << true;

    // minSeconds = 0 percent not met even if played >= 240s
    QTest::newRow("minSeconds 0 percent not met despite 240s")
        << noDurationRule << qint64 { 240'000 } << std::optional<qint64> { 600'000 } << false;

    // minSeconds = 0 unknown duration always false
    QTest::newRow("minSeconds 0 unknown duration false")
        << noDurationRule << qint64 { 500'000 } << std::optional<qint64> { std::nullopt } << false;
}

void TstPlayCountRule::counts()
{
    QFETCH(PlayCountRule, rule);
    QFETCH(qint64, playedMs);
    QFETCH(std::optional<qint64>, durationMs);
    QFETCH(bool, expected);

    QCOMPARE(rule.counts(playedMs, durationMs), expected);
}

void TstPlayCountRule::sqlConditionMatchesCounts()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    PlayEventStore store(db);

    struct TestCase {
        qint64 id;
        qint64 playedMs;
        std::optional<qint64> durationMs;
    };

    const std::vector<TestCase> testCases = {
        { .id = 1, .playedMs = 100'000, .durationMs = 200'000 },
        { .id = 2, .playedMs = 99'999, .durationMs = 200'000 },
        { .id = 3, .playedMs = 240'000, .durationMs = 600'000 },
        { .id = 4, .playedMs = 239'999, .durationMs = 600'000 },
        { .id = 5, .playedMs = 240'000, .durationMs = std::nullopt },
        { .id = 6, .playedMs = 100'000, .durationMs = std::nullopt },
        { .id = 7, .playedMs = 0, .durationMs = 200'000 },
        { .id = 8, .playedMs = 300'000, .durationMs = 500'000 },
        { .id = 9, .playedMs = 3'600'000, .durationMs = std::nullopt },
    };

    for (const auto &tc : testCases) {
        PlayEvent ev;
        ev.startedAtMs = tc.id * 1000;
        ev.playedMs = tc.playedMs;
        ev.durationMs = tc.durationMs;
        const auto res = store.insert(ev);
        QVERIFY(res.ok());
    }

    const std::vector<PlayCountRule> rulesToTest = {
        PlayCountRule { .minPercent = 50, .minSeconds = 240 },
        PlayCountRule { .minPercent = 50, .minSeconds = 0 },
        PlayCountRule { .minPercent = 80, .minSeconds = 120 },
        PlayCountRule { .minPercent = 100, .minSeconds = 0 },
        PlayCountRule { .minPercent = 1, .minSeconds = 3600 },
    };

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());

    for (const auto &rule : rulesToTest) {
        QSqlQuery query(connRes.value());
        const QString sql = QStringLiteral("SELECT id FROM play_events WHERE ")
            + PlayCountRule::sqlCondition() + QStringLiteral(" ORDER BY id ASC;");
        QVERIFY(query.prepare(sql));
        rule.bindSql(query);
        QVERIFY(query.exec());

        QList<qint64> sqlIds;
        while (query.next()) {
            sqlIds.append(query.value(0).toLongLong());
        }

        QList<qint64> cppIds;
        for (const auto &tc : testCases) {
            if (rule.counts(tc.playedMs, tc.durationMs)) {
                cppIds.append(tc.id);
            }
        }

        QCOMPARE(sqlIds, cppIds);
    }
}

void TstPlayCountRule::normalized()
{
    const PlayCountRule r1 { .minPercent = 0, .minSeconds = -10 };
    const auto n1 = r1.normalized();
    QCOMPARE(n1.minPercent, 1);
    QCOMPARE(n1.minSeconds, 0);

    const PlayCountRule r2 { .minPercent = 150, .minSeconds = 5000 };
    const auto n2 = r2.normalized();
    QCOMPARE(n2.minPercent, 100);
    QCOMPARE(n2.minSeconds, 3600);

    const PlayCountRule r3 { .minPercent = 50, .minSeconds = 240 };
    const auto n3 = r3.normalized();
    QCOMPARE(n3.minPercent, 50);
    QCOMPARE(n3.minSeconds, 240);
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayCountRule)
#include "tst_PlayCountRule.moc"
