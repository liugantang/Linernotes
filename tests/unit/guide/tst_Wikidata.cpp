// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QTest>
#include <QUrlQuery>

#include <common/TestSupport.h>
#include <guide/Wikidata.h>

namespace {

using linernotes::guide::extractFacts;
using linernotes::guide::factPropertyName;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::parseWikidataEntities;
using linernotes::guide::parseWikidataSearch;
using linernotes::guide::pickAlbum;
using linernotes::guide::pickArtist;
using linernotes::guide::pickSong;
using linernotes::guide::preferredLabel;
using linernotes::guide::referencedIds;
using linernotes::guide::searchLanguagesFor;
using linernotes::guide::wikidataEntitiesUrl;
using linernotes::guide::wikidataEntityPageUrl;
using linernotes::guide::wikidataLabelsUrl;
using linernotes::guide::wikidataSearchUrl;
using linernotes::test::fixturePath;

QByteArray readFixture(const QString &relativePath)
{
    QFile file(fixturePath(relativePath));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

class TstWikidata : public QObject {
    Q_OBJECT

private slots:
    void parsesSearchSongIgnite();
    void parsesSearchInvalidJson();
    void parsesEntitiesArtistAoiEir();
    void parsesEntitiesInvalidJson();
    void picksArtistFromHits();
    void picksSongAndAlbum();
    void extractsFactsForSong();
    void determinesSearchLanguages();
    void constructsUrls();
};

void TstWikidata::parsesSearchSongIgnite()
{
    const QByteArray json = readFixture(QStringLiteral("guide/wd_search_song_ignite.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseWikidataSearch(json);
    QVERIFY(res.ok());

    const auto &hits = res.value();
    QCOMPARE(hits.size(), 10);
    QCOMPARE(hits.at(0).id, QStringLiteral("Q17988679"));
    QCOMPARE(hits.at(0).label, QStringLiteral("IGNITE"));
    QCOMPARE(hits.at(0).description, QStringLiteral("single by Eir Aoi"));
}

void TstWikidata::parsesSearchInvalidJson()
{
    const auto res = parseWikidataSearch("not a valid json");
    QVERIFY(!res.ok());
}

void TstWikidata::parsesEntitiesArtistAoiEir()
{
    const QByteArray json = readFixture(QStringLiteral("guide/wd_entities_artist_aoi_eir.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseWikidataEntities(json);
    QVERIFY(res.ok());

    const auto &entities = res.value();
    QVERIFY(entities.contains(QStringLiteral("Q4021857")));

    const auto &artist = entities.value(QStringLiteral("Q4021857"));
    QCOMPARE(artist.id, QStringLiteral("Q4021857"));
    QCOMPARE(artist.labels.value(QStringLiteral("zh")), QStringLiteral("藍井艾露"));
    QCOMPARE(artist.labels.value(QStringLiteral("ja")), QStringLiteral("藍井エイル"));
    QCOMPARE(artist.labels.value(QStringLiteral("en")), QStringLiteral("Eir Aoi"));

    const QStringList p31 = artist.itemClaims.value(QStringLiteral("P31"));
    QVERIFY(p31.contains(QStringLiteral("Q5")));

    const QStringList p106 = artist.itemClaims.value(QStringLiteral("P106"));
    QVERIFY(p106.contains(QStringLiteral("Q177220")));

    QCOMPARE(artist.sitelinkTitles.value(QStringLiteral("zhwiki")), QStringLiteral("藍井艾露"));
    QCOMPARE(artist.sitelinkTitles.value(QStringLiteral("jawiki")), QStringLiteral("藍井エイル"));
    QCOMPARE(artist.sitelinkTitles.value(QStringLiteral("enwiki")), QStringLiteral("Eir Aoi"));

    QCOMPARE(preferredLabel(artist), QStringLiteral("藍井艾露"));
}

void TstWikidata::parsesEntitiesInvalidJson()
{
    const auto res = parseWikidataEntities("invalid json content");
    QVERIFY(!res.ok());
}

void TstWikidata::picksArtistFromHits()
{
    const QByteArray searchJson
        = readFixture(QStringLiteral("guide/wd_search_artist_aoi_eir.json"));
    const QByteArray entitiesJson
        = readFixture(QStringLiteral("guide/wd_entities_artist_aoi_eir.json"));
    QVERIFY(!searchJson.isEmpty());
    QVERIFY(!entitiesJson.isEmpty());

    const auto searchRes = parseWikidataSearch(searchJson);
    const auto entitiesRes = parseWikidataEntities(entitiesJson);
    QVERIFY(searchRes.ok());
    QVERIFY(entitiesRes.ok());

    const auto picked = pickArtist(searchRes.value(), entitiesRes.value());
    QCOMPARE(picked, std::optional<QString>(QStringLiteral("Q4021857")));
}

void TstWikidata::picksSongAndAlbum()
{
    const QByteArray songSearchJson
        = readFixture(QStringLiteral("guide/wd_search_song_ignite.json"));
    const QByteArray songEntitiesJson
        = readFixture(QStringLiteral("guide/wd_entities_song_ignite.json"));
    QVERIFY(!songSearchJson.isEmpty());
    QVERIFY(!songEntitiesJson.isEmpty());

    const auto songHits = parseWikidataSearch(songSearchJson).value();
    const auto songEntities = parseWikidataEntities(songEntitiesJson).value();

    const auto pickedSong = pickSong(songHits, songEntities, QStringLiteral("Q4021857"));
    QCOMPARE(pickedSong, std::optional<QString>(QStringLiteral("Q17988679")));

    const auto pickedWrongSong = pickSong(songHits, songEntities, QStringLiteral("Q999999"));
    QCOMPARE(pickedWrongSong, std::nullopt);

    const auto pickedEmptyArtist = pickSong(songHits, songEntities, QString());
    QCOMPARE(pickedEmptyArtist, std::nullopt);

    const QByteArray albumSearchJson
        = readFixture(QStringLiteral("guide/wd_search_album_dazur.json"));
    const QByteArray albumEntitiesJson
        = readFixture(QStringLiteral("guide/wd_entities_album_dazur.json"));
    QVERIFY(!albumSearchJson.isEmpty());
    QVERIFY(!albumEntitiesJson.isEmpty());

    const auto albumHits = parseWikidataSearch(albumSearchJson).value();
    const auto albumEntities = parseWikidataEntities(albumEntitiesJson).value();

    const auto pickedAlbum = pickAlbum(albumHits, albumEntities, QStringLiteral("Q4021857"));
    QCOMPARE(pickedAlbum, std::optional<QString>(QStringLiteral("Q22126542")));

    const auto pickedWrongAlbum = pickAlbum(albumHits, albumEntities, QStringLiteral("Q999999"));
    QCOMPARE(pickedWrongAlbum, std::nullopt);
}

void TstWikidata::extractsFactsForSong()
{
    const QByteArray songEntitiesJson
        = readFixture(QStringLiteral("guide/wd_entities_song_ignite.json"));
    const QByteArray labelsJson = readFixture(QStringLiteral("guide/wd_labels_refs.json"));
    QVERIFY(!songEntitiesJson.isEmpty());
    QVERIFY(!labelsJson.isEmpty());

    const auto songEntities = parseWikidataEntities(songEntitiesJson).value();
    const auto labelEntities = parseWikidataEntities(labelsJson).value();

    const auto &ignite = songEntities.value(QStringLiteral("Q17988679"));
    const auto refs = referencedIds(ignite, KnowledgeKind::Song);
    QVERIFY(refs.contains(QStringLiteral("Q4021857")));
    QVERIFY(refs.contains(QStringLiteral("Q10852418")));

    const auto facts = extractFacts(ignite, KnowledgeKind::Song, labelEntities);

    bool foundP577 = false;
    bool foundP175 = false;
    for (const auto &fact : facts) {
        if (fact.property == QStringLiteral("P577")) {
            foundP577 = true;
            QCOMPARE(fact.value, QStringLiteral("2014-08-20"));
        } else if (fact.property == QStringLiteral("P175")) {
            foundP175 = true;
            QCOMPARE(fact.value, QStringLiteral("藍井艾露"));
        }
    }
    QVERIFY(foundP577);
    QVERIFY(foundP175);

    QCOMPARE(factPropertyName(QStringLiteral("P577")), QStringLiteral("Release date"));
    QCOMPARE(factPropertyName(QStringLiteral("P175")), QStringLiteral("Performer"));
    QCOMPARE(factPropertyName(QStringLiteral("P99999")), QStringLiteral("P99999"));
}

void TstWikidata::determinesSearchLanguages()
{
    QCOMPARE(
        searchLanguagesFor(QStringLiteral("藍井エイル")), QStringList({ QStringLiteral("ja") }));
    QCOMPARE(searchLanguagesFor(QStringLiteral("梶浦由記")),
        QStringList({ QStringLiteral("ja"), QStringLiteral("zh") }));
    QCOMPARE(searchLanguagesFor(QStringLiteral("Roxanne")),
        QStringList({ QStringLiteral("en"), QStringLiteral("ja") }));
}

void TstWikidata::constructsUrls()
{
    const QUrl searchUrl = wikidataSearchUrl(QStringLiteral("藍井エイル"), QStringLiteral("ja"));
    QCOMPARE(searchUrl.host(), QStringLiteral("www.wikidata.org"));
    QCOMPARE(searchUrl.path(), QStringLiteral("/w/api.php"));
    const QUrlQuery searchQ(searchUrl.query());
    QCOMPARE(searchQ.queryItemValue(QStringLiteral("action")), QStringLiteral("wbsearchentities"));
    QCOMPARE(searchQ.queryItemValue(QStringLiteral("language")), QStringLiteral("ja"));
    QCOMPARE(searchQ.queryItemValue(QStringLiteral("uselang")), QStringLiteral("ja"));
    QCOMPARE(searchQ.queryItemValue(QStringLiteral("type")), QStringLiteral("item"));
    QCOMPARE(searchQ.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));
    QCOMPARE(searchQ.queryItemValue(QStringLiteral("format")), QStringLiteral("json"));

    const QUrl entitiesUrl
        = wikidataEntitiesUrl({ QStringLiteral("Q4021857"), QStringLiteral("Q17988679") });
    QCOMPARE(entitiesUrl.host(), QStringLiteral("www.wikidata.org"));
    const QUrlQuery entitiesQ(entitiesUrl.query());
    QCOMPARE(entitiesQ.queryItemValue(QStringLiteral("action")), QStringLiteral("wbgetentities"));
    QCOMPARE(entitiesQ.queryItemValue(QStringLiteral("props")),
        QStringLiteral("labels|descriptions|claims|sitelinks/urls"));
    QCOMPARE(entitiesQ.queryItemValue(QStringLiteral("languages")), QStringLiteral("zh|ja|en"));

    const QUrl labelsUrl = wikidataLabelsUrl({ QStringLiteral("Q4021857") });
    const QUrlQuery labelsQ(labelsUrl.query());
    QCOMPARE(labelsQ.queryItemValue(QStringLiteral("props")), QStringLiteral("labels"));

    const QUrl pageUrl = wikidataEntityPageUrl(QStringLiteral("Q4021857"));
    QCOMPARE(pageUrl.toString(), QStringLiteral("https://www.wikidata.org/wiki/Q4021857"));
}

} // namespace

QTEST_GUILESS_MAIN(TstWikidata)
#include "tst_Wikidata.moc"
