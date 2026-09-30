// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QTest>

#include <audio/Fingerprint.h>
#include <butler/DuplicateFinder.h>

namespace {

using linernotes::audio::RawFingerprint;
using linernotes::butler::candidateClusters;
using linernotes::butler::DuplicateKind;
using linernotes::butler::DupTrack;
using linernotes::butler::findDuplicates;
using linernotes::butler::KeepFactors;
using linernotes::butler::keepScore;

class TstDuplicateFinder : public QObject {
    Q_OBJECT

private slots:
    void clustersCandidatesByWorkVersionAndDuration();
    void exactAndSameRecordingDuplicates();
    void dissimilarFingerprintsDoNotGroup();
    void missingFingerprintFormsSuspectGroup();
    void keepScoreCalculationAndRecommended();
};

void TstDuplicateFinder::clustersCandidatesByWorkVersionAndDuration()
{
    const QList<DupTrack> tracks = {
        { .trackId = 1,
            .workId = 10,
            .versionType = QStringLiteral("studio"),
            .durationMs = 200000 },
        { .trackId = 2,
            .workId = 10,
            .versionType = QStringLiteral("studio"),
            .durationMs = 201500 },
        { .trackId = 3,
            .workId = 10,
            .versionType = QStringLiteral("studio"),
            .durationMs = 230000 },
        { .trackId = 4,
            .workId = 20,
            .versionType = QStringLiteral("studio"),
            .durationMs = 200000 },
        { .trackId = 5,
            .workId = 0,
            .versionType = QStringLiteral("studio"),
            .durationMs = 200000 },
    };

    const auto clusters = candidateClusters(tracks);
    QCOMPARE(clusters.size(), 1);
    QCOMPARE(clusters.first(), (QList<qint64> { 1, 2 }));
}

void TstDuplicateFinder::exactAndSameRecordingDuplicates()
{
    QList<quint32> itemsA;
    itemsA.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        itemsA.append(0x12345678U ^ (i * 0x9e3779b9U));
    }
    const RawFingerprint fpA { .algorithm = 1, .items = itemsA };

    // Two tracks with same content_hash -> Exact group
    DupTrack t1 {
        .trackId = 1,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("hash_exact"),
        .fingerprint = fpA,
        .keepScore = 100.0,
    };
    DupTrack t2 {
        .trackId = 2,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("hash_exact"),
        .fingerprint = fpA,
        .keepScore = 150.0,
    };

    const auto exactGroups = findDuplicates({ t1, t2 });
    QCOMPARE(exactGroups.size(), 1);
    QCOMPARE(exactGroups.first().kind, DuplicateKind::Exact);
    QCOMPARE(exactGroups.first().trackIds, (QList<qint64> { 1, 2 }));
    QCOMPARE(exactGroups.first().recommendedTrackId, 2);

    // Add 3rd track with different content_hash but same fingerprint -> SameRecording group
    DupTrack t3 {
        .trackId = 3,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200500,
        .contentHash = QStringLiteral("hash_diff"),
        .fingerprint = fpA,
        .keepScore = 200.0,
    };

    const auto sameRecGroups = findDuplicates({ t1, t2, t3 });
    QCOMPARE(sameRecGroups.size(), 1);
    QCOMPARE(sameRecGroups.first().kind, DuplicateKind::SameRecording);
    QCOMPARE(sameRecGroups.first().trackIds, (QList<qint64> { 1, 2, 3 }));
    QCOMPARE(sameRecGroups.first().recommendedTrackId, 3);
}

void TstDuplicateFinder::dissimilarFingerprintsDoNotGroup()
{
    QList<quint32> itemsA;
    itemsA.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        itemsA.append(0x12345678U ^ (i * 0x9e3779b9U));
    }
    const RawFingerprint fpA { .algorithm = 1, .items = itemsA };

    QList<quint32> itemsB;
    itemsB.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        itemsB.append(0xabcdef01U ^ (i * 0x55555555U));
    }
    const RawFingerprint fpB { .algorithm = 1, .items = itemsB };

    const DupTrack t1 {
        .trackId = 1,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("hash_1"),
        .fingerprint = fpA,
        .keepScore = 100.0,
    };
    const DupTrack t2 {
        .trackId = 2,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 201000,
        .contentHash = QStringLiteral("hash_2"),
        .fingerprint = fpB,
        .keepScore = 100.0,
    };

    const auto groups = findDuplicates({ t1, t2 });
    QVERIFY(groups.isEmpty());
}

void TstDuplicateFinder::missingFingerprintFormsSuspectGroup()
{
    QList<quint32> itemsA;
    itemsA.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        itemsA.append(0x12345678U ^ (i * 0x9e3779b9U));
    }
    const RawFingerprint fpA { .algorithm = 1, .items = itemsA };

    const DupTrack t1 {
        .trackId = 1,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("hash_1"),
        .fingerprint = fpA,
        .keepScore = 100.0,
    };
    const DupTrack t2 {
        .trackId = 2,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 201000,
        .contentHash = QStringLiteral("hash_2"),
        .fingerprint = std::nullopt,
        .keepScore = 120.0,
    };

    const auto groups = findDuplicates({ t1, t2 });
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().kind, DuplicateKind::Suspect);
    QCOMPARE(groups.first().trackIds, (QList<qint64> { 1, 2 }));
    QCOMPARE(groups.first().recommendedTrackId, 2);
}

void TstDuplicateFinder::keepScoreCalculationAndRecommended()
{
    const KeepFactors fFlac2496 {
        .codec = QStringLiteral("flac"),
        .sampleRate = 96000,
        .bitDepth = 24,
        .bitrate = 0,
        .filledTagFields = 7,
        .hasCover = true,
    };
    const KeepFactors fFlac1644 {
        .codec = QStringLiteral("flac"),
        .sampleRate = 44100,
        .bitDepth = 16,
        .bitrate = 0,
        .filledTagFields = 7,
        .hasCover = true,
    };
    const KeepFactors fMp3320 {
        .codec = QStringLiteral("mp3"),
        .sampleRate = 44100,
        .bitDepth = 0,
        .bitrate = 320,
        .filledTagFields = 7,
        .hasCover = true,
    };

    const double score1 = keepScore(fFlac2496);
    const double score2 = keepScore(fFlac1644);
    const double score3 = keepScore(fMp3320);

    QVERIFY(score1 > score2);
    QVERIFY(score2 > score3);

    const DupTrack t1 {
        .trackId = 1,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("h_exact"),
        .fingerprint = std::nullopt,
        .keepScore = score1,
    };
    const DupTrack t2 {
        .trackId = 2,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("h_exact"),
        .fingerprint = std::nullopt,
        .keepScore = score2,
    };
    const DupTrack t3 {
        .trackId = 3,
        .workId = 10,
        .versionType = QStringLiteral("studio"),
        .durationMs = 200000,
        .contentHash = QStringLiteral("h_exact"),
        .fingerprint = std::nullopt,
        .keepScore = score3,
    };

    const auto groups = findDuplicates({ t1, t2, t3 });
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().recommendedTrackId, 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstDuplicateFinder)

#include "tst_DuplicateFinder.moc"
