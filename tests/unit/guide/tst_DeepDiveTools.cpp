// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTest>

#include <ai/ChatTypes.h>
#include <ai/JsonSchema.h>
#include <guide/DeepDiveTools.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/Lyrics.h>
#include <guide/Wikidata.h>
#include <guide/Wikipedia.h>
#include <library/LibraryQuery.h>
#include <library/LibrarySearch.h>

namespace {

using linernotes::guide::deepDiveToolSpecs;
using linernotes::guide::factPropertyName;
using linernotes::guide::KnowledgeFact;
using linernotes::guide::KnowledgeResult;
using linernotes::guide::KnowledgeStatus;
using linernotes::guide::lookupResultJson;
using linernotes::guide::Lyrics;
using linernotes::guide::LyricsResult;
using linernotes::guide::lyricsResultJson;
using linernotes::guide::LyricsSource;
using linernotes::guide::LyricsStatus;
using linernotes::guide::searchResultJson;
using linernotes::guide::WikipediaSummary;

class TstDeepDiveTools : public QObject {
    Q_OBJECT

private slots:
    void toolSpecsValidAndCompile();
    void lookupResultJsonFound();
    void lookupResultJsonNotFoundAndFailed();
    void searchResultJsonFieldsAndPlays();
    void lyricsResultJsonFoundWithTranslations();
    void lyricsResultJsonTruncation();
    void lyricsResultJsonStatuses();
};

void TstDeepDiveTools::toolSpecsValidAndCompile()
{
    const auto specs = deepDiveToolSpecs();
    QCOMPARE(specs.size(), 3);

    QCOMPARE(specs.at(0).name, QStringLiteral("lookup_knowledge"));
    QCOMPARE(specs.at(1).name, QStringLiteral("search_library"));
    QCOMPARE(specs.at(2).name, QStringLiteral("get_lyrics"));

    for (const auto &spec : specs) {
        QVERIFY(!spec.description.isEmpty());
        const auto compileRes = linernotes::ai::JsonSchema::compile(spec.parameters);
        QVERIFY(compileRes.ok());
    }

    // Test lookup_knowledge schema validation
    {
        const auto compileRes = linernotes::ai::JsonSchema::compile(specs.at(0).parameters);
        QVERIFY(compileRes.ok());
        const auto &schema = compileRes.value();

        QJsonObject validArtist;
        validArtist.insert(QStringLiteral("kind"), QStringLiteral("artist"));
        validArtist.insert(QStringLiteral("artist"), QStringLiteral("Aimer"));
        QVERIFY(schema.validate(validArtist).ok());

        QJsonObject validSong;
        validSong.insert(QStringLiteral("kind"), QStringLiteral("song"));
        validSong.insert(QStringLiteral("artist"), QStringLiteral("Aimer"));
        validSong.insert(QStringLiteral("title"), QStringLiteral("Brave Shine"));
        QVERIFY(schema.validate(validSong).ok());

        QJsonObject invalidKind;
        invalidKind.insert(QStringLiteral("kind"), QStringLiteral("single"));
        invalidKind.insert(QStringLiteral("artist"), QStringLiteral("Aimer"));
        QVERIFY(!schema.validate(invalidKind).ok());

        QJsonObject missingArtist;
        missingArtist.insert(QStringLiteral("kind"), QStringLiteral("artist"));
        QVERIFY(!schema.validate(missingArtist).ok());
    }

    // Test search_library schema validation
    {
        const auto compileRes = linernotes::ai::JsonSchema::compile(specs.at(1).parameters);
        QVERIFY(compileRes.ok());
        const auto &schema = compileRes.value();

        QJsonObject validSearch;
        validSearch.insert(QStringLiteral("text"), QStringLiteral("Aimer"));
        QVERIFY(schema.validate(validSearch).ok());

        QJsonObject invalidEmpty;
        QVERIFY(!schema.validate(invalidEmpty).ok());
    }

    // Test get_lyrics schema validation
    {
        const auto compileRes = linernotes::ai::JsonSchema::compile(specs.at(2).parameters);
        QVERIFY(compileRes.ok());
        const auto &schema = compileRes.value();

        QJsonObject emptyObj;
        QVERIFY(schema.validate(emptyObj).ok());
    }
}

void TstDeepDiveTools::lookupResultJsonFound()
{
    KnowledgeResult r;
    r.status = KnowledgeStatus::Found;
    r.entry.label = QStringLiteral("Aimer");
    r.entry.description = QStringLiteral("Japanese singer");
    r.entry.facts = {
        KnowledgeFact {
            .property = QStringLiteral("P136"),
            .value = QStringLiteral("J-pop"),
        },
        KnowledgeFact {
            .property = QStringLiteral("P495"),
            .value = QStringLiteral("Japan"),
        },
    };
    WikipediaSummary summary;
    summary.lang = QStringLiteral("zh");
    summary.title = QStringLiteral("Aimer");
    summary.extract = QStringLiteral("Aimer是日本女歌手。");
    summary.pageUrl = QUrl(QStringLiteral("https://zh.wikipedia.org/wiki/Aimer"));
    r.entry.summary = summary;
    r.entry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q1056893"));

    const QString jsonStr = lookupResultJson(r);
    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QVERIFY(doc.isObject());
    const auto obj = doc.object();

    QCOMPARE(obj.value(QStringLiteral("found")).toBool(), true);
    QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("Aimer"));
    QCOMPARE(
        obj.value(QStringLiteral("description")).toString(), QStringLiteral("Japanese singer"));
    QCOMPARE(
        obj.value(QStringLiteral("extract")).toString(), QStringLiteral("Aimer是日本女歌手。"));

