// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <player/PlayMode.h>
#include <player/PlayOrder.h>

#include <algorithm>
#include <numeric>
#include <optional>
#include <set>
#include <vector>

using linernotes::player::PlayMode;
using linernotes::player::PlayOrder;

Q_DECLARE_METATYPE(linernotes::player::PlayMode)
Q_DECLARE_METATYPE(linernotes::player::PlayOrder::Advance)
Q_DECLARE_METATYPE(std::optional<int>)

namespace {

class TstPlayOrder : public QObject {
    Q_OBJECT

private slots:
    void tableBehavior_data();
    void tableBehavior();

    void peekNextMatchesAdvance_data();
    void peekNextMatchesAdvance();

    void shuffleVisitsEveryItemOncePerRound_data();
    void shuffleVisitsEveryItemOncePerRound();

    void shuffleAvoidsShortTermRepeats_data();
    void shuffleAvoidsShortTermRepeats();

    void shuffleIsDeterministicForSeed();

    void deletionBehavior();
    void insertionBehavior();
    void movementBehavior();
    void modeSwitchBehavior();
    void jumpToBehavior();
};

void TstPlayOrder::tableBehavior_data()
{
    QTest::addColumn<PlayMode>("mode");
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("startCurrent");
    QTest::addColumn<PlayOrder::Advance>("advanceType");
    QTest::addColumn<std::optional<int>>("expectedAdvance");
    QTest::addColumn<std::optional<int>>("expectedPrevious");

    // Sequential
    QTest::newRow("Seq count=0 advance(Auto)")
        << PlayMode::Sequential << 0 << -1 << PlayOrder::Advance::Auto
        << std::optional<int>(std::nullopt) << std::optional<int>(std::nullopt);

    QTest::newRow("Seq count=1 start=-1 advance(Auto)")
        << PlayMode::Sequential << 1 << -1 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(std::nullopt);

    QTest::newRow("Seq count=1 start=0 advance(Auto)")
        << PlayMode::Sequential << 1 << 0 << PlayOrder::Advance::Auto
        << std::optional<int>(std::nullopt) << std::optional<int>(std::nullopt);

    QTest::newRow("Seq count=3 start=-1 advance(Auto)")
        << PlayMode::Sequential << 3 << -1 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(std::nullopt);

    QTest::newRow("Seq count=3 start=0 advance(Auto)")
        << PlayMode::Sequential << 3 << 0 << PlayOrder::Advance::Auto << std::optional<int>(1)
        << std::optional<int>(std::nullopt);

    QTest::newRow("Seq count=3 start=1 advance(Auto)")
        << PlayMode::Sequential << 3 << 1 << PlayOrder::Advance::Auto << std::optional<int>(2)
        << std::optional<int>(0);

    QTest::newRow("Seq count=3 start=2 advance(Auto)")
        << PlayMode::Sequential << 3 << 2 << PlayOrder::Advance::Auto
        << std::optional<int>(std::nullopt) << std::optional<int>(1);

    QTest::newRow("Seq count=3 start=2 advance(User)")
        << PlayMode::Sequential << 3 << 2 << PlayOrder::Advance::User
        << std::optional<int>(std::nullopt) << std::optional<int>(1);

    // RepeatAll
    QTest::newRow("RepeatAll count=0 advance(Auto)")
        << PlayMode::RepeatAll << 0 << -1 << PlayOrder::Advance::Auto
        << std::optional<int>(std::nullopt) << std::optional<int>(std::nullopt);

    QTest::newRow("RepeatAll count=1 start=-1 advance(Auto)")
        << PlayMode::RepeatAll << 1 << -1 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(std::nullopt);

    QTest::newRow("RepeatAll count=1 start=0 advance(Auto)")
        << PlayMode::RepeatAll << 1 << 0 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(0);

    QTest::newRow("RepeatAll count=3 start=2 advance(Auto) wraps")
        << PlayMode::RepeatAll << 3 << 2 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(1);

    QTest::newRow("RepeatAll count=3 start=0 previous() wraps")
        << PlayMode::RepeatAll << 3 << 0 << PlayOrder::Advance::Auto << std::optional<int>(1)
        << std::optional<int>(2);

    QTest::newRow("RepeatAll count=3 start=2 advance(User)")
        << PlayMode::RepeatAll << 3 << 2 << PlayOrder::Advance::User << std::optional<int>(0)
        << std::optional<int>(1);

    // RepeatOne
    QTest::newRow("RepeatOne count=0 advance(Auto)")
        << PlayMode::RepeatOne << 0 << -1 << PlayOrder::Advance::Auto
        << std::optional<int>(std::nullopt) << std::optional<int>(std::nullopt);

    QTest::newRow("RepeatOne count=3 start=-1 advance(Auto)")
        << PlayMode::RepeatOne << 3 << -1 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(std::nullopt);

    QTest::newRow("RepeatOne count=3 start=1 advance(Auto) stays")
        << PlayMode::RepeatOne << 3 << 1 << PlayOrder::Advance::Auto << std::optional<int>(1)
        << std::optional<int>(0);

    QTest::newRow("RepeatOne count=3 start=1 advance(User) advances like RepeatAll")
        << PlayMode::RepeatOne << 3 << 1 << PlayOrder::Advance::User << std::optional<int>(2)
        << std::optional<int>(0);

    QTest::newRow("RepeatOne count=3 start=0 previous() wraps like RepeatAll")
        << PlayMode::RepeatOne << 3 << 0 << PlayOrder::Advance::Auto << std::optional<int>(0)
        << std::optional<int>(2);
}

void TstPlayOrder::tableBehavior()
{
    QFETCH(PlayMode, mode);
    QFETCH(int, count);
    QFETCH(int, startCurrent);
    QFETCH(PlayOrder::Advance, advanceType);
    QFETCH(std::optional<int>, expectedAdvance);
    QFETCH(std::optional<int>, expectedPrevious);

    // Test advance
    {
        PlayOrder order(42);
        order.setMode(mode);
        order.reset(count, startCurrent);

        const auto next = order.advance(advanceType);
        QCOMPARE(next, expectedAdvance);
        if (expectedAdvance.has_value()) {
            QCOMPARE(order.current(), expectedAdvance.value_or(-1));
        } else {
            QCOMPARE(order.current(), -1);
        }
    }

    // Test previous
    {
        PlayOrder order(42);
        order.setMode(mode);
        order.reset(count, startCurrent);

        const auto prev = order.previous();
        QCOMPARE(prev, expectedPrevious);
        if (expectedPrevious.has_value()) {
            QCOMPARE(order.current(), expectedPrevious.value_or(-1));
        } else {
            QCOMPARE(order.current(), startCurrent);
        }
    }
}

void TstPlayOrder::peekNextMatchesAdvance_data()
{
    QTest::addColumn<PlayMode>("mode");
    QTest::addColumn<quint64>("seed");
    QTest::addColumn<int>("count");

    QTest::newRow("Sequential seed=1 count=5") << PlayMode::Sequential << 1ULL << 5;
    QTest::newRow("RepeatAll seed=42 count=4") << PlayMode::RepeatAll << 42ULL << 4;
    QTest::newRow("RepeatOne seed=100 count=3") << PlayMode::RepeatOne << 100ULL << 3;
    QTest::newRow("Shuffle seed=42 count=5") << PlayMode::Shuffle << 42ULL << 5;
    QTest::newRow("Shuffle seed=12345 count=10") << PlayMode::Shuffle << 12345ULL << 10;
    QTest::newRow("Shuffle seed=99999999 count=2") << PlayMode::Shuffle << 99999999ULL << 2;
    QTest::newRow("Shuffle seed=777 count=1") << PlayMode::Shuffle << 777ULL << 1;
}

void TstPlayOrder::peekNextMatchesAdvance()
{
    QFETCH(PlayMode, mode);
    QFETCH(quint64, seed);
    QFETCH(int, count);

    // Test Auto advance
    {
        PlayOrder order(seed);
        order.setMode(mode);
        order.reset(count, -1);

        for (int step = 0; step < 50; ++step) {
            const auto peek = order.peekNext(PlayOrder::Advance::Auto);
            const auto adv = order.advance(PlayOrder::Advance::Auto);
            QCOMPARE(peek, adv);
            if (!adv.has_value()) {
                break;
            }
        }
    }

    // Test User advance
    {
        PlayOrder order(seed);
        order.setMode(mode);
        order.reset(count, -1);

        for (int step = 0; step < 50; ++step) {
            const auto peek = order.peekNext(PlayOrder::Advance::User);
            const auto adv = order.advance(PlayOrder::Advance::User);
            QCOMPARE(peek, adv);
            if (!adv.has_value()) {
                break;
            }
        }
    }
}

void TstPlayOrder::shuffleVisitsEveryItemOncePerRound_data()
{
    QTest::addColumn<quint64>("seed");

    QTest::newRow("seed 1") << 1ULL;
    QTest::newRow("seed 42") << 42ULL;
    QTest::newRow("seed 987654321") << 987654321ULL;
    QTest::newRow("seed 0xDEADBEEF") << 0xDEADBEEFULL;
}

void TstPlayOrder::shuffleVisitsEveryItemOncePerRound()
{
    QFETCH(quint64, seed);

    const int n = 10;
    const int rounds = 5;

    PlayOrder order(seed);
    order.setMode(PlayMode::Shuffle);
    order.reset(n, -1);

    for (int r = 0; r < rounds; ++r) {
        std::vector<int> roundItems;
        roundItems.reserve(n);
        for (int i = 0; i < n; ++i) {
            const auto next = order.advance(PlayOrder::Advance::Auto);
            QVERIFY(next.has_value());
            roundItems.push_back(next.value_or(-1));
        }

        std::ranges::sort(roundItems);
        std::vector<int> expected(n);
        std::iota(expected.begin(), expected.end(), 0);
        QCOMPARE(roundItems, expected);
    }
}

void TstPlayOrder::shuffleAvoidsShortTermRepeats_data()
{
    QTest::addColumn<int>("n");
    QTest::addColumn<quint64>("seed");

    const std::vector<int> nValues = { 2, 3, 5, 10, 50, 100 };
    const std::vector<quint64> seeds = { 1ULL, 42ULL, 987654321ULL };

    for (int n : nValues) {
        for (quint64 seed : seeds) {
            const QString tag = QStringLiteral("n=%1 seed=%2").arg(n).arg(seed);
            QTest::newRow(tag.toUtf8().constData()) << n << seed;
        }
    }
}

void TstPlayOrder::shuffleAvoidsShortTermRepeats()
{
    QFETCH(int, n);
    QFETCH(quint64, seed);

    const int k = std::min(n / 2, 20);
    const int rounds = 25;
    const int totalSteps = rounds * n;

    PlayOrder order(seed);
    order.setMode(PlayMode::Shuffle);
    order.reset(n, -1);

    std::vector<int> stream;
    stream.reserve(totalSteps);

    for (int i = 0; i < totalSteps; ++i) {
        const auto next = order.advance(PlayOrder::Advance::Auto);
        QVERIFY(next.has_value());
        stream.push_back(next.value_or(-1));
    }

    // Verify distance between consecutive occurrences of each track
    std::vector<std::vector<int>> occurrences(n);
    for (int i = 0; i < totalSteps; ++i) {
        const int song = stream.at(static_cast<size_t>(i));
        QVERIFY(song >= 0 && song < n);
        occurrences.at(static_cast<size_t>(song)).push_back(i);
    }

    for (int song = 0; song < n; ++song) {
        const auto &posList = occurrences.at(static_cast<size_t>(song));
        for (size_t i = 1; i < posList.size(); ++i) {
            const int gap = posList.at(i) - posList.at(i - 1) - 1;
            QVERIFY2(gap >= k,
                qPrintable(QStringLiteral("Short repeat for song %1: gap=%2 < k=%3 (pos %4 to %5)")
                        .arg(song)
                        .arg(gap)
                        .arg(k)
                        .arg(posList.at(i - 1))
                        .arg(posList.at(i))));
        }
    }
}

void TstPlayOrder::shuffleIsDeterministicForSeed()
{
    const quint64 seedA = 12345678ULL;
    const quint64 seedB = 87654321ULL;
    const int n = 10;
    const int steps = 50;

    PlayOrder orderA1(seedA);
    orderA1.setMode(PlayMode::Shuffle);
    orderA1.reset(n, -1);

    PlayOrder orderA2(seedA);
    orderA2.setMode(PlayMode::Shuffle);
    orderA2.reset(n, -1);

    PlayOrder orderB(seedB);
    orderB.setMode(PlayMode::Shuffle);
    orderB.reset(n, -1);

    std::vector<int> streamA1;
    std::vector<int> streamA2;
    std::vector<int> streamB;

    for (int i = 0; i < steps; ++i) {
        const auto advA1 = orderA1.advance(PlayOrder::Advance::Auto);
        const auto advA2 = orderA2.advance(PlayOrder::Advance::Auto);
        const auto advB = orderB.advance(PlayOrder::Advance::Auto);

        QVERIFY(advA1.has_value());
        QVERIFY(advA2.has_value());
        QVERIFY(advB.has_value());

        streamA1.push_back(advA1.value_or(-1));
        streamA2.push_back(advA2.value_or(-1));
        streamB.push_back(advB.value_or(-1));
    }

    QCOMPARE(streamA1, streamA2);
    QVERIFY(streamA1 != streamB);
}

void TstPlayOrder::deletionBehavior()
{
    // 1. Sequential: delete current track -> current() is -1, advance returns original next track
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(4, 1); // tracks 0, 1, 2, 3; current is 1

        order.onRemoved(1, 1); // remove track 1
        QCOMPARE(order.count(), 3);
        QCOMPARE(order.current(), -1);

        // Advance should return original track 2 (which is now at index 1)
        const auto next = order.advance(PlayOrder::Advance::Auto);
        QCOMPARE(next, std::optional<int>(1));
        QCOMPARE(order.current(), 1);
    }

