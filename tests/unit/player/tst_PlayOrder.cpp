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

namespace {

class TstPlayOrder : public QObject {
    Q_OBJECT

private slots:
    void sequentialAndRepeatModes();
    void peekNextMatchesAdvance();
    void shuffleVisitsEveryItemOnceAndAvoidsShortRepeats();
    void shuffleIsDeterministicForSeed();
    void queueMutations();
    void modeSwitchAndJumpTo();
    void scheduleNextBehavior();
};

void TstPlayOrder::sequentialAndRepeatModes()
{
    // Sequential: advances until end, then nullopt; previous() does not wrap
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(3, -1);

        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(0));
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(1));
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(2));
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::nullopt);
        QCOMPARE(order.previous(), std::nullopt);
    }

    // RepeatAll: wraps around at boundaries for both advance and previous
    {
        PlayOrder order(42);
        order.setMode(PlayMode::RepeatAll);
        order.reset(3, 2);

        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(0));
        QCOMPARE(order.previous(), std::optional<int>(2));
    }

    // RepeatOne: Auto stays on same track; User advance advances like RepeatAll
    {
        PlayOrder order(42);
        order.setMode(PlayMode::RepeatOne);
        order.reset(3, 1);

        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(1));
        QCOMPARE(order.advance(PlayOrder::Advance::User), std::optional<int>(2));
        QCOMPARE(order.previous(), std::optional<int>(1));
    }
}

void TstPlayOrder::peekNextMatchesAdvance()
{
    for (auto mode :
        { PlayMode::Sequential, PlayMode::RepeatAll, PlayMode::RepeatOne, PlayMode::Shuffle }) {
        PlayOrder order(42);
        order.setMode(mode);
        order.reset(5, -1);

        for (int step = 0; step < 10; ++step) {
            const auto peek = order.peekNext(PlayOrder::Advance::Auto);
            const auto adv = order.advance(PlayOrder::Advance::Auto);
            QCOMPARE(peek, adv);
            if (!adv.has_value()) {
                break;
            }
        }
    }
}

void TstPlayOrder::shuffleVisitsEveryItemOnceAndAvoidsShortRepeats()
{
    const int n = 10;
    const int rounds = 10;
    PlayOrder order(42);
    order.setMode(PlayMode::Shuffle);
    order.reset(n, -1);

    std::vector<int> stream;
    stream.reserve(static_cast<size_t>(rounds) * static_cast<size_t>(n));

    for (int r = 0; r < rounds; ++r) {
        std::vector<int> roundItems;
        roundItems.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const auto next = order.advance(PlayOrder::Advance::Auto);
            if (!next.has_value()) {
                QFAIL("next is nullopt");
                return;
            }
            const int val = *next;
            roundItems.push_back(val);
            stream.push_back(val);
        }
        std::ranges::sort(roundItems);
        std::vector<int> expected(n);
        std::iota(expected.begin(), expected.end(), 0);
        QCOMPARE(roundItems, expected); // All items visited once per round
    }

    // Check minimum gap between consecutive plays of each track (k = min(n/2, 20) = 5)
    const int k = std::min(n / 2, 20);
    std::vector<std::vector<int>> occurrences(n);
    for (size_t i = 0; i < stream.size(); ++i) {
        occurrences.at(static_cast<size_t>(stream.at(i))).push_back(static_cast<int>(i));
    }
    for (int song = 0; song < n; ++song) {
        const auto &pos = occurrences.at(static_cast<size_t>(song));
        for (size_t i = 1; i < pos.size(); ++i) {
            QVERIFY(pos.at(i) - pos.at(i - 1) - 1 >= k);
        }
    }
}