    const auto factsArr = obj.value(QStringLiteral("facts")).toArray();
    QCOMPARE(factsArr.size(), 2);
    QCOMPARE(factsArr.at(0).toObject().value(QStringLiteral("name")).toString(),
        factPropertyName(QStringLiteral("P136")));
    QCOMPARE(factsArr.at(0).toObject().value(QStringLiteral("value")).toString(),
        QStringLiteral("J-pop"));
    QCOMPARE(factsArr.at(1).toObject().value(QStringLiteral("name")).toString(),
        factPropertyName(QStringLiteral("P495")));
    QCOMPARE(factsArr.at(1).toObject().value(QStringLiteral("value")).toString(),
        QStringLiteral("Japan"));

    const auto srcIdsArr = obj.value(QStringLiteral("source_ids")).toArray();
    QCOMPARE(srcIdsArr.size(), 2);
    QCOMPARE(srcIdsArr.at(0).toInt(), 1);
    QCOMPARE(srcIdsArr.at(1).toInt(), 2);
}

void TstDeepDiveTools::lookupResultJsonNotFoundAndFailed()
{
    // NotFound
    {
        KnowledgeResult r;
        r.status = KnowledgeStatus::NotFound;
        const QString jsonStr = lookupResultJson(r);
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QStringLiteral("found")).toBool(), false);
    }

    // Failed
    {
        KnowledgeResult r;
        r.status = KnowledgeStatus::Failed;
        const QString jsonStr = lookupResultJson(r);
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QVERIFY(doc.isObject());
        QCOMPARE(doc.object().value(QStringLiteral("error")).toString(),
            QStringLiteral("lookup failed"));
    }
}