    // 2. Sequential: delete last track when current is last track -> advance returns nullopt
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(4, 3); // current is 3

        order.onRemoved(3, 1);
        QCOMPARE(order.count(), 3);
        QCOMPARE(order.current(), -1);

        const auto next = order.advance(PlayOrder::Advance::Auto);
        QCOMPARE(next, std::optional<int>(std::nullopt));
        QCOMPARE(order.current(), -1);
    }

    // 3. Sequential: delete before and after current track
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, 3); // current is 3

        // Delete row 1 (before current)
        order.onRemoved(1, 1);
        QCOMPARE(order.count(), 4);
        QCOMPARE(order.current(), 2); // shifted from 3 to 2

        // Delete row 3 (after current)
        order.onRemoved(3, 1);
        QCOMPARE(order.count(), 3);
        QCOMPARE(order.current(), 2); // stays 2
    }

    // 4. Shuffle: delete current track -> current is -1, advance returns next item in shuffle
    // sequence
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);

        const auto first = order.advance(PlayOrder::Advance::Auto);
        QVERIFY(first.has_value());
        const auto second = order.advance(PlayOrder::Advance::Auto);
        const auto peekThird = order.peekNext(PlayOrder::Advance::Auto);

        QVERIFY(second.has_value());
        QVERIFY(peekThird.has_value());

        const int removedSong = second.value_or(-1);
        int expectedNextSong = peekThird.value_or(-1);
        if (expectedNextSong > removedSong) {
            expectedNextSong--; // index shifted down due to removal
        }

        order.onRemoved(removedSong, 1);
        QCOMPARE(order.count(), 4);
        QCOMPARE(order.current(), -1);

        const auto adv = order.advance(PlayOrder::Advance::Auto);
        QCOMPARE(adv, std::optional<int>(expectedNextSong));
        QCOMPARE(order.current(), expectedNextSong);
    }

    // 5. Delete to empty queue
    {
        PlayOrder order(42);
        order.reset(2, 0);
        order.onRemoved(0, 2);
        QCOMPARE(order.count(), 0);
        QCOMPARE(order.current(), -1);
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(std::nullopt));
        QCOMPARE(order.previous(), std::optional<int>(std::nullopt));
    }
}

