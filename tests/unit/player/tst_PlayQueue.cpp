// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QAbstractItemModelTester>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <player/PlayMode.h>
#include <player/PlayOrder.h>
#include <player/PlayQueue.h>

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

using linernotes::player::PlayMode;
using linernotes::player::PlayOrder;
using linernotes::player::PlayQueue;
using linernotes::player::QueueItem;

Q_DECLARE_METATYPE(linernotes::player::PlayMode)
Q_DECLARE_METATYPE(linernotes::player::QueueItem)

namespace {

class TstPlayQueue : public QObject {
    Q_OBJECT

private slots:
    void modelEditingAndDataRoles();
    void uidBehavior();
    void currentIndexTrackingOnModelChanges();
    void insertNextBehavior();
    void navigationBehavior();
    void upcomingChangedAndModeChangedSignals();
    void isCurrentRoleDataChanged();
};

void TstPlayQueue::modelEditingAndDataRoles()
{
    PlayQueue queue(42);
    auto *tester = new QAbstractItemModelTester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    // 1. Check roleNames
    const auto roles = queue.roleNames();
    QCOMPARE(roles.value(PlayQueue::SourceRole), QByteArray("source"));
    QCOMPARE(roles.value(PlayQueue::TrackIdRole), QByteArray("trackId"));
    QCOMPARE(roles.value(PlayQueue::UidRole), QByteArray("uid"));
    QCOMPARE(roles.value(PlayQueue::IsCurrentRole), QByteArray("isCurrent"));

    // 2. Initial state
    QCOMPARE(queue.count(), 0);
    QCOMPARE(queue.rowCount(), 0);
    QCOMPARE(queue.currentIndex(), -1);

    // 3. setItems
    const QList<QueueItem> initialItems
        = { { .source = QStringLiteral("/music/track1.flac"), .trackId = 101 },
              { .source = QStringLiteral("/music/track2.mp3"), .trackId = 102 } };
    queue.setItems(initialItems, 0);
    QCOMPARE(queue.count(), 2);
    QCOMPARE(queue.rowCount(), 2);
    QCOMPARE(queue.rowCount(queue.index(0, 0)), 0);
    QCOMPARE(queue.currentIndex(), 0);

    // Check data roles
    QCOMPARE(
        queue.data(queue.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("track1.flac"));
    QCOMPARE(queue.data(queue.index(0, 0), PlayQueue::SourceRole).toString(),
        QStringLiteral("/music/track1.flac"));
    QCOMPARE(queue.data(queue.index(0, 0), PlayQueue::TrackIdRole).toLongLong(), 101LL);
    QCOMPARE(queue.data(queue.index(0, 0), PlayQueue::IsCurrentRole).toBool(), true);
    QCOMPARE(queue.data(queue.index(1, 0), PlayQueue::IsCurrentRole).toBool(), false);
    QCOMPARE(
        queue.data(queue.index(1, 0), Qt::DisplayRole).toString(), QStringLiteral("track2.mp3"));

    // 4. append
    queue.append({ { .source = QStringLiteral("/music/track3.wav"), .trackId = 103 },
        { .source = QStringLiteral("/music/track4.ogg"), .trackId = 104 } });
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.at(2).source, QStringLiteral("/music/track3.wav"));
    QCOMPARE(queue.at(3).source, QStringLiteral("/music/track4.ogg"));

    // 5. insert at index 1
    queue.insert(1, { { .source = QStringLiteral("/music/inserted.m4a"), .trackId = 105 } });
    QCOMPARE(queue.count(), 5);
    QCOMPARE(queue.at(1).source, QStringLiteral("/music/inserted.m4a"));
    QCOMPARE(queue.at(2).source, QStringLiteral("/music/track2.mp3"));

    // 6. move forward: 1 to 3
    queue.move(1, 3);
    QCOMPARE(queue.count(), 5);
    QCOMPARE(queue.at(3).source, QStringLiteral("/music/inserted.m4a"));
    QCOMPARE(queue.at(1).source, QStringLiteral("/music/track2.mp3"));

    // 7. move backward: 3 to 0
    queue.move(3, 0);
    QCOMPARE(queue.count(), 5);
    QCOMPARE(queue.at(0).source, QStringLiteral("/music/inserted.m4a"));

    // 8. remove
    queue.remove(0, 1);
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.at(0).source, QStringLiteral("/music/track1.flac"));

