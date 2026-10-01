// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QObject>
#include <QString>
#include <QTest>

#include <library/LibraryEnums.h>
#include <nlq/OfflineParser.h>

#include <optional>

namespace {

using namespace linernotes;
using namespace linernotes::nlq;
using namespace linernotes::library;

void verifySpecificConditions(const QString &input, const OfflineParse &parse)
{
    if (input == QStringLiteral("最近常听的日语歌")) {
        QCOMPARE(parse.query.rule.conditions.size(), 2);
        bool hasPlayCount = false;
        bool hasLang = false;
        for (const auto &c : parse.query.rule.conditions) {
            if (c.field == SmartField::PlayCount && c.op == SmartOp::Greater
                && c.value.toInt() == 0) {
                hasPlayCount = true;
            }
            if (c.field == SmartField::Language && c.op == SmartOp::Is
                && c.value.toString() == QStringLiteral("ja")) {
                hasLang = true;
            }
        }
        QVERIFY(hasPlayCount);
        QVERIFY(hasLang);
    } else if (input == QStringLiteral("收藏的专辑")) {
        QCOMPARE(parse.query.rule.conditions.size(), 1);
        const auto &c = parse.query.rule.conditions.at(0);
        QCOMPARE(c.field, SmartField::AlbumFavorite);
        QCOMPARE(c.op, SmartOp::IsTrue);
    } else if (input == QStringLiteral("90年代的现场版 20首")) {
        QCOMPARE(parse.query.rule.conditions.size(), 2);
        bool hasYear = false;
        bool hasLive = false;
        for (const auto &c : parse.query.rule.conditions) {
            if (c.field == SmartField::Year && c.op == SmartOp::Between && c.value.toInt() == 1990
                && c.value2.toInt() == 1999) {
                hasYear = true;
            }
            if (c.field == SmartField::VersionType && c.op == SmartOp::Is
                && c.value.toString() == QStringLiteral("live")) {
                hasLive = true;
            }
        }
        QVERIFY(hasYear);
        QVERIFY(hasLive);
    } else if (input == QStringLiteral("很久没听的歌")) {
        QCOMPARE(parse.query.rule.conditions.size(), 1);
        const auto &c = parse.query.rule.conditions.at(0);
        QCOMPARE(c.field, SmartField::LastPlayed);
        QCOMPARE(c.op, SmartOp::NotInLastDays);
        QCOMPARE(c.value.toInt(), 180);
    } else if (input == QStringLiteral("my most played songs this week")) {
        QCOMPARE(parse.query.rule.conditions.size(), 1);
        const auto &c = parse.query.rule.conditions.at(0);
        QCOMPARE(c.field, SmartField::PlayCount);
        QCOMPARE(c.op, SmartOp::Greater);
        QCOMPARE(c.value.toInt(), 0);
    }
}

class TstOfflineParser : public QObject {
    Q_OBJECT

private slots:
    void parseBasic_data();
    void parseBasic();
    void refinementWithBase();
};

void TstOfflineParser::parseBasic_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("expectedMatchedRule");
    QTest::addColumn<QString>("expectedLeftover");
    QTest::addColumn<Entity>("expectedEntity");
    QTest::addColumn<SortKey>("expectedSortKey");
    QTest::addColumn<Qt::SortOrder>("expectedSortOrder");
    QTest::addColumn<int>("expectedLimit");
    QTest::addColumn<QDate>("expectedPlayedFrom");
    QTest::addColumn<QDate>("expectedPlayedTo");

    // Case 1: 最近常听的日语歌
    QTest::newRow("most_played_japanese_last_30_days")
        << QStringLiteral("最近常听的日语歌") << true << QString() << Entity::Track
        << SortKey::PlayCount << Qt::DescendingOrder << 50 << QDate(2026, 9, 1)
        << QDate(2026, 10, 1);

    // Case 2: 收藏的专辑
    QTest::newRow("favorite_albums")
        << QStringLiteral("收藏的专辑") << true << QString() << Entity::Album << SortKey::Default
        << Qt::DescendingOrder << 50 << QDate() << QDate();

