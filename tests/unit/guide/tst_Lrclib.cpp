// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>
#include <QUrlQuery>

#include <guide/Lrclib.h>
#include <library/LibraryQuery.h>

namespace {

using namespace linernotes::guide;
using linernotes::library::TrackRow;

class TstLrclib : public QObject {
    Q_OBJECT

private slots:
    void parsesLrclibRecordJson();
    void parsesLrclibRecordWithNullSyncedLyrics();
    void parsesLrclibSearchJson();
    void picksSearchResultByTierAndDuration();
    void buildsLrclibQueryAndUrls();
};

void TstLrclib::parsesLrclibRecordJson()
{
    const QByteArray json = R"({
        "id": 12345,
        "trackName": "Calling You",
        "artistName": "Jevetta Steele",
        "albumName": "Bagdad Cafe",
        "duration": 320.5,
        "instrumental": false,
        "plainLyrics": "A desert road from Vegas to nowhere...",
        "syncedLyrics": "[00:10.50] A desert road from Vegas to nowhere..."
    })";

    const auto res = parseLrclibRecord(json);
    QVERIFY(res.ok());
    const auto &rec = res.value();
    QCOMPARE(rec.id, 12345);
    QCOMPARE(rec.trackName, QStringLiteral("Calling You"));
    QCOMPARE(rec.artistName, QStringLiteral("Jevetta Steele"));
    QCOMPARE(rec.albumName, QStringLiteral("Bagdad Cafe"));
    QCOMPARE(rec.durationSec, 320.5);
    QCOMPARE(rec.instrumental, false);
    QCOMPARE(rec.plainLyrics, QStringLiteral("A desert road from Vegas to nowhere..."));
    QCOMPARE(rec.syncedLyrics, QStringLiteral("[00:10.50] A desert road from Vegas to nowhere..."));
}

void TstLrclib::parsesLrclibRecordWithNullSyncedLyrics()
{
    const QByteArray json = R"({
        "id": 67890,
        "trackName": "Instrumental Track",
        "artistName": "Composer",
        "albumName": "Soundtrack",
        "duration": 180.0,
        "instrumental": true,
        "plainLyrics": "",
        "syncedLyrics": null
    })";

    const auto res = parseLrclibRecord(json);
    QVERIFY(res.ok());
    const auto &rec = res.value();
    QCOMPARE(rec.id, 67890);
    QCOMPARE(rec.instrumental, true);
    QVERIFY(rec.plainLyrics.isEmpty());
    QVERIFY(rec.syncedLyrics.isEmpty());
}

void TstLrclib::parsesLrclibSearchJson()
{
    const QByteArray json = R"([
        {
            "id": 1,
            "trackName": "Song A",
            "artistName": "Artist A",
            "albumName": "Album A",
            "duration": 200.0,
            "instrumental": false,
            "plainLyrics": "Plain",
            "syncedLyrics": "[00:01.00] Synced"
        },
        {
            "id": 2,
            "trackName": "Song B",
            "artistName": "Artist B",
            "albumName": "Album B",
            "duration": 210.0,
            "instrumental": true,
            "plainLyrics": "",
            "syncedLyrics": ""
        }
    ])";

    const auto res = parseLrclibSearch(json);
    QVERIFY(res.ok());
    const auto &list = res.value();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).id, 1);
    QCOMPARE(list.at(1).id, 2);
}

