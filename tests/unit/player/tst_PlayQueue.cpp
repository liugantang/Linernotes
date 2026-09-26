// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QAbstractItemModelTester>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <player/PlayMode.h>
#include <player/PlayOrder.h>
#include <player/PlayQueue.h>

#include <optional>
#include <set>

using linernotes::player::PlayMode;
using linernotes::player::PlayOrder;
using linernotes::player::PlayQueue;
using linernotes::player::QueueItem;

namespace {

class TstPlayQueue : public QObject {
    Q_OBJECT

private slots:
    void modelEditingAndDataRoles();
    void currentIndexTrackingOnModelChanges();
    void insertNextBehavior();
    void navigationAndSignals();
};

void TstPlayQueue::modelEditingAndDataRoles()
{
    PlayQueue queue(42);
    QAbstractItemModelTester tester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    // Initial state
    QCOMPARE(queue.count(), 0);
    QCOMPARE(queue.currentIndex(), -1);

    // setItems
    const QList<QueueItem> initialItems
        = { { .source = QStringLiteral("/music/track1.flac"), .trackId = 101 },
              { .source = QStringLiteral("/music/track2.mp3"), .trackId = 102 } };
    queue.setItems(initialItems, 0);
    QCOMPARE(queue.count(), 2);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.data(queue.index(0, 0), PlayQueue::SourceRole).toString(),
        QStringLiteral("/music/track1.flac"));
    QCOMPARE(queue.data(queue.index(0, 0), PlayQueue::IsCurrentRole).toBool(), true);
    QCOMPARE(queue.data(queue.index(1, 0), PlayQueue::IsCurrentRole).toBool(), false);

    // Append and insert
    queue.append({ { .source = QStringLiteral("/music/track3.wav"), .trackId = 103 } });
    queue.insert(1, { { .source = QStringLiteral("/music/inserted.m4a"), .trackId = 105 } });
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.at(1).source, QStringLiteral("/music/inserted.m4a"));

    // UID monotonic increment & lookup
    QCOMPARE(queue.rowOfUid(queue.at(0).uid), 0);
    QCOMPARE(queue.rowOfUid(queue.at(1).uid), 1);

    // Move & remove
    queue.move(1, 3);
    QCOMPARE(queue.at(3).source, QStringLiteral("/music/inserted.m4a"));

    queue.remove(0, 1);
    QCOMPARE(queue.count(), 3);

    // Clear
    queue.clear();
    QCOMPARE(queue.count(), 0);
    QCOMPARE(queue.currentIndex(), -1);
}

void TstPlayQueue::currentIndexTrackingOnModelChanges()
{
    PlayQueue queue(42);
    QAbstractItemModelTester tester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    const QList<QueueItem> items
        = { { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") },
              { .source = QStringLiteral("s2") }, { .source = QStringLiteral("s3") } };
    queue.setItems(items, -1);
    queue.jumpTo(2); // Current is s2 at index 2
    QCOMPARE(queue.currentIndex(), 2);

    QSignalSpy currentSpy(&queue, &PlayQueue::currentIndexChanged);

    // Insert before current moves index
    queue.insert(0, { { .source = QStringLiteral("x0") } });
    QCOMPARE(queue.currentIndex(), 3);
    const auto item1 = queue.currentItem();
    if (!item1.has_value()) {
        QFAIL("currentItem is nullopt");
        return;
    }
    QCOMPARE(item1->source, QStringLiteral("s2"));

    // Remove before current shifts index back
    queue.remove(0, 1);
    QCOMPARE(queue.currentIndex(), 2);
    const auto item2 = queue.currentItem();
    if (!item2.has_value()) {
        QFAIL("currentItem is nullopt");
        return;
    }
    QCOMPARE(item2->source, QStringLiteral("s2"));

    // Remove current item sets currentIndex to -1
    queue.remove(2, 1);
    QCOMPARE(queue.currentIndex(), -1);
    QCOMPARE(queue.currentItem(), std::nullopt);

    // Auto advance returns item after removed current (s3)
    const auto adv = queue.advance(PlayOrder::Advance::Auto);
    if (!adv.has_value()) {
        QFAIL("adv is nullopt");
        return;
    }
    QCOMPARE(adv->source, QStringLiteral("s3"));
}

void TstPlayQueue::insertNextBehavior()
{
    // Sequential mode: insertNext mid-playback
    {
        PlayQueue queue(42);
        queue.setItems({ { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") } },
            0); // current is s0

        queue.insertNext({ { .source = QStringLiteral("nextA") } });
        QCOMPARE(queue.at(1).source, QStringLiteral("nextA"));

        const auto n1 = queue.advance(PlayOrder::Advance::Auto);
        if (!n1.has_value()) {
            QFAIL("n1 is nullopt");
            return;
        }
        QCOMPARE(n1->source, QStringLiteral("nextA"));

        const auto n2 = queue.advance(PlayOrder::Advance::Auto);
        if (!n2.has_value()) {
            QFAIL("n2 is nullopt");
            return;
        }
        QCOMPARE(n2->source, QStringLiteral("s1"));
    }

    // Shuffle mode: insertNext strictly played next
    {
        PlayQueue queue(42);
        queue.setMode(PlayMode::Shuffle);
        queue.setItems({ { .source = QStringLiteral("orig0") },
            { .source = QStringLiteral("orig1") }, { .source = QStringLiteral("orig2") } });

        const auto s0 = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(s0.has_value());

        queue.insertNext({ { .source = QStringLiteral("nextA") } });

        const auto advA = queue.advance(PlayOrder::Advance::Auto);
        if (!advA.has_value()) {
            QFAIL("advA is nullopt");
            return;
        }
        QCOMPARE(advA->source, QStringLiteral("nextA"));
    }
}

void TstPlayQueue::navigationAndSignals()
{
    PlayQueue queue(42);
    QAbstractItemModelTester tester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    QSignalSpy upcomingSpy(&queue, &PlayQueue::upcomingChanged);
    QSignalSpy modeSpy(&queue, &PlayQueue::modeChanged);
    QSignalSpy countSpy(&queue, &PlayQueue::countChanged);
    QSignalSpy dataSpy(&queue, &PlayQueue::dataChanged);

    queue.setItems({ { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") },
        { .source = QStringLiteral("s2") } });
    QCOMPARE(countSpy.count(), 1);

    // peekNext matches advance
    const auto peek0 = queue.peekNext(PlayOrder::Advance::Auto);
    const auto adv0 = queue.advance(PlayOrder::Advance::Auto);
    QCOMPARE(peek0, adv0);
    QCOMPARE(queue.currentIndex(), 0);
    QVERIFY(dataSpy.count() >= 1);

    // previous()
    queue.advance(PlayOrder::Advance::Auto);
    QCOMPARE(queue.currentIndex(), 1);
    const auto prev = queue.previous();
    QVERIFY(prev.has_value());
    QCOMPARE(queue.currentIndex(), 0);

    // jumpTo
    const auto jumped = queue.jumpTo(2);
    QVERIFY(jumped.has_value());
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(queue.jumpTo(10), std::nullopt);

    // mode change signal
    queue.setMode(PlayMode::RepeatAll);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(queue.mode(), PlayMode::RepeatAll);
    QVERIFY(upcomingSpy.count() >= 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayQueue)

#include "tst_PlayQueue.moc"
