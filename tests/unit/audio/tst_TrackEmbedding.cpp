// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <audio/TrackEmbedding.h>

namespace {

using linernotes::audio::embedWindowStartsMs;
using linernotes::audio::kEmbedWindowMs;

class TstTrackEmbedding : public QObject {
    Q_OBJECT

private slots:
    void windowStartsForLongTrack();
    void windowStartsForShortTrack();
    void windowStartsForMediumTrackClamped();
};

void TstTrackEmbedding::windowStartsForLongTrack()
{
    const qint64 durationMs = 300'000;
    const auto starts = embedWindowStartsMs(durationMs);
    QCOMPARE(starts.size(), 3);
    QCOMPARE(starts.at(0), 56'500);
    QCOMPARE(starts.at(1), 146'500);
    QCOMPARE(starts.at(2), 236'500);
}

void TstTrackEmbedding::windowStartsForShortTrack()
{
    // Exactly 7s
    const auto starts7s = embedWindowStartsMs(kEmbedWindowMs);
    QCOMPARE(starts7s, (QList<qint64> { 0 }));

    // Shorter than 7s
    const auto starts5s = embedWindowStartsMs(5000);
    QCOMPARE(starts5s, (QList<qint64> { 0 }));

    // 0 or negative
    const auto starts0s = embedWindowStartsMs(0);
    QCOMPARE(starts0s, (QList<qint64> { 0 }));
}

void TstTrackEmbedding::windowStartsForMediumTrackClamped()
{
    // 10s track (between 7s and 21s)
    const qint64 durationMs = 10'000;
    const auto starts = embedWindowStartsMs(durationMs);
    QCOMPARE(starts.size(), 3);
    const qint64 maxStart = durationMs - kEmbedWindowMs; // 3000

    // 20% center = 2000, start = 2000 - 3500 = -1500 -> clamped to 0
    QCOMPARE(starts.at(0), 0);
    // 50% center = 5000, start = 5000 - 3500 = 1500 -> 1500
    QCOMPARE(starts.at(1), 1500);
    // 80% center = 8000, start = 8000 - 3500 = 4500 -> clamped to 3000
    QCOMPARE(starts.at(2), maxStart);

    for (const qint64 s : starts) {
        QVERIFY(s >= 0);
        QVERIFY(s <= maxStart);
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstTrackEmbedding)

#include "tst_TrackEmbedding.moc"