    queue.remove(1, 2);
    QCOMPARE(queue.count(), 2);

    // 9. clear
    queue.clear();
    QCOMPARE(queue.count(), 0);
    QCOMPARE(queue.rowCount(), 0);
    QCOMPARE(queue.currentIndex(), -1);
}

void TstPlayQueue::uidBehavior()
{
    PlayQueue queue(42);
    auto *tester = new QAbstractItemModelTester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    // 1. Initial additions: UID monotonic increment, input UID ignored
    queue.append({ { .source = QStringLiteral("a.mp3"), .trackId = 1, .uid = 999 },
        { .source = QStringLiteral("b.mp3"), .trackId = 2, .uid = 888 } });
    QCOMPARE(queue.at(0).uid, 1ULL);
    QCOMPARE(queue.at(1).uid, 2ULL);

    // 2. rowOfUid
    QCOMPARE(queue.rowOfUid(1), 0);
    QCOMPARE(queue.rowOfUid(2), 1);
    QCOMPARE(queue.rowOfUid(999), -1);

    // 3. insert in middle
    queue.insert(1, { { .source = QStringLiteral("c.mp3"), .trackId = 3, .uid = 777 } });
    QCOMPARE(queue.at(1).uid, 3ULL);
    QCOMPARE(queue.rowOfUid(1), 0);
    QCOMPARE(queue.rowOfUid(3), 1);
    QCOMPARE(queue.rowOfUid(2), 2);

    // 4. clear does not reuse UID
    queue.clear();
    QCOMPARE(queue.rowOfUid(1), -1);

    queue.append({ { .source = QStringLiteral("d.mp3") } });
    QCOMPARE(queue.at(0).uid, 4ULL);
    QCOMPARE(queue.rowOfUid(4), 0);

    // 5. setItems does not reuse UID
    queue.setItems(
        { { .source = QStringLiteral("e.mp3") }, { .source = QStringLiteral("f.mp3") } });
    QCOMPARE(queue.at(0).uid, 5ULL);
    QCOMPARE(queue.at(1).uid, 6ULL);
    QCOMPARE(queue.rowOfUid(5), 0);
    QCOMPARE(queue.rowOfUid(6), 1);
}

void TstPlayQueue::currentIndexTrackingOnModelChanges()
{
    PlayQueue queue(42);
    auto *tester = new QAbstractItemModelTester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    const QList<QueueItem> items = { { .source = QStringLiteral("s0") },
        { .source = QStringLiteral("s1") }, { .source = QStringLiteral("s2") },
        { .source = QStringLiteral("s3") }, { .source = QStringLiteral("s4") } };
    queue.setItems(items, -1);
    queue.jumpTo(2); // Current is s2 at index 2
    QCOMPARE(queue.currentIndex(), 2);
    QCOMPARE(queue.currentItem()->source, QStringLiteral("s2"));

    QSignalSpy currentSpy(&queue, &PlayQueue::currentIndexChanged);

    // 1. Insert before current (at 0, 2 items)
    queue.insert(0, { { .source = QStringLiteral("x0") }, { .source = QStringLiteral("x1") } });
    QCOMPARE(queue.currentIndex(), 4);
    QCOMPARE(queue.currentItem()->source, QStringLiteral("s2"));
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(currentSpy.takeFirst().at(0).toInt(), 4);

    // 2. Insert after current (at 5, 2 items)
    queue.insert(5, { { .source = QStringLiteral("y0") }, { .source = QStringLiteral("y1") } });
    QCOMPARE(queue.currentIndex(), 4);
    QCOMPARE(queue.currentItem()->source, QStringLiteral("s2"));
    QCOMPARE(currentSpy.count(), 0);

    // 3. Move before current (row 0 to row 1)
    queue.move(0, 1);
    QCOMPARE(queue.currentIndex(), 4);
    QCOMPARE(currentSpy.count(), 0);

    // 4. Move after current to before current (row 8 to row 0)
    queue.move(8, 0);
    QCOMPARE(queue.currentIndex(), 5);
    QCOMPARE(queue.currentItem()->source, QStringLiteral("s2"));
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(currentSpy.takeFirst().at(0).toInt(), 5);

    // 5. Move current item itself (row 5 to row 1)
    queue.move(5, 1);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.currentItem()->source, QStringLiteral("s2"));
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(currentSpy.takeFirst().at(0).toInt(), 1);

    // 6. Remove before current (row 0)
    queue.remove(0, 1);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.currentItem()->source, QStringLiteral("s2"));
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(currentSpy.takeFirst().at(0).toInt(), 0);

    // 7. Remove current item itself (row 0)
    const QString nextExpectedSource = queue.at(1).source;
    queue.remove(0, 1);
    QCOMPARE(queue.currentIndex(), -1);
    QCOMPARE(queue.currentItem(), std::nullopt);
    QCOMPARE(currentSpy.count(), 1);
    QCOMPARE(currentSpy.takeFirst().at(0).toInt(), -1);

    // Advance(Auto) should now return the track originally following the deleted current track
    const auto adv = queue.advance(PlayOrder::Advance::Auto);
    QVERIFY(adv.has_value());
    QCOMPARE(adv->source, nextExpectedSource);
    QCOMPARE(queue.currentIndex(), 0);
}

