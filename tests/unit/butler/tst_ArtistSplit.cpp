// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include <butler/ArtistSplit.h>
#include <common/TestSupport.h>

namespace {

using linernotes::butler::SplitVerdict;

class TstArtistSplit : public QObject {
    Q_OBJECT

private slots:
    void decideSplit_data();
    void decideSplit();
    void weakSeparatorWithKnownParts();
};

void TstArtistSplit::decideSplit_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<SplitVerdict>("expectedVerdict");
    QTest::addColumn<QStringList>("expectedParts");

    QTest::newRow("feat_with_dot_no_space")
        << QStringLiteral("Starving Trancer feat.Saori Hayami") << SplitVerdict::Split
        << QStringList { QStringLiteral("Starving Trancer"), QStringLiteral("Saori Hayami") };

    QTest::newRow("feat_with_space")
        << QStringLiteral("Yoshino Nanjo feat. yanaginagi") << SplitVerdict::Split
        << QStringList { QStringLiteral("Yoshino Nanjo"), QStringLiteral("yanaginagi") };

    QTest::newRow("multiply_no_space")
        << QStringLiteral("angela×fripSide") << SplitVerdict::Split
        << QStringList { QStringLiteral("angela"), QStringLiteral("fripSide") };

    QTest::newRow("multiply_with_space")
        << QStringLiteral("KOTOKO × LUNA") << SplitVerdict::Split
        << QStringList { QStringLiteral("KOTOKO"), QStringLiteral("LUNA") };

    QTest::newRow("semicolon_not_separator")
        << QStringLiteral("ave;new feat.C;LINE") << SplitVerdict::Split
        << QStringList { QStringLiteral("ave;new"), QStringLiteral("C;LINE") };

    QTest::newRow("pure_digits_keep")
        << QStringLiteral("22/7") << SplitVerdict::Keep << QStringList { QStringLiteral("22/7") };

    QTest::newRow("slash_without_space_ambiguous")
        << QStringLiteral("+α/Alfakyun.") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("+α"), QStringLiteral("Alfakyun.") };

    QTest::newRow("simon_and_garfunkel")
        << QStringLiteral("Simon & Garfunkel") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Simon"), QStringLiteral("Garfunkel") };

    QTest::newRow("earth_wind_and_fire")
        << QStringLiteral("Earth, Wind & Fire") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Earth"), QStringLiteral("Wind"), QStringLiteral("Fire") };

    QTest::newRow("florence_plus_the_machine")
        << QStringLiteral("Florence + the Machine") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Florence"), QStringLiteral("the Machine") };

    QTest::newRow("tom_petty_and_the_heartbreakers")
        << QStringLiteral("Tom Petty and the Heartbreakers") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Tom Petty"), QStringLiteral("the Heartbreakers") };

    QTest::newRow("wake_up_girls")
        << QStringLiteral("Wake Up, Girls!") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Wake Up"), QStringLiteral("Girls!") };

    QTest::newRow("comma_weak_separator_ambiguous")
        << QStringLiteral("Risa Taneda, Minori Chihara, Yuri Yamaoka") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Risa Taneda"), QStringLiteral("Minori Chihara"),
               QStringLiteral("Yuri Yamaoka") };

    QTest::newRow("cv_ambiguous") << QStringLiteral(
        "Mahiru Tsuyuzaki (CV: Haruki Iwata), Karen Aijo (CV: Momoyo Koyama)")
                                  << SplitVerdict::Ambiguous
                                  << QStringList { QStringLiteral(
                                                       "Mahiru Tsuyuzaki (CV: Haruki Iwata)"),
                                         QStringLiteral("Karen Aijo (CV: Momoyo Koyama)") };

    QTest::newRow("brackets_inside_keep")
        << QStringLiteral("sweet ARMS (Iori Nomizu, Misuzu Togashi, Kaori Sadohara, Misato)")
        << SplitVerdict::Keep
        << QStringList { QStringLiteral(
               "sweet ARMS (Iori Nomizu, Misuzu Togashi, Kaori Sadohara, Misato)") };

    QTest::newRow("multivalue_with_feat")
        << QStringLiteral("A / B feat. C") << SplitVerdict::Split
        << QStringList { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") };
}

void TstArtistSplit::decideSplit()
{
    QFETCH(QString, input);
    QFETCH(SplitVerdict, expectedVerdict);
    QFETCH(QStringList, expectedParts);

    const auto decision = linernotes::butler::decideSplit(input, { });
    QCOMPARE(decision.verdict, expectedVerdict);
    QCOMPARE(decision.parts, expectedParts);
}

void TstArtistSplit::weakSeparatorWithKnownParts()
{
    const QSet<QString> known = {
        QStringLiteral("Risa Taneda"),
        QStringLiteral("Minori Chihara"),
        QStringLiteral("Yuri Yamaoka"),
    };
    const auto decision = linernotes::butler::decideSplit(
        QStringLiteral("Risa Taneda, Minori Chihara, Yuri Yamaoka"), known);
    QCOMPARE(decision.verdict, SplitVerdict::Split);
    QCOMPARE(decision.parts,
        (QStringList {
            QStringLiteral("Risa Taneda"),
            QStringLiteral("Minori Chihara"),
            QStringLiteral("Yuri Yamaoka"),
        }));
    QCOMPARE(decision.confidence, 0.9);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistSplit)

#include "tst_ArtistSplit.moc"