    // Case 3: 90年代的现场版 20首
    QTest::newRow("90s_live_20_tracks")
        << QStringLiteral("90年代的现场版 20首") << true << QString() << Entity::Track
        << SortKey::Default << Qt::DescendingOrder << 20 << QDate() << QDate();

    // Case 4: 很久没听的歌
    QTest::newRow("havenot_played_songs")
        << QStringLiteral("很久没听的歌") << true << QString() << Entity::Track
        << SortKey::LastPlayed << Qt::AscendingOrder << 50 << QDate() << QDate();

    // Case 5: 周杰伦
    QTest::newRow("pure_artist_name")
        << QStringLiteral("周杰伦") << false << QStringLiteral("周杰伦") << Entity::Track
        << SortKey::Default << Qt::DescendingOrder << 50 << QDate() << QDate();

    // Case 7: my most played songs this week
    QTest::newRow("english_most_played_this_week")
        << QStringLiteral("my most played songs this week") << true << QString() << Entity::Track
        << SortKey::PlayCount << Qt::DescendingOrder << 50 << QDate(2026, 9, 25)
        << QDate(2026, 10, 1);
}

void TstOfflineParser::parseBasic()
{
    QFETCH(QString, input);
    QFETCH(bool, expectedMatchedRule);
    QFETCH(QString, expectedLeftover);
    QFETCH(Entity, expectedEntity);
    QFETCH(SortKey, expectedSortKey);
    QFETCH(Qt::SortOrder, expectedSortOrder);
    QFETCH(int, expectedLimit);
    QFETCH(QDate, expectedPlayedFrom);
    QFETCH(QDate, expectedPlayedTo);

    const QDate today(2026, 10, 1);
    const OfflineParse parse = parseOffline(input, today, std::nullopt);

    QCOMPARE(parse.matchedRule, expectedMatchedRule);
    QCOMPARE(parse.leftover, expectedLeftover);
    QCOMPARE(parse.query.entity, expectedEntity);
    QCOMPARE(parse.query.sortKey, expectedSortKey);
    QCOMPARE(parse.query.sortOrder, expectedSortOrder);
    QCOMPARE(parse.query.limit, expectedLimit);

    const std::optional<QDate> optFrom
        = expectedPlayedFrom.isValid() ? std::optional<QDate>(expectedPlayedFrom) : std::nullopt;
    const std::optional<QDate> optTo
        = expectedPlayedTo.isValid() ? std::optional<QDate>(expectedPlayedTo) : std::nullopt;

    QCOMPARE(parse.query.rule.playedFrom, optFrom);
    QCOMPARE(parse.query.rule.playedTo, optTo);

    verifySpecificConditions(input, parse);
}

void TstOfflineParser::refinementWithBase()
{
    const QDate today(2026, 10, 1);

    // Case 6: base = "日语歌" -> "只要现场版"
    const OfflineParse baseParse = parseOffline(QStringLiteral("日语歌"), today, std::nullopt);
    QVERIFY(baseParse.matchedRule);
    QCOMPARE(baseParse.query.rule.conditions.size(), 1);
    QCOMPARE(baseParse.query.rule.conditions.at(0).field, SmartField::Language);
    QCOMPARE(baseParse.query.rule.conditions.at(0).value.toString(), QStringLiteral("ja"));

    const OfflineParse refined = parseOffline(QStringLiteral("只要现场版"), today, baseParse.query);
    QVERIFY(refined.matchedRule);
    QVERIFY(refined.leftover.isEmpty());
    QCOMPARE(refined.query.rule.conditions.size(), 2);

    bool hasJa = false;
    bool hasLive = false;
    for (const auto &c : refined.query.rule.conditions) {
        if (c.field == SmartField::Language && c.op == SmartOp::Is
            && c.value.toString() == QStringLiteral("ja")) {
            hasJa = true;
        }
        if (c.field == SmartField::VersionType && c.op == SmartOp::Is
            && c.value.toString() == QStringLiteral("live")) {
            hasLive = true;
        }
    }
    QVERIFY(hasJa);
    QVERIFY(hasLive);
}

} // namespace

QTEST_GUILESS_MAIN(TstOfflineParser)
#include "tst_OfflineParser.moc"