void TstPlayQueue::insertNextBehavior()
{
    // 1. Sequential mode: insertNext when unstarted (current == -1)
    {
        PlayQueue queue(42);
        auto *tester = new QAbstractItemModelTester(
            &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
        Q_UNUSED(tester);

        queue.insertNext(
            { { .source = QStringLiteral("a.mp3") }, { .source = QStringLiteral("b.mp3") } });
        QCOMPARE(queue.count(), 2);
        QCOMPARE(queue.currentIndex(), -1);

        const auto first = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(first.has_value());
        QCOMPARE(first->source, QStringLiteral("a.mp3"));

        const auto second = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(second.has_value());
        QCOMPARE(second->source, QStringLiteral("b.mp3"));
    }

    // 2. Sequential mode: insertNext mid-playback
    {
        PlayQueue queue(42);
        auto *tester = new QAbstractItemModelTester(
            &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
        Q_UNUSED(tester);

        queue.setItems({ { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") },
                           { .source = QStringLiteral("s2") } },
            0); // current is s0 at index 0

        queue.insertNext(
            { { .source = QStringLiteral("nextA") }, { .source = QStringLiteral("nextB") } });
        // Model order should now be: s0, nextA, nextB, s1, s2
        QCOMPARE(queue.count(), 5);
        QCOMPARE(queue.at(0).source, QStringLiteral("s0"));
        QCOMPARE(queue.at(1).source, QStringLiteral("nextA"));
        QCOMPARE(queue.at(2).source, QStringLiteral("nextB"));
        QCOMPARE(queue.at(3).source, QStringLiteral("s1"));
        QCOMPARE(queue.at(4).source, QStringLiteral("s2"));

        const auto n1 = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(n1.has_value());
        QCOMPARE(n1->source, QStringLiteral("nextA"));

        const auto n2 = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(n2.has_value());
        QCOMPARE(n2->source, QStringLiteral("nextB"));

        const auto n3 = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(n3.has_value());
        QCOMPARE(n3->source, QStringLiteral("s1"));
    }

    // 3. Shuffle mode across multiple seeds
    for (const quint64 seed : { 1ULL, 42ULL, 987654ULL }) {
        PlayQueue queue(seed);
        auto *tester = new QAbstractItemModelTester(
            &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
        Q_UNUSED(tester);

        queue.setMode(PlayMode::Shuffle);
        queue.setItems({ { .source = QStringLiteral("orig0") },
            { .source = QStringLiteral("orig1") }, { .source = QStringLiteral("orig2") },
            { .source = QStringLiteral("orig3") }, { .source = QStringLiteral("orig4") } });

        // Advance 2 songs
        const auto s0 = queue.advance(PlayOrder::Advance::Auto);
        const auto s1 = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(s0.has_value());
        QVERIFY(s1.has_value());

        // Insert next two songs
        queue.insertNext(
            { { .source = QStringLiteral("nextA") }, { .source = QStringLiteral("nextB") } });

        // Next two advances must strictly return nextA then nextB
        const auto advA = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(advA.has_value());
        QCOMPARE(advA->source, QStringLiteral("nextA"));

        const auto advB = queue.advance(PlayOrder::Advance::Auto);
        QVERIFY(advB.has_value());
        QCOMPARE(advB->source, QStringLiteral("nextB"));

        // Remaining 3 advances must visit the 3 remaining original songs
        std::set<QString> allVisited;
        allVisited.insert(s0->source);
        allVisited.insert(s1->source);
        allVisited.insert(advA->source);
        allVisited.insert(advB->source);

        for (int i = 0; i < 3; ++i) {
            const auto adv = queue.advance(PlayOrder::Advance::Auto);
            QVERIFY(adv.has_value());
            allVisited.insert(adv->source);
        }

        QCOMPARE(allVisited.size(), 7UL);
        QVERIFY(allVisited.contains(QStringLiteral("orig0")));
        QVERIFY(allVisited.contains(QStringLiteral("orig1")));
        QVERIFY(allVisited.contains(QStringLiteral("orig2")));
        QVERIFY(allVisited.contains(QStringLiteral("orig3")));
        QVERIFY(allVisited.contains(QStringLiteral("orig4")));
        QVERIFY(allVisited.contains(QStringLiteral("nextA")));
        QVERIFY(allVisited.contains(QStringLiteral("nextB")));
    }
}

void TstPlayQueue::navigationBehavior()
{
    PlayQueue queue(42);
    auto *tester = new QAbstractItemModelTester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    queue.setItems({ { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") },
        { .source = QStringLiteral("s2") }, { .source = QStringLiteral("s3") } });

    // 1. peekNext matches advance
    const auto peek0 = queue.peekNext(PlayOrder::Advance::Auto);
    const auto adv0 = queue.advance(PlayOrder::Advance::Auto);
    QCOMPARE(peek0, adv0);
    QCOMPARE(adv0, std::optional<QueueItem>(queue.at(queue.currentIndex())));
    QCOMPARE(queue.currentItem(), adv0);

    const auto peek1 = queue.peekNext(PlayOrder::Advance::Auto);
    const auto adv1 = queue.advance(PlayOrder::Advance::Auto);
    QCOMPARE(peek1, adv1);
    QCOMPARE(adv1, std::optional<QueueItem>(queue.at(queue.currentIndex())));

    // 2. previous() matches at(currentIndex())
    const auto prev0 = queue.previous();
    QCOMPARE(prev0, std::optional<QueueItem>(queue.at(queue.currentIndex())));
    QCOMPARE(queue.currentIndex(), 0);

    // 3. jumpTo
    const auto jumped = queue.jumpTo(3);
    QVERIFY(jumped.has_value());
    QCOMPARE(jumped->source, QStringLiteral("s3"));
    QCOMPARE(queue.currentIndex(), 3);

    // Out of bound jumpTo returns nullopt and leaves index intact
    QCOMPARE(queue.jumpTo(-1), std::optional<QueueItem>(std::nullopt));
    QCOMPARE(queue.jumpTo(10), std::optional<QueueItem>(std::nullopt));
    QCOMPARE(queue.currentIndex(), 3);
}

void TstPlayQueue::upcomingChangedAndModeChangedSignals()
{
    PlayQueue queue(42);
    auto *tester = new QAbstractItemModelTester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    QSignalSpy upcomingSpy(&queue, &PlayQueue::upcomingChanged);
    QSignalSpy modeSpy(&queue, &PlayQueue::modeChanged);
    QSignalSpy countSpy(&queue, &PlayQueue::countChanged);

    // 1. setItems
    queue.setItems({ { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") } });
    QVERIFY(upcomingSpy.count() >= 1);
    QCOMPARE(countSpy.count(), 1);
    upcomingSpy.clear();
    countSpy.clear();

    // 2. append
    queue.append({ { .source = QStringLiteral("s2") } });
    QVERIFY(upcomingSpy.count() >= 1);
    QCOMPARE(countSpy.count(), 1);
    upcomingSpy.clear();
    countSpy.clear();

    // 3. insert
    queue.insert(0, { { .source = QStringLiteral("s-pre") } });
    QVERIFY(upcomingSpy.count() >= 1);
    QCOMPARE(countSpy.count(), 1);
    upcomingSpy.clear();
    countSpy.clear();

    // 4. insertNext
    queue.insertNext({ { .source = QStringLiteral("s-next") } });
    QVERIFY(upcomingSpy.count() >= 1);
    QCOMPARE(countSpy.count(), 1);
    upcomingSpy.clear();
    countSpy.clear();

    // 5. move
    queue.move(0, 2);
    QVERIFY(upcomingSpy.count() >= 1);
    upcomingSpy.clear();

    // 6. remove
    queue.remove(0, 1);
    QVERIFY(upcomingSpy.count() >= 1);
    QCOMPARE(countSpy.count(), 1);
    upcomingSpy.clear();
    countSpy.clear();

    // 7. mode switch
    queue.setMode(PlayMode::RepeatAll);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.takeFirst().at(0).value<PlayMode>(), PlayMode::RepeatAll);
    QVERIFY(upcomingSpy.count() >= 1);
    upcomingSpy.clear();

    // Same mode does not re-emit
    queue.setMode(PlayMode::RepeatAll);
    QCOMPARE(modeSpy.count(), 0);
    QCOMPARE(upcomingSpy.count(), 0);

    // 8. navigation
    queue.advance(PlayOrder::Advance::Auto);
    QVERIFY(upcomingSpy.count() >= 1);
    upcomingSpy.clear();

    queue.previous();
    QVERIFY(upcomingSpy.count() >= 1);
    upcomingSpy.clear();

    queue.jumpTo(1);
    QVERIFY(upcomingSpy.count() >= 1);
    upcomingSpy.clear();

    // 9. clear
    queue.clear();
    QVERIFY(upcomingSpy.count() >= 1);
    QCOMPARE(countSpy.count(), 1);
}

void TstPlayQueue::isCurrentRoleDataChanged()
{
    PlayQueue queue(42);
    auto *tester = new QAbstractItemModelTester(
        &queue, QAbstractItemModelTester::FailureReportingMode::QtTest, &queue);
    Q_UNUSED(tester);

    queue.setItems({ { .source = QStringLiteral("s0") }, { .source = QStringLiteral("s1") },
        { .source = QStringLiteral("s2") }, { .source = QStringLiteral("s3") } });

    QSignalSpy dataSpy(&queue, &PlayQueue::dataChanged);

    // advance from -1 to 0: dataChanged for row 0
    queue.advance(PlayOrder::Advance::Auto);
    QVERIFY(dataSpy.count() >= 1);
    bool row0Notified = false;
    for (int i = 0; i < dataSpy.count(); ++i) {
        const auto topLeft = dataSpy.at(i).at(0).value<QModelIndex>();
        const auto roles = dataSpy.at(i).at(2).value<QList<int>>();
        if (topLeft.row() == 0 && roles.contains(PlayQueue::IsCurrentRole)) {
            row0Notified = true;
        }
    }
    QVERIFY(row0Notified);

    // advance from 0 to 1: dataChanged for row 0 (old) and row 1 (new)
    dataSpy.clear();
    queue.advance(PlayOrder::Advance::Auto);
    row0Notified = false;
    bool row1Notified = false;
    for (int i = 0; i < dataSpy.count(); ++i) {
        const auto topLeft = dataSpy.at(i).at(0).value<QModelIndex>();
        const auto roles = dataSpy.at(i).at(2).value<QList<int>>();
        if (topLeft.row() == 0 && roles.contains(PlayQueue::IsCurrentRole)) {
            row0Notified = true;
        }
        if (topLeft.row() == 1 && roles.contains(PlayQueue::IsCurrentRole)) {
            row1Notified = true;
        }
    }
    QVERIFY(row0Notified);
    QVERIFY(row1Notified);

    // jumpTo from 1 to 3: dataChanged for row 1 (old) and row 3 (new)
    dataSpy.clear();
    queue.jumpTo(3);
    row1Notified = false;
    bool row3Notified = false;
    for (int i = 0; i < dataSpy.count(); ++i) {
        const auto topLeft = dataSpy.at(i).at(0).value<QModelIndex>();
        const auto roles = dataSpy.at(i).at(2).value<QList<int>>();
        if (topLeft.row() == 1 && roles.contains(PlayQueue::IsCurrentRole)) {
            row1Notified = true;
        }
        if (topLeft.row() == 3 && roles.contains(PlayQueue::IsCurrentRole)) {
            row3Notified = true;
        }
    }
    QVERIFY(row1Notified);
    QVERIFY(row3Notified);
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayQueue)

#include "tst_PlayQueue.moc"