void TstPlayOrder::shuffleIsDeterministicForSeed()
{
    const quint64 seedA = 12345678ULL;
    const quint64 seedB = 87654321ULL;
    const int n = 8;
    const int steps = 20;

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
        streamA1.push_back(orderA1.advance(PlayOrder::Advance::Auto).value_or(-1));
        streamA2.push_back(orderA2.advance(PlayOrder::Advance::Auto).value_or(-1));
        streamB.push_back(orderB.advance(PlayOrder::Advance::Auto).value_or(-1));
    }

    QCOMPARE(streamA1, streamA2);
    QVERIFY(streamA1 != streamB);
}

void TstPlayOrder::queueMutations()
{
    // Insertion
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(3, 1); // current is 1
        order.onInserted(0, 2); // insert 2 before
        QCOMPARE(order.count(), 5);
        QCOMPARE(order.current(), 3); // current shifted
    }

    // Deletion: removing current track sets current to -1, advance goes to next remaining
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(4, 1);
        order.onRemoved(1, 1);
        QCOMPARE(order.count(), 3);
        QCOMPARE(order.current(), -1);
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(1));
    }

    // Movement: moving current re-tracks index
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, 1);
        order.onMoved(1, 3);
        QCOMPARE(order.current(), 3);
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(4));
    }
}

void TstPlayOrder::modeSwitchAndJumpTo()
{
    // Mode switch: Sequential to Shuffle retains current and visits remaining
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Sequential);
        order.reset(5, -1);
        order.advance(PlayOrder::Advance::Auto); // 0
        order.advance(PlayOrder::Advance::Auto); // 1
        order.advance(PlayOrder::Advance::Auto); // 2
        QCOMPARE(order.current(), 2);

        order.setMode(PlayMode::Shuffle);
        QCOMPARE(order.current(), 2);

        std::vector<int> remaining;
        remaining.reserve(4);
        for (int i = 0; i < 4; ++i) {
            const auto nxt = order.advance(PlayOrder::Advance::Auto);
            if (!nxt.has_value() || *nxt == 2) {
                QFAIL("nxt is unexpected");
                return;
            }
            remaining.push_back(*nxt);
        }
        std::ranges::sort(remaining);
        QCOMPARE(remaining, (std::vector<int> { 0, 1, 3, 4 }));
    }

    // JumpTo: marks target as current and treats as played in shuffle round
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);
        order.jumpTo(3);
        QCOMPARE(order.current(), 3);

        std::vector<int> remaining;
        remaining.reserve(4);
        for (int i = 0; i < 4; ++i) {
            remaining.push_back(order.advance(PlayOrder::Advance::Auto).value_or(-1));
        }
        std::ranges::sort(remaining);
        QCOMPARE(remaining, (std::vector<int> { 0, 1, 2, 4 }));
    }
}

void TstPlayOrder::scheduleNextBehavior()
{
    // Unstarted shuffle queue: scheduleNext sets next track
    {
        PlayOrder order(42);
        order.setMode(PlayMode::Shuffle);
        order.reset(5, -1);
        order.scheduleNext(3);

        QCOMPARE(order.peekNext(PlayOrder::Advance::Auto), std::optional<int>(3));
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(3));
        QCOMPARE(order.current(), 3);
    }

    // Mid-round multiple scheduleNext: LIFO order (last scheduled plays next)
    {
        PlayOrder order(12345);
        order.setMode(PlayMode::Shuffle);
        order.reset(6, -1);
        const auto firstOpt = order.advance(PlayOrder::Advance::Auto);
        if (!firstOpt.has_value()) {
            QFAIL("first is nullopt");
            return;
        }
        const int first = *firstOpt;

        const int b = (first + 1) % 6;
        const int a = (first + 2) % 6;
        order.scheduleNext(b);
        order.scheduleNext(a);

        QCOMPARE(order.peekNext(PlayOrder::Advance::Auto), std::optional<int>(a));
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(a));
        QCOMPARE(order.peekNext(PlayOrder::Advance::Auto), std::optional<int>(b));
        QCOMPARE(order.advance(PlayOrder::Advance::Auto), std::optional<int>(b));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayOrder)

#include "tst_PlayOrder.moc"
