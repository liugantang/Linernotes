// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <player/ListenSession.h>

namespace {

using linernotes::player::ListenEnd;
using linernotes::player::ListenSession;

class TstListenSession : public QObject {
    Q_OBJECT

private slots:
    void continuousPlaybackAndCompletion();
    void seeksForwardAndBackward();
    void pausePeriods();
    void switchedVsStoppedSkipped();
    void ignorePositionZero();
};

void TstListenSession::continuousPlaybackAndCompletion()
{
    ListenSession session(100000); // 100s duration
    QVERIFY(!session.hasStarted());

    session.onPlaying(true, 1000);
    QVERIFY(session.hasStarted());

    session.onPosition(1000, 1000);
    session.onPosition(5000, 5000);
    session.onPosition(97500, 97500);

    const auto res = session.result(ListenEnd::Stopped, 98000);
    QCOMPARE(res.startedAtMs, 1000);
    QCOMPARE(res.endedAtMs, 98000);
    QCOMPARE(res.playedMs, 96500);
    QCOMPARE(res.durationMs, 100000);
    QCOMPARE(res.completed, true);
    QCOMPARE(res.skipped, false);
    QCOMPARE(res.pausedMs, 0);
}

void TstListenSession::seeksForwardAndBackward()
{
    ListenSession session(200000);
    session.onPlaying(true, 1000);
    session.onPosition(1000, 1000);
    session.onPosition(3000, 3000); // playedMs = 2000

    // Forward seek: position jumps by 47000ms while wall clock advances only 100ms
    session.onPosition(50000, 3100);
    QCOMPARE(session.result(ListenEnd::Stopped, 3100).playedMs, 2000);

    // Play continuously after forward seek
    session.onPosition(54000, 7100); // playedMs += 4000 -> 6000
    QCOMPARE(session.result(ListenEnd::Stopped, 7100).playedMs, 6000);

    // Backward seek: position jumps back to 10000ms
    session.onPosition(10000, 7200);
    QCOMPARE(session.result(ListenEnd::Stopped, 7200).playedMs, 6000);

    // Replay after backward seek
    session.onPosition(15000, 12200); // playedMs += 5000 -> 11000
    QCOMPARE(session.result(ListenEnd::Stopped, 12200).playedMs, 11000);
}

void TstListenSession::pausePeriods()
{
    ListenSession session(120000);

    // Pause before session start is ignored in pausedMs
    session.onPlaying(false, 500);
    QVERIFY(!session.hasStarted());
    QCOMPARE(session.result(ListenEnd::Stopped, 800).pausedMs, 0);

    // Start playing
    session.onPlaying(true, 1000);
    session.onPosition(1000, 1000);
    session.onPosition(6000, 6000); // playedMs = 5000

    // Pause
    session.onPlaying(false, 6000);

    // While paused, duration advances in wall clock
    const auto resMidPause = session.result(ListenEnd::Stopped, 10000);
    QCOMPARE(resMidPause.playedMs, 5000);
    QCOMPARE(resMidPause.pausedMs, 4000);

    // Resume at 11000
    session.onPlaying(true, 11000);

    // Play from 6000 to 10000
    session.onPosition(10000, 15000); // deltaPos = 4000, deltaWall = 4000 -> playedMs = 9000
    const auto resAfterResume = session.result(ListenEnd::Stopped, 15000);
    QCOMPARE(resAfterResume.playedMs, 9000);
    QCOMPARE(resAfterResume.pausedMs, 5000);
}

void TstListenSession::switchedVsStoppedSkipped()
{
    ListenSession session(180000);
    session.onPlaying(true, 1000);
    session.onPosition(1000, 1000);
    session.onPosition(30000, 30000);

    // 1) Incomplete + Switched -> skipped = true, skipPositionMs = 30000
    const auto resSwitched = session.result(ListenEnd::Switched, 30000);
    QCOMPARE(resSwitched.completed, false);
    QCOMPARE(resSwitched.skipped, true);
    QCOMPARE(resSwitched.skipPositionMs, std::optional<qint64>(30000));

    // 2) Incomplete + Stopped -> skipped = false, skipPositionMs = nullopt
    const auto resStopped = session.result(ListenEnd::Stopped, 30000);
    QCOMPARE(resStopped.completed, false);
    QCOMPARE(resStopped.skipped, false);
    QCOMPARE(resStopped.skipPositionMs, std::nullopt);

    // 3) Completed + Switched -> completed = true, skipped = false
    session.onPosition(178000, 178000); // 178000 >= 180000 - 3000
    const auto resCompletedSwitched = session.result(ListenEnd::Switched, 178000);
    QCOMPARE(resCompletedSwitched.completed, true);
    QCOMPARE(resCompletedSwitched.skipped, false);
    QCOMPARE(resCompletedSwitched.skipPositionMs, std::nullopt);
}

void TstListenSession::ignorePositionZero()
{
    ListenSession session(60000);
    session.onPlaying(true, 1000);

    // Position 0 is ignored
    session.onPosition(0, 1000);
    session.onPosition(5000, 5000);
    session.onPosition(0, 7000);
    session.onPosition(8000, 8000);

    const auto res = session.result(ListenEnd::Stopped, 8000);
    QCOMPARE(res.playedMs, 3000);

    // Completed position
    session.onPosition(58000, 58000);
    session.onPosition(0, 59000); // Position 0 should not overwrite completed position

    const auto resFinal = session.result(ListenEnd::Stopped, 59000);
    QCOMPARE(resFinal.completed, true);
}

} // namespace

QTEST_GUILESS_MAIN(TstListenSession)
#include "tst_ListenSession.moc"