void TstLrclib::picksSearchResultByTierAndDuration()
{
    const LrclibQuery q {
        .title = QStringLiteral("Calling You"),
        .artist = QStringLiteral("Jevetta Steele"),
        .album = QStringLiteral("Bagdad Cafe"),
        .durationSec = 200,
    };

    // 1. Exclude records with duration difference > 3s
    LrclibRecord rFar {
        .id = 1,
        .trackName = QStringLiteral("Calling You"),
        .artistName = QStringLiteral("Jevetta Steele"),
        .albumName = QStringLiteral("Bagdad Cafe"),
        .durationSec = 205.0, // diff = 5.0 > 3.0
        .instrumental = false,
        .plainLyrics = QStringLiteral("Plain"),
        .syncedLyrics = QStringLiteral("[00:01.00] Synced"),
    };
    QCOMPARE(pickSearchResult({ rFar }, q), std::nullopt);

    // 2. Synced lyrics is preferred over plain lyrics even if plain is closer in duration
    LrclibRecord rPlainClose {
        .id = 2,
        .trackName = QStringLiteral("Calling You"),
        .artistName = QStringLiteral("Jevetta Steele"),
        .albumName = QStringLiteral("Bagdad Cafe"),
        .durationSec = 200.1,
        .instrumental = false,
        .plainLyrics = QStringLiteral("Plain text"),
        .syncedLyrics = QStringLiteral(""),
    };
    LrclibRecord rSyncedFarther {
        .id = 3,
        .trackName = QStringLiteral("Calling You"),
        .artistName = QStringLiteral("Jevetta Steele"),
        .albumName = QStringLiteral("Bagdad Cafe"),
        .durationSec = 202.0,
        .instrumental = false,
        .plainLyrics = QStringLiteral("Plain text"),
        .syncedLyrics = QStringLiteral("[00:01.00] Synced text"),
    };
    const auto picked1 = pickSearchResult({ rPlainClose, rSyncedFarther }, q);
    QVERIFY(picked1.has_value());
    if (!picked1.has_value()) {
        return;
    }
    QCOMPARE(picked1.value().id, 3);

    // 3. Plain lyrics is preferred over instrumental
    LrclibRecord rInst {
        .id = 4,
        .trackName = QStringLiteral("Calling You"),
        .artistName = QStringLiteral("Jevetta Steele"),
        .albumName = QStringLiteral("Bagdad Cafe"),
        .durationSec = 200.0,
        .instrumental = true,
        .plainLyrics = QStringLiteral(""),
        .syncedLyrics = QStringLiteral(""),
    };
    const auto picked2 = pickSearchResult({ rInst, rPlainClose }, q);
    QVERIFY(picked2.has_value());
    if (!picked2.has_value()) {
        return;
    }
    QCOMPARE(picked2.value().id, 2);

    // 4. Same tier chooses closest duration
    LrclibRecord rSyncedCloser {
        .id = 5,
        .trackName = QStringLiteral("Calling You"),
        .artistName = QStringLiteral("Jevetta Steele"),
        .albumName = QStringLiteral("Bagdad Cafe"),
        .durationSec = 200.5,
        .instrumental = false,
        .plainLyrics = QStringLiteral(""),
        .syncedLyrics = QStringLiteral("[00:01.00] Closer"),
    };
    const auto picked3 = pickSearchResult({ rSyncedFarther, rSyncedCloser }, q);
    QVERIFY(picked3.has_value());
    if (!picked3.has_value()) {
        return;
    }
    QCOMPARE(picked3.value().id, 5);

    // 5. All unsuitable returns nullopt
    LrclibRecord rEmpty {
        .id = 6,
        .trackName = QStringLiteral("Calling You"),
        .artistName = QStringLiteral("Jevetta Steele"),
        .albumName = QStringLiteral("Bagdad Cafe"),
        .durationSec = 200.0,
        .instrumental = false,
        .plainLyrics = QStringLiteral(""),
        .syncedLyrics = QStringLiteral(""),
    };
    QCOMPARE(pickSearchResult({ rEmpty }, q), std::nullopt);
}

void TstLrclib::buildsLrclibQueryAndUrls()
{
    TrackRow track;
    track.title = QStringLiteral("Calling You (24bit/96kHz)");
    track.artist = QStringLiteral("Jevetta Steele / Bob Telson");
    track.album = QStringLiteral("Bagdad Cafe");
    track.durationMs = 320499; // rounds to 320s

    const LrclibQuery q = lrclibQueryFor(track);
    QCOMPARE(q.title, QStringLiteral("Calling You"));
    QCOMPARE(q.artist, QStringLiteral("Jevetta Steele"));
    QCOMPARE(q.album, QStringLiteral("Bagdad Cafe"));
    QCOMPARE(q.durationSec, 320);

    const QUrl getUrl = lrclibGetUrl(q);
    const QUrlQuery getQuery(getUrl);
    QCOMPARE(getQuery.queryItemValue(QStringLiteral("track_name")), QStringLiteral("Calling You"));
    QCOMPARE(
        getQuery.queryItemValue(QStringLiteral("artist_name")), QStringLiteral("Jevetta Steele"));
    QCOMPARE(getQuery.queryItemValue(QStringLiteral("album_name")), QStringLiteral("Bagdad Cafe"));
    QCOMPARE(getQuery.queryItemValue(QStringLiteral("duration")), QStringLiteral("320"));

    // Omit album and duration when empty/0
    LrclibQuery qMinimal {
        .title = QStringLiteral("Calling You"),
        .artist = QStringLiteral("Jevetta Steele"),
        .album = QStringLiteral(""),
        .durationSec = 0,
    };
    const QUrl getUrlMinimal = lrclibGetUrl(qMinimal);
    const QUrlQuery getQueryMinimal(getUrlMinimal);
    QVERIFY(!getQueryMinimal.hasQueryItem(QStringLiteral("album_name")));
    QVERIFY(!getQueryMinimal.hasQueryItem(QStringLiteral("duration")));

    // '+' 与 '%' 必须转义，否则服务器会把 '+' 解码成空格
    qMinimal.title = QStringLiteral("A+B 100%");
    const QByteArray encoded = lrclibGetUrl(qMinimal).toEncoded();
    QVERIFY(encoded.contains("track_name=A%2BB%20100%25"));

    const QUrl searchUrl = lrclibSearchUrl(q);
    const QUrlQuery searchQuery(searchUrl);
    QCOMPARE(
        searchQuery.queryItemValue(QStringLiteral("track_name")), QStringLiteral("Calling You"));
    QCOMPARE(searchQuery.queryItemValue(QStringLiteral("artist_name")),
        QStringLiteral("Jevetta Steele"));
    QVERIFY(!searchQuery.hasQueryItem(QStringLiteral("album_name")));
    QVERIFY(!searchQuery.hasQueryItem(QStringLiteral("duration")));
}

} // namespace

QTEST_GUILESS_MAIN(TstLrclib)

#include "tst_Lrclib.moc"
