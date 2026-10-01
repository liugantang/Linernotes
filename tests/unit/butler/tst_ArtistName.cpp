// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QString>
#include <QTest>

#include <butler/ArtistName.h>
#include <common/TestSupport.h>

namespace {

using linernotes::butler::exactKey;
using linernotes::butler::romanKey;

class TstArtistName : public QObject {
    Q_OBJECT

private slots:
    void exactKeyNormalizes_data();
    void exactKeyNormalizes();
    void romanKeyMatchesKanaAndLatin();
    void romanKeyRejectsHanAndTooShort();
};

void TstArtistName::exactKeyNormalizes_data()
{
    QTest::addColumn<QString>("input1");
    QTest::addColumn<QString>("input2");
    QTest::addColumn<QString>("expectedKey");

    QTest::newRow("fullwidth/halfwidth")
        << QStringLiteral("Pastel＊Palettes") << QStringLiteral("Pastel*Palettes")
        << QStringLiteral("pastelpalettes");

    QTest::newRow("case") << QStringLiteral("GALNERYUS") << QStringLiteral("Galneryus")
                          << QStringLiteral("galneryus");

    QTest::newRow("traditional/simplified")
        << QStringLiteral("周杰倫") << QStringLiteral("周杰伦") << QStringLiteral("周杰伦");

    QTest::newRow("punctuation") << QStringLiteral("May’n") << QStringLiteral("May'n")
                                 << QStringLiteral("mayn");

    QTest::newRow("space") << QStringLiteral("佐々木 淳") << QStringLiteral("佐々木淳")
                           << QStringLiteral("佐々木淳");
}

void TstArtistName::exactKeyNormalizes()
{
    QFETCH(QString, input1);
    QFETCH(QString, input2);
    QFETCH(QString, expectedKey);

    QCOMPARE(exactKey(input1), expectedKey);
    QCOMPARE(exactKey(input2), expectedKey);
}

void TstArtistName::romanKeyMatchesKanaAndLatin()
{
    const QString k1 = romanKey(QStringLiteral("ハルカトミユキ"));
    const QString k2 = romanKey(QStringLiteral("Haruka to Miyuki"));
    QVERIFY(!k1.isEmpty());
    QCOMPARE(k1, k2);

    const QString c1 = romanKey(QStringLiteral("ちょうちょ"));
    const QString c2 = romanKey(QStringLiteral("ChouCho"));
    QVERIFY(!c1.isEmpty());
    QCOMPARE(c1, c2);
}

void TstArtistName::romanKeyRejectsHanAndTooShort()
{
    QVERIFY(romanKey(QStringLiteral("浜崎あゆみ")).isEmpty());
    QVERIFY(romanKey(QStringLiteral("IU")).isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistName)

#include "tst_ArtistName.moc"