void TstPlayOrder::insertionBehavior()
{
    // 1. Non-shuffle: indices shift properly
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(3, 1); // current is 1

        // Insert 2 items before current (at row 0)
        order.onInserted(0, 2);
        QCOMPARE(order.count(), 5);
        QCOMPARE(order.current(), 3);

        // Insert 2 items after current (at row 4)
        order.onInserted(4, 2);
        QCOMPARE(order.count(), 7);
        QCOMPARE(order.current(), 3);
    }

    // 2. Shuffle: new items inserted into unplayed portion of round
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);

        std::set<int> playedItems;
        const auto s1 = order.advance(PlayOrder::Advance::Auto);
        const auto s2 = order.advance(PlayOrder::Advance::Auto);
        QVERIFY(s1.has_value());
        QVERIFY(s2.has_value());
        playedItems.insert(s1.value_or(-1));
        playedItems.insert(s2.value_or(-1));

        // Insert 2 items at row 2
        order.onInserted(2, 2);
        QCOMPARE(order.count(), 7);

        // Update played items mapping for insertion at row 2
        std::set<int> updatedPlayed;
        for (int item : playedItems) {
            updatedPlayed.insert(item >= 2 ? item + 2 : item);
        }

        // Advance the remaining 5 items of round 1
        std::vector<int> remainingItems;
        for (int i = 0; i < 5; ++i) {
            const auto nxt = order.advance(PlayOrder::Advance::Auto);
            QVERIFY(nxt.has_value());
            const int nxtVal = nxt.value_or(-1);
            remainingItems.push_back(nxtVal);
            // Must not repeat already-played items
            QVERIFY(!updatedPlayed.contains(nxtVal));
        }

        // All 7 items in total must be present
        std::set<int> allRoundItems = updatedPlayed;
        for (int item : remainingItems) {
            allRoundItems.insert(item);
        }
        QCOMPARE(allRoundItems.size(), 7UL);
    }
}

