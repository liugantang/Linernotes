// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QTest>

#include <butler/TitleMatchLlm.h>
#include <butler/TitleMatchSource.h>
#include <butler/TitleMatchStore.h>

namespace {

using linernotes::butler::findTitlePairs;
using linernotes::butler::parseTitleMatchResult;
using linernotes::butler::TitleMatchTrack;
using linernotes::butler::TitlePair;

class TstTitleMatch : public QObject {
    Q_OBJECT

private slots:
    void findTitlePairsSameArtistWithinTolerance();
    void findTitlePairsDurationExceedsTolerance();
    void findTitlePairsBothLatinOnly();
    void findTitlePairsDifferentMainArtists();
    void parseTitleMatchResultNormalAndOutOfBounds();
};

void TstTitleMatch::findTitlePairsSameArtistWithinTolerance()
{
    const QList<TitleMatchTrack> tracks = {
        TitleMatchTrack {
            .trackId = 1,
            .workId = 10,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Haruka to Miyuki"),
            .baseTitle = QStringLiteral("17-sai"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 271000,
        },
        TitleMatchTrack {
            .trackId = 2,
            .workId = 20,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Haruka to Miyuki"),
            .baseTitle = QStringLiteral("17才"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 273300, // diff = 2300 ms (2.3 s <= 3.0 s)
        },
    };

    const auto pairs = findTitlePairs(tracks);
    QCOMPARE(pairs.size(), 1);
    const auto &p = pairs.at(0);
    QCOMPARE(p.titleA, QStringLiteral("17-sai"));
    QCOMPARE(p.titleB, QStringLiteral("17才"));
    QCOMPARE(p.keyA, QStringLiteral("17sai"));
    QCOMPARE(p.keyB, QStringLiteral("17才"));
    QCOMPARE(p.artist, QStringLiteral("Haruka to Miyuki"));
}

void TstTitleMatch::findTitlePairsDurationExceedsTolerance()
{
    const QList<TitleMatchTrack> tracks = {
        TitleMatchTrack {
            .trackId = 1,
            .workId = 10,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Haruka to Miyuki"),
            .baseTitle = QStringLiteral("17-sai"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 271000,
        },
        TitleMatchTrack {
            .trackId = 2,
            .workId = 20,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Haruka to Miyuki"),
            .baseTitle = QStringLiteral("17才"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 276000, // diff = 5000 ms (5 s > 3.0 s)
        },
    };

    const auto pairs = findTitlePairs(tracks);
    QCOMPARE(pairs.size(), 0);
}

void TstTitleMatch::findTitlePairsBothLatinOnly()
{
    const QList<TitleMatchTrack> tracks = {
        TitleMatchTrack {
            .trackId = 1,
            .workId = 10,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Artist X"),
            .baseTitle = QStringLiteral("17-sai"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 271000,
        },
        TitleMatchTrack {
            .trackId = 2,
            .workId = 20,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Artist X"),
            .baseTitle = QStringLiteral("Seventeen"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 272000,
        },
    };

    const auto pairs = findTitlePairs(tracks);
    QCOMPARE(pairs.size(), 0);
}

void TstTitleMatch::findTitlePairsDifferentMainArtists()
{
    const QList<TitleMatchTrack> tracks = {
        TitleMatchTrack {
            .trackId = 1,
            .workId = 10,
            .mainArtistId = 100,
            .artistName = QStringLiteral("Artist A"),
            .baseTitle = QStringLiteral("17-sai"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 271000,
        },
        TitleMatchTrack {
            .trackId = 2,
            .workId = 20,
            .mainArtistId = 200,
            .artistName = QStringLiteral("Artist B"),
            .baseTitle = QStringLiteral("17才"),
            .versionType = QStringLiteral("studio"),
            .durationMs = 273300,
        },
    };

    const auto pairs = findTitlePairs(tracks);
    QCOMPARE(pairs.size(), 0);
}

void TstTitleMatch::parseTitleMatchResultNormalAndOutOfBounds()
{
    const QList<TitlePair> pairs = {
        TitlePair {
            .titleA = QStringLiteral("17-sai"),
            .titleB = QStringLiteral("17才"),
            .keyA = QStringLiteral("17sai"),
            .keyB = QStringLiteral("17才"),
            .artist = QStringLiteral("Haruka to Miyuki"),
        },
    };

    // Item 1: Valid id=1
    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    obj1.insert(QStringLiteral("same"), true);
    obj1.insert(QStringLiteral("confidence"), 0.95);
    obj1.insert(QStringLiteral("reason"), QStringLiteral("Romanization match"));

    // Item 2: Out of bounds id=99
    QJsonObject objOob;
    objOob.insert(QStringLiteral("id"), 99);
    objOob.insert(QStringLiteral("same"), false);
    objOob.insert(QStringLiteral("confidence"), 0.1);
    objOob.insert(QStringLiteral("reason"), QStringLiteral("Out of bounds"));

    QJsonArray itemsArr;
    itemsArr.append(obj1);
    itemsArr.append(objOob);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseTitleMatchResult(root, pairs);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 1);
    const auto keyPair = qMakePair(QStringLiteral("17sai"), QStringLiteral("17才"));
    QVERIFY(map.contains(keyPair));

    const auto &verdict = map.value(keyPair);
    QCOMPARE(verdict.same, true);
    QCOMPARE(verdict.confidence, 0.95);
    QCOMPARE(verdict.reason, QStringLiteral("Romanization match"));
}

} // namespace

QTEST_GUILESS_MAIN(TstTitleMatch)

#include "tst_TitleMatch.moc"
