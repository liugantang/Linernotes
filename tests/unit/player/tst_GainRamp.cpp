// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QTest>

#include <player/GainRamp.h>

#include <cmath>

using linernotes::player::GainRamp;

namespace {

class TstGainRamp : public QObject {
    Q_OBJECT

private slots:
    void defaultConstructedStaysAtOne();
    void endpointValues();
    void immediateCompletionWhenDurationNonPositive();
    void monotonicityRising();
    void monotonicityFalling();
    void zeroSlopeAtEndpoints();
    void valuesClampedToRange();
    void elapsedNegativeOrExceeded();
};

void TstGainRamp::defaultConstructedStaysAtOne()
{
    GainRamp ramp;
    QCOMPARE(ramp.valueAt(0), 1.0);
    QCOMPARE(ramp.valueAt(100), 1.0);
    QCOMPARE(ramp.valueAt(-50), 1.0);
    QCOMPARE(ramp.target(), 1.0);
    QVERIFY(ramp.isFinishedAt(0));
    QVERIFY(ramp.isFinishedAt(100));
}

void TstGainRamp::endpointValues()
{
    GainRamp ramp(0.2, 0.8, 1000);
    QCOMPARE(ramp.valueAt(0), 0.2);
    QCOMPARE(ramp.valueAt(1000), 0.8);
    QCOMPARE(ramp.target(), 0.8);
    QVERIFY(!ramp.isFinishedAt(0));
    QVERIFY(!ramp.isFinishedAt(500));
    QVERIFY(!ramp.isFinishedAt(999));
    QVERIFY(ramp.isFinishedAt(1000));
    QVERIFY(ramp.isFinishedAt(1500));
}

void TstGainRamp::immediateCompletionWhenDurationNonPositive()
{
    // durationMs == 0
    GainRamp rampZero(0.3, 0.7, 0);
    QCOMPARE(rampZero.valueAt(0), 0.7);
    QCOMPARE(rampZero.valueAt(-10), 0.7);
    QCOMPARE(rampZero.valueAt(100), 0.7);
    QCOMPARE(rampZero.target(), 0.7);
    QVERIFY(rampZero.isFinishedAt(0));
    QVERIFY(rampZero.isFinishedAt(-10));
    QVERIFY(rampZero.isFinishedAt(100));

    // durationMs < 0
    GainRamp rampNeg(0.4, 0.9, -100);
    QCOMPARE(rampNeg.valueAt(0), 0.9);
    QCOMPARE(rampNeg.valueAt(-50), 0.9);
    QCOMPARE(rampNeg.valueAt(50), 0.9);
    QCOMPARE(rampNeg.target(), 0.9);
    QVERIFY(rampNeg.isFinishedAt(0));
}

void TstGainRamp::monotonicityRising()
{
    GainRamp ramp(0.1, 0.9, 1000);
    double prev = ramp.valueAt(0);

    for (int t = 10; t <= 1000; t += 10) {
        const double curr = ramp.valueAt(t);
        QVERIFY2(curr >= prev, "Rising ramp must be monotonically non-decreasing");
        prev = curr;
    }
}

void TstGainRamp::monotonicityFalling()
{
    GainRamp ramp(0.9, 0.1, 1000);
    double prev = ramp.valueAt(0);

    for (int t = 10; t <= 1000; t += 10) {
        const double curr = ramp.valueAt(t);
        QVERIFY2(curr <= prev, "Falling ramp must be monotonically non-increasing");
        prev = curr;
    }
}

void TstGainRamp::zeroSlopeAtEndpoints()
{
    GainRamp ramp(0.0, 1.0, 1000);

    // Delta near t = 0 (first 2% = 20ms)
    const double diffStart = ramp.valueAt(20) - ramp.valueAt(0);
    // Delta near midpoint (490ms to 510ms, centered at 500ms, width 20ms)
    const double diffMid = ramp.valueAt(510) - ramp.valueAt(490);
    // Delta near t = 1000 (last 2% = 20ms)
    const double diffEnd = ramp.valueAt(1000) - ramp.valueAt(980);

    QVERIFY(diffStart >= 0.0);
    QVERIFY(diffEnd >= 0.0);
    QVERIFY(diffMid > 0.0);

    // Smoothstep has derivative 0 at t=0 and t=1, so slope near ends is much flatter than at mid.
    QVERIFY2(
        diffStart < diffMid * 0.1, "Slope near start must be significantly smaller than midpoint");
    QVERIFY2(diffEnd < diffMid * 0.1, "Slope near end must be significantly smaller than midpoint");
}

void TstGainRamp::valuesClampedToRange()
{
    // Out-of-range input clamping
    GainRamp rampOutOfRange(-0.5, 1.5, 1000);
    QCOMPARE(rampOutOfRange.valueAt(0), 0.0);
    QCOMPARE(rampOutOfRange.valueAt(1000), 1.0);
    QCOMPARE(rampOutOfRange.target(), 1.0);

    for (int t = -100; t <= 1100; t += 25) {
        const double v = rampOutOfRange.valueAt(t);
        QVERIFY(v >= 0.0 && v <= 1.0);
    }
}

void TstGainRamp::elapsedNegativeOrExceeded()
{
    GainRamp ramp(0.25, 0.75, 400);

    // Negative elapsed time
    QCOMPARE(ramp.valueAt(-1000), 0.25);
    QCOMPARE(ramp.valueAt(-1), 0.25);
    QVERIFY(!ramp.isFinishedAt(-1000));
    QVERIFY(!ramp.isFinishedAt(-1));

    // Exceeded elapsed time
    QCOMPARE(ramp.valueAt(400), 0.75);
    QCOMPARE(ramp.valueAt(401), 0.75);
    QCOMPARE(ramp.valueAt(10000), 0.75);
    QVERIFY(ramp.isFinishedAt(400));
    QVERIFY(ramp.isFinishedAt(401));
    QVERIFY(ramp.isFinishedAt(10000));
}

} // namespace

QTEST_MAIN(TstGainRamp)
#include "tst_GainRamp.moc"
