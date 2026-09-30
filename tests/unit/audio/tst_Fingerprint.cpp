// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTest>

#include <audio/Fingerprint.h>
#include <common/TestSupport.h>

namespace {

using linernotes::audio::Fingerprinter;
using linernotes::audio::RawFingerprint;
using linernotes::test::fixturePath;

class TstFingerprint : public QObject {
    Q_OBJECT

private slots:
    void computesCrossFormatSimilarity();
    void testsSimilarityLogic();
    void testsBlobRoundtrip();
};

void TstFingerprint::computesCrossFormatSimilarity()
{
    const QString melodyFlac = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString melodyOgg = fixturePath(QStringLiteral("audio/melody_8s.ogg"));
    const QString otherFlac = fixturePath(QStringLiteral("audio/other_8s.flac"));

    const auto fpMelodyFlacRes = Fingerprinter::computeFile(melodyFlac);
    const auto fpMelodyOggRes = Fingerprinter::computeFile(melodyOgg);
    const auto fpOtherFlacRes = Fingerprinter::computeFile(otherFlac);

    QVERIFY(fpMelodyFlacRes.ok());
    QVERIFY(fpMelodyOggRes.ok());
    QVERIFY(fpOtherFlacRes.ok());

    const double sameMelodySim
        = linernotes::audio::fingerprintSimilarity(fpMelodyFlacRes.value(), fpMelodyOggRes.value());
    QVERIFY(sameMelodySim >= 0.9);

    const double diffMelodySim
        = linernotes::audio::fingerprintSimilarity(fpMelodyFlacRes.value(), fpOtherFlacRes.value());
    QVERIFY(diffMelodySim < 0.8);
}

void TstFingerprint::testsSimilarityLogic()
{
    QList<quint32> items;
    items.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        items.append(0x12345678U ^ (i * 0x9e3779b9U));
    }

    const RawFingerprint fpA { .algorithm = 1, .items = items };
    const RawFingerprint fpB { .algorithm = 1, .items = items };
    QCOMPARE(linernotes::audio::fingerprintSimilarity(fpA, fpB), 1.0);

    QList<quint32> shiftedItems;
    shiftedItems.reserve(55);
    for (quint32 i = 0; i < 5; ++i) {
        shiftedItems.append(0xdeadbeefU ^ (i * 0x1337U));
    }
    shiftedItems.append(items);

    const RawFingerprint fpBShifted { .algorithm = 1, .items = shiftedItems };
    QCOMPARE(linernotes::audio::fingerprintSimilarity(fpA, fpBShifted), 1.0);

    const RawFingerprint fpEmpty { .algorithm = 1, .items = { } };
    QCOMPARE(linernotes::audio::fingerprintSimilarity(fpA, fpEmpty), 0.0);
    QCOMPARE(linernotes::audio::fingerprintSimilarity(fpEmpty, fpA), 0.0);

    const RawFingerprint fpDiffAlgo { .algorithm = 2, .items = items };
    QCOMPARE(linernotes::audio::fingerprintSimilarity(fpA, fpDiffAlgo), 0.0);
}

void TstFingerprint::testsBlobRoundtrip()
{
    const QList<quint32> items { 0x11223344U, 0x55667788U, 0x99aabbccU, 0xddeeff00U };
    const RawFingerprint fp { .algorithm = 1, .items = items };

    const QByteArray blob = linernotes::audio::toBlob(fp);
    QCOMPARE(blob.size(), 16);

    const auto decodedOpt = linernotes::audio::itemsFromBlob(blob);
    QVERIFY(decodedOpt.has_value());
    if (!decodedOpt.has_value()) {
        return;
    }
    QCOMPARE(*decodedOpt, items);

    QByteArray invalidBlob = blob;
    invalidBlob.append(static_cast<char>(0x7a));
    const auto invalidDecoded = linernotes::audio::itemsFromBlob(invalidBlob);
    QVERIFY(!invalidDecoded.has_value());
}

} // namespace

QTEST_GUILESS_MAIN(TstFingerprint)

#include "tst_Fingerprint.moc"