void TstPlayOrder::movementBehavior()
{
    // 1. Sequential: move current item forward
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, 1); // tracks 0, 1, 2, 3, 4; current is 1

        order.onMoved(1, 3); // move track 1 to index 3
        QCOMPARE(order.current(), 3);

        const auto next = order.advance(PlayOrder::Advance::Auto);
        QCOMPARE(next, std::optional<int>(4));
    }

    // 2. Sequential: move current item backward
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, 3); // current is 3

        order.onMoved(3, 1); // move track 3 to index 1
        QCOMPARE(order.current(), 1);

        const auto next = order.advance(PlayOrder::Advance::Auto);
        QCOMPARE(next, std::optional<int>(2));
    }

    // 3. Shuffle: movement remaps indices without altering shuffle play sequence
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);

        const auto s0 = order.advance(PlayOrder::Advance::Auto);
        const auto s1 = order.advance(PlayOrder::Advance::Auto);
        const auto p2 = order.peekNext(PlayOrder::Advance::Auto);
        QVERIFY(s0.has_value());
        QVERIFY(s1.has_value());
        QVERIFY(p2.has_value());

        const int oldNext = p2.value_or(-1);
        // Move item oldNext from its position to index 0
        order.onMoved(oldNext, 0);

        // Peek should now see the remapped index 0
        const auto newPeek = order.peekNext(PlayOrder::Advance::Auto);
        QCOMPARE(newPeek, std::optional<int>(0));
    }
}

