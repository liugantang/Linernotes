// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QTest>

#include <ai/RequestScheduler.h>
#include <common/ManualClock.h>

using linernotes::ai::RequestScheduler;
using linernotes::test::ManualClock;

namespace {

class TstRequestScheduler : public QObject {
    Q_OBJECT

private slots:
    void maxConcurrentLimitsInFlightAndReleasesOnTicketDestroy();
    void destroyQueuedTicketCancelsWithoutBlockingOthers();
    void rateLimitingRequestsPerMinuteWithClockAdvance();
    void pauseServiceBlocksAndResumesAfterClockAdvance();
    void independentServicesDoNotBlockEachOther();
    void dynamicConcurrencyUpdateOnSubsequentAcquire();
};

void TstRequestScheduler::maxConcurrentLimitsInFlightAndReleasesOnTicketDestroy()
{
    ManualClock clock(1000);
    RequestScheduler scheduler(clock);

    int grantedCount = 0;
    bool g1 = false;
    bool g2 = false;
    bool g3 = false;

    auto t1 = scheduler.acquire(QStringLiteral("svc"), 2, 0, [&]() {
        g1 = true;
        grantedCount++;
    });
    auto t2 = scheduler.acquire(QStringLiteral("svc"), 2, 0, [&]() {
        g2 = true;
        grantedCount++;
    });
    auto t3 = scheduler.acquire(QStringLiteral("svc"), 2, 0, [&]() {
        g3 = true;
        grantedCount++;
    });

    QTRY_COMPARE(grantedCount, 2);
    QVERIFY(g1);
    QVERIFY(g2);
    QVERIFY(!g3);

    // Destroy first ticket -> third ticket should be granted
    t1.reset();

    QTRY_COMPARE(grantedCount, 3);
    QVERIFY(g3);
}

void TstRequestScheduler::destroyQueuedTicketCancelsWithoutBlockingOthers()
{
    ManualClock clock(1000);
    RequestScheduler scheduler(clock);

    bool g1 = false;
    bool g2 = false;
    bool g3 = false;

    auto t1 = scheduler.acquire(QStringLiteral("svc"), 1, 0, [&]() { g1 = true; });
    auto t2 = scheduler.acquire(QStringLiteral("svc"), 1, 0, [&]() { g2 = true; });
    auto t3 = scheduler.acquire(QStringLiteral("svc"), 1, 0, [&]() { g3 = true; });

    QTRY_VERIFY(g1);
    QVERIFY(!g2);
    QVERIFY(!g3);

    // Destroy queued ticket t2
    t2.reset();

    // Release t1 -> t3 should be granted, t2 never granted
    t1.reset();

    QTRY_VERIFY(g3);
    QVERIFY(!g2);
}

void TstRequestScheduler::rateLimitingRequestsPerMinuteWithClockAdvance()
{
    ManualClock clock(1000);
    RequestScheduler scheduler(clock);

    bool g1 = false;
    bool g2 = false;
    bool g3 = false;

    auto t1 = scheduler.acquire(QStringLiteral("svc"), 10, 2, [&]() { g1 = true; });
    auto t2 = scheduler.acquire(QStringLiteral("svc"), 10, 2, [&]() { g2 = true; });
    auto t3 = scheduler.acquire(QStringLiteral("svc"), 10, 2, [&]() { g3 = true; });

    // First two are granted immediately
    QTRY_VERIFY(g1);
    QTRY_VERIFY(g2);
    QVERIFY(!g3);

    // Advance clock by 30 seconds (60000 / 2 = 30000 ms needed for 1 token)
    clock.advance(30000);

    // Third ticket should now be granted
    QTRY_VERIFY(g3);
}

void TstRequestScheduler::pauseServiceBlocksAndResumesAfterClockAdvance()
{
    ManualClock clock(1000);
    RequestScheduler scheduler(clock);

    scheduler.pauseService(QStringLiteral("svc"), 5000);

    bool g1 = false;
    auto t1 = scheduler.acquire(QStringLiteral("svc"), 2, 0, [&]() { g1 = true; });

    QCoreApplication::processEvents();
    QVERIFY(!g1);

    // Advance clock past the pause duration
    clock.advance(5000);

    QTRY_VERIFY(g1);
}

void TstRequestScheduler::independentServicesDoNotBlockEachOther()
{
    ManualClock clock(1000);
    RequestScheduler scheduler(clock);

    bool gA1 = false;
    bool gA2 = false;
    bool gB1 = false;

    // Service A has concurrency 1, acquires 2 tickets
    auto tA1 = scheduler.acquire(QStringLiteral("svcA"), 1, 0, [&]() { gA1 = true; });
    auto tA2 = scheduler.acquire(QStringLiteral("svcA"), 1, 0, [&]() { gA2 = true; });

    // Service B has concurrency 1, acquires 1 ticket
    auto tB1 = scheduler.acquire(QStringLiteral("svcB"), 1, 0, [&]() { gB1 = true; });

    QTRY_VERIFY(gA1);
    QTRY_VERIFY(gB1);
    QVERIFY(!gA2);

    tA1.reset();
    QTRY_VERIFY(gA2);
}

void TstRequestScheduler::dynamicConcurrencyUpdateOnSubsequentAcquire()
{
    ManualClock clock(1000);
    RequestScheduler scheduler(clock);

    bool g1 = false;
    bool g2 = false;

    // First acquire with maxConcurrent=1
    auto t1 = scheduler.acquire(QStringLiteral("svc"), 1, 0, [&]() { g1 = true; });
    QTRY_VERIFY(g1);

    // Second acquire increases maxConcurrent to 2 -> should be granted immediately
    auto t2 = scheduler.acquire(QStringLiteral("svc"), 2, 0, [&]() { g2 = true; });
    QTRY_VERIFY(g2);
}

} // namespace

QTEST_GUILESS_MAIN(TstRequestScheduler)

#include "tst_RequestScheduler.moc"