void TstDeepDiveTools::searchResultJsonFieldsAndPlays()
{
    linernotes::library::SearchResults results;

    linernotes::library::TrackRow t1;
    t1.trackId = 101;
    t1.title = QStringLiteral("Song 1");
    t1.artist = QStringLiteral("Artist 1");
    t1.album = QStringLiteral("Album 1");
    t1.year = 2020;
    t1.playCount = 3;

    linernotes::library::TrackRow t2;
    t2.trackId = 102;
    t2.title = QStringLiteral("Song 2");
    t2.artist = QStringLiteral("Artist 2");
    t2.album = QStringLiteral("Album 2");
    t2.year = std::nullopt;
    t2.playCount = 0;

    results.tracks = { t1, t2 };

    linernotes::library::AlbumRow a1;
    a1.albumId = 201;
    a1.title = QStringLiteral("Album 1");
    a1.albumArtist = QStringLiteral("Artist 1");
    a1.year = 2020;

    results.albums = { a1 };

    linernotes::library::ArtistRow ar1;
    ar1.artistId = 301;
    ar1.name = QStringLiteral("Artist 1");

    results.artists = { ar1 };

    QHash<qint64, int> plays;
    plays.insert(101, 15);

    const QString jsonStr = searchResultJson(results, plays);
    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QVERIFY(doc.isObject());
    const auto obj = doc.object();

    const auto tracksArr = obj.value(QStringLiteral("tracks")).toArray();
    QCOMPARE(tracksArr.size(), 2);
    {
        const auto item = tracksArr.at(0).toObject();
        QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("Song 1"));
        QCOMPARE(item.value(QStringLiteral("artist")).toString(), QStringLiteral("Artist 1"));
        QCOMPARE(item.value(QStringLiteral("album")).toString(), QStringLiteral("Album 1"));
        QCOMPARE(item.value(QStringLiteral("year")).toInt(), 2020);
        QCOMPARE(item.value(QStringLiteral("plays")).toInt(), 15);
    }
    {
        const auto item = tracksArr.at(1).toObject();
        QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("Song 2"));
        QCOMPARE(item.value(QStringLiteral("artist")).toString(), QStringLiteral("Artist 2"));
        QCOMPARE(item.value(QStringLiteral("album")).toString(), QStringLiteral("Album 2"));
        QVERIFY(item.value(QStringLiteral("year")).isNull());
        QCOMPARE(item.value(QStringLiteral("plays")).toInt(), 0);
    }

    const auto albumsArr = obj.value(QStringLiteral("albums")).toArray();
    QCOMPARE(albumsArr.size(), 1);
    {
        const auto item = albumsArr.at(0).toObject();
        QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("Album 1"));
        QCOMPARE(item.value(QStringLiteral("artist")).toString(), QStringLiteral("Artist 1"));
        QCOMPARE(item.value(QStringLiteral("year")).toInt(), 2020);
    }

    const auto artistsArr = obj.value(QStringLiteral("artists")).toArray();
    QCOMPARE(artistsArr.size(), 1);
    {
        const auto item = artistsArr.at(0).toObject();
        QCOMPARE(item.value(QStringLiteral("name")).toString(), QStringLiteral("Artist 1"));
    }
}

void TstDeepDiveTools::lyricsResultJsonFoundWithTranslations()
{
    LyricsResult r;
    r.trackId = 1;
    r.status = LyricsStatus::Found;
    r.lyrics = Lyrics {
        .text
        = QStringLiteral("[00:01.00]Line 1\n[00:02.00]   \n[00:03.00]Line 3\n[00:04.00]Line 4"),
        .source = LyricsSource::Lrclib,
        .synced = true,
    };

    const QStringList translations = {
        QStringLiteral("译文 1"),
        QString(),
        QString(),
        QStringLiteral("译文 4"),
    };

    const QString jsonStr = lyricsResultJson(r, translations);
    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QVERIFY(doc.isObject());
    const QString lyrics = doc.object().value(QStringLiteral("lyrics")).toString();

    const QString expected = QStringLiteral("Line 1 / 译文 1\nLine 3\nLine 4 / 译文 4");
    QCOMPARE(lyrics, expected);
}

void TstDeepDiveTools::lyricsResultJsonTruncation()
{
    LyricsResult r;
    r.trackId = 1;
    r.status = LyricsStatus::Found;
    r.lyrics = Lyrics {
        .text = QString(5000, QLatin1Char('A')),
        .source = LyricsSource::Lrclib,
        .synced = false,
    };

    const QString jsonStr = lyricsResultJson(r, { });
    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QVERIFY(doc.isObject());
    const QString lyrics = doc.object().value(QStringLiteral("lyrics")).toString();

    QCOMPARE(lyrics.size(), 4000);
}

void TstDeepDiveTools::lyricsResultJsonStatuses()
{
    // Instrumental
    {
        LyricsResult r;
        r.status = LyricsStatus::Instrumental;
        const QString jsonStr = lyricsResultJson(r, { });
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QVERIFY(doc.isObject());
        QVERIFY(doc.object().value(QStringLiteral("lyrics")).isNull());
        QCOMPARE(doc.object().value(QStringLiteral("status")).toString(),
            QStringLiteral("instrumental"));
    }

    // NotFound
    {
        LyricsResult r;
        r.status = LyricsStatus::NotFound;
        const QString jsonStr = lyricsResultJson(r, { });
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QVERIFY(doc.isObject());
        QVERIFY(doc.object().value(QStringLiteral("lyrics")).isNull());
        QCOMPARE(
            doc.object().value(QStringLiteral("status")).toString(), QStringLiteral("not_found"));
    }

    // Failed
    {
        LyricsResult r;
        r.status = LyricsStatus::Failed;
        const QString jsonStr = lyricsResultJson(r, { });
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        QVERIFY(doc.isObject());
        QVERIFY(doc.object().value(QStringLiteral("lyrics")).isNull());
        QCOMPARE(doc.object().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstDeepDiveTools)

#include "tst_DeepDiveTools.moc"