void TstPlayOrder::modeSwitchBehavior()
{
    // 1. Sequential to Shuffle: mid-play switch
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, -1);

        const auto s0 = order.advance(PlayOrder::Advance::Auto); // 0
        const auto s1 = order.advance(PlayOrder::Advance::Auto); // 1
        const auto currentTrack = order.advance(PlayOrder::Advance::Auto); // 2
        QVERIFY(s0.has_value());
        QVERIFY(s1.has_value());
        QCOMPARE(currentTrack, std::optional<int>(2));
        QCOMPARE(order.current(), 2);

        // Switch to Shuffle
        order.setMode(PlayMode::Shuffle);
        QCOMPARE(order.current(), 2);

        // The remaining 4 advances in this round should visit each of {0, 1, 3, 4} once, and not 2
        std::vector<int> remaining;
        for (int i = 0; i < 4; ++i) {
            const auto nxt = order.advance(PlayOrder::Advance::Auto);
            QVERIFY(nxt.has_value());
            const int nxtVal = nxt.value_or(-1);
            QVERIFY(nxtVal != 2);
            remaining.push_back(nxtVal);
        }
        std::ranges::sort(remaining);
        const std::vector<int> expected = { 0, 1, 3, 4 };
        QCOMPARE(remaining, expected);
    }

    // 2. Shuffle to Sequential: continues from current
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);

        const auto s0 = order.advance(PlayOrder::Advance::Auto);
        QVERIFY(s0.has_value());
        const int currentTrack = order.current();

        // Switch to Sequential
        order.setMode(PlayMode::Sequential);
        QCOMPARE(order.current(), currentTrack);

        const auto next = order.advance(PlayOrder::Advance::Auto);
        if (currentTrack + 1 < 5) {
            QCOMPARE(next, std::optional<int>(currentTrack + 1));
        } else {
            QCOMPARE(next, std::optional<int>(std::nullopt));
        }
    }
}

void TstPlayOrder::jumpToBehavior()
{
    // 1. Sequential jumpTo
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, -1);

        order.jumpTo(3);
        QCOMPARE(order.current(), 3);

        const auto next = order.advance(PlayOrder::Advance::Auto);
        QCOMPARE(next, std::optional<int>(4));
    }

    // 2. Shuffle jumpTo: target is treated as already played in this round
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);

        order.jumpTo(3);
        QCOMPARE(order.current(), 3);

        // At first item of round, previous returns nullopt
        QCOMPARE(order.previous(), std::optional<int>(std::nullopt));

        // The next 4 advances visit {0, 1, 2, 4} without repeating 3
        std::vector<int> remaining;
        for (int i = 0; i < 4; ++i) {
            const auto nxt = order.advance(PlayOrder::Advance::Auto);
            QVERIFY(nxt.has_value());
            const int nxtVal = nxt.value_or(-1);
            QVERIFY(nxtVal != 3);
            remaining.push_back(nxtVal);
        }
        std::ranges::sort(remaining);
        const std::vector<int> expected = { 0, 1, 2, 4 };
        QCOMPARE(remaining, expected);
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayOrder)

#include "tst_PlayOrder.moc"
