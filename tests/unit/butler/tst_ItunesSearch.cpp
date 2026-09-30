// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QTest>

#include <butler/ItunesSearch.h>
#include <common/TestSupport.h>

namespace {

using linernotes::butler::ItunesAlbum;
using linernotes::butler::itunesArtworkUrl;
using linernotes::butler::itunesSearchTerms;
using linernotes::butler::mergeItunesResults;
using linernotes::butler::parseItunesSearch;
using linernotes::test::fixturePath;

class TstItunesSearch : public QObject {
    Q_OBJECT

private slots:
    void parsesSearchAlbumTw();
    void parsesSearchAlbumEmpty();
    void parsesInvalidJsonReturnsError();
    void transformsArtworkUrl();
    void generatesSearchTerms();
    void mergesResultsPreservingOrderAndDeduplicating();
};

void TstItunesSearch::parsesSearchAlbumTw()
{
    const QString filePath = fixturePath(QStringLiteral("itunes/search_album_tw.json"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray json = file.readAll();

    const auto res = parseItunesSearch(json, QStringLiteral("tw"));
    QVERIFY(res.ok());

    const auto &albums = res.value();
    QCOMPARE(albums.size(), 10);

    const auto &first = albums.at(0);
    QCOMPARE(first.collectionId, 535739206LL);
    QCOMPARE(first.title, QStringLiteral("范特西"));
    QCOMPARE(first.artist, QStringLiteral("周杰倫"));
    QVERIFY(first.year.has_value());
    if (first.year.has_value()) {
        QCOMPARE(first.year.value(), 2001);
    }
    QCOMPARE(first.trackCount, 10);
    QCOMPARE(first.country, QStringLiteral("tw"));
    QCOMPARE(first.artworkUrl100.toString(),
        QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music124/v4/a4/f2/f0/"
                       "a4f2f0d4-cc80-d1a0-7f20-a0cc842a523c/JAY.jpg/100x100bb.jpg"));
}

void TstItunesSearch::parsesSearchAlbumEmpty()
{
    const QString filePath = fixturePath(QStringLiteral("itunes/search_album_empty.json"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray json = file.readAll();

    const auto res = parseItunesSearch(json, QStringLiteral("cn"));
    QVERIFY(res.ok());
    QCOMPARE(res.value().size(), 0);
}

void TstItunesSearch::parsesInvalidJsonReturnsError()
{
    const auto res = parseItunesSearch("not valid json", QStringLiteral("tw"));
    QVERIFY(!res.ok());
}

void TstItunesSearch::transformsArtworkUrl()
{
    const QUrl original(
        QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music124/v4/a4/f2/f0/"
                       "a4f2f0d4-cc80-d1a0-7f20-a0cc842a523c/JAY.jpg/100x100bb.jpg"));
    const QUrl url1400 = itunesArtworkUrl(original, 1400);
    QCOMPARE(url1400.toString(),
        QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music124/v4/a4/f2/f0/"
                       "a4f2f0d4-cc80-d1a0-7f20-a0cc842a523c/JAY.jpg/1400x1400bb.jpg"));

    const QUrl url300 = itunesArtworkUrl(original, 300);
    QCOMPARE(url300.toString(),
        QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/Music124/v4/a4/f2/f0/"
                       "a4f2f0d4-cc80-d1a0-7f20-a0cc842a523c/JAY.jpg/300x300bb.jpg"));

    // Non-standard ending returned untouched
    const QUrl nonStandard(QStringLiteral("https://example.com/cover.png"));
    QCOMPARE(itunesArtworkUrl(nonStandard, 1400), nonStandard);
}

void TstItunesSearch::generatesSearchTerms()
{
    // Simplified Chinese -> adds ICU Hans-Hant form (character-level; iTunes tw/hk still finds
    // 范特西)
    const QStringList termsZh
        = itunesSearchTerms(QStringLiteral("范特西"), QStringLiteral("周杰伦"));
    QCOMPARE(termsZh.size(), 2);
    QCOMPARE(termsZh.at(0), QStringLiteral("周杰伦 范特西"));
    QCOMPARE(termsZh.at(1), QStringLiteral("周傑倫 範特西"));

    // Pure English -> produces 1 term
    const QStringList termsEn
        = itunesSearchTerms(QStringLiteral("OK Computer"), QStringLiteral("Radiohead"));
    QCOMPARE(termsEn.size(), 1);
    QCOMPARE(termsEn.at(0), QStringLiteral("Radiohead OK Computer"));

    // Empty artist -> uses only album title
    const QStringList termsNoArtist
        = itunesSearchTerms(QStringLiteral("Abbey Road"), QStringLiteral(""));
    QCOMPARE(termsNoArtist.size(), 1);
    QCOMPARE(termsNoArtist.at(0), QStringLiteral("Abbey Road"));
}

void TstItunesSearch::mergesResultsPreservingOrderAndDeduplicating()
{
    ItunesAlbum alb1;
    alb1.collectionId = 101;
    alb1.title = QStringLiteral("Album 1");

    ItunesAlbum alb2;
    alb2.collectionId = 102;
    alb2.title = QStringLiteral("Album 2");

    ItunesAlbum alb3;
    alb3.collectionId = 103;
    alb3.title = QStringLiteral("Album 3");

    ItunesAlbum alb1Dup;
    alb1Dup.collectionId = 101;
    alb1Dup.title = QStringLiteral("Album 1 Duplicate");

    const QList<QList<ItunesAlbum>> perRequest = {
        { alb1, alb2 },
        { alb1Dup, alb3 },
    };

    const auto merged = mergeItunesResults(perRequest);
    QCOMPARE(merged.size(), 3);
    QCOMPARE(merged.at(0).collectionId, 101LL);
    QCOMPARE(merged.at(0).title, QStringLiteral("Album 1"));
    QCOMPARE(merged.at(1).collectionId, 102LL);
    QCOMPARE(merged.at(2).collectionId, 103LL);
}

} // namespace

QTEST_GUILESS_MAIN(TstItunesSearch)
#include "tst_ItunesSearch.moc"
