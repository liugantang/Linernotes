// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <butler/Errors.h>
#include <butler/MusicBrainz.h>
#include <butler/MusicBrainzClient.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::artistSearchUrl;
using linernotes::butler::MbArtist;
using linernotes::butler::MbSearchTask;
using linernotes::butler::MusicBrainzClient;
using linernotes::butler::parseArtistSearch;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
namespace errc = linernotes::butler::errc;

class TstMusicBrainz : public QObject {
    Q_OBJECT

private slots:
    void parseRealResponses();
    void parseRejectsInvalid();
    void urlEscapesQuery_data();
    void urlEscapesQuery();
    void cacheHitReturnsWithoutNetwork();
};

void TstMusicBrainz::parseRealResponses()
{
    // 1. Mimori Suzuko
    {
        const QString mimoriPath = linernotes::test::fixturePath(
            QStringLiteral("musicbrainz/artist_search_mimori.json"));
        QFile file(mimoriPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();

        const auto res = parseArtistSearch(data);
        QVERIFY(res.ok());
        const auto &artists = res.value();
        QCOMPARE(artists.size(), 3);

        const auto &first = artists.at(0);
        QCOMPARE(first.mbid, QStringLiteral("316aa1f1-8680-427b-b4c4-fa12bffd107c"));
        QCOMPARE(first.name, QStringLiteral("三森すずこ"));
        QCOMPARE(first.sortName, QStringLiteral("Mimori, Suzuko"));
        QCOMPARE(first.type, QStringLiteral("Person"));
        QCOMPARE(first.country, QStringLiteral("JP"));
        QCOMPARE(first.disambiguation, QString());
        QCOMPARE(first.score, 100);
        QCOMPARE(first.aliases.size(), 2);

        // Alias 1: Suzuko Mimori (en, primary)
        const auto &aliasEn = first.aliases.at(0);
        QCOMPARE(aliasEn.name, QStringLiteral("Suzuko Mimori"));
        QCOMPARE(aliasEn.sortName, QStringLiteral("Mimori, Suzuko"));
        QCOMPARE(aliasEn.locale, QStringLiteral("en"));
        QVERIFY(aliasEn.primary);

        // Alias 2: 三森すずこ (ja, primary)
        const auto &aliasJa = first.aliases.at(1);
        QCOMPARE(aliasJa.name, QStringLiteral("三森すずこ"));
        QCOMPARE(aliasJa.sortName, QStringLiteral("みもり すずこ"));
        QCOMPARE(aliasJa.locale, QStringLiteral("ja"));
        QVERIFY(aliasJa.primary);
    }

    // 2. Jay Chou
    {
        const QString jayPath = linernotes::test::fixturePath(
            QStringLiteral("musicbrainz/artist_search_jaychou.json"));
        QFile file(jayPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();

        const auto res = parseArtistSearch(data);
        QVERIFY(res.ok());
        const auto &artists = res.value();
        QCOMPARE(artists.size(), 3);

        const auto &first = artists.at(0);
        QCOMPARE(first.mbid, QStringLiteral("a223958d-5c56-4b2c-a30a-87e357bc121b"));
        QCOMPARE(first.name, QStringLiteral("周杰倫"));
        QCOMPARE(first.sortName, QStringLiteral("Chou, Jay"));
        QCOMPARE(first.type, QStringLiteral("Person"));
        QCOMPARE(first.country, QStringLiteral("TW"));
        QCOMPARE(first.score, 100);

        // Check zh_Hans alias: 周杰伦
        bool foundZhHans = false;
        for (const auto &alias : first.aliases) {
            if (alias.locale == QStringLiteral("zh_Hans")) {
                QCOMPARE(alias.name, QStringLiteral("周杰伦"));
                QVERIFY(alias.primary);
                foundZhHans = true;
                break;
            }
        }
        QVERIFY(foundZhHans);
    }
}

void TstMusicBrainz::parseRejectsInvalid()
{
    const auto resArray = parseArtistSearch(QByteArray("[]"));
    QVERIFY(!resArray.ok());
    QCOMPARE(resArray.error().code, QString(errc::kMbInvalidResponse));

    const auto resObject = parseArtistSearch(QByteArray("{\"x\":1}"));
    QVERIFY(!resObject.ok());
    QCOMPARE(resObject.error().code, QString(errc::kMbInvalidResponse));
}

void TstMusicBrainz::urlEscapesQuery_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expectedQuery");

    QTest::newRow("ac_dc") << QStringLiteral("AC/DC") << QStringLiteral("artist:\"AC\\/DC\"");
    QTest::newRow("mayn") << QStringLiteral("May'n") << QStringLiteral("artist:\"May'n\"");
    QTest::newRow("pastel_palettes")
        << QStringLiteral("Pastel*Palettes") << QStringLiteral("artist:\"Pastel\\*Palettes\"");
    QTest::newRow("mimori") << QStringLiteral("三森すずこ")
                            << QStringLiteral("artist:\"三森すずこ\"");
}

void TstMusicBrainz::urlEscapesQuery()
{
    QFETCH(QString, name);
    QFETCH(QString, expectedQuery);

    const QUrl url = artistSearchUrl(name);
    const QString actualQuery
        = QUrlQuery(url).queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded);
    QCOMPARE(actualQuery, expectedQuery);
}

void TstMusicBrainz::cacheHitReturnsWithoutNetwork()
{
    const QString mimoriPath
        = linernotes::test::fixturePath(QStringLiteral("musicbrainz/artist_search_mimori.json"));
    QFile file(mimoriPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray mimoriData = file.readAll();

    const auto directParseRes = parseArtistSearch(mimoriData);
    QVERIFY(directParseRes.ok());

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test_mb_cache.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());

    ManualClock clock(500000);
    const QUrl searchUrl = artistSearchUrl(QStringLiteral("三森すずこ"));

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("INSERT INTO mb_cache (url, body, fetched_at) VALUES (?, ?, ?);"));
    q.addBindValue(searchUrl.toString());
    q.addBindValue(QString::fromUtf8(mimoriData));
    q.addBindValue(clock.nowMs());
    QVERIFY(q.exec());

    QNetworkAccessManager nam;
    MusicBrainzClient client(nam, db, clock);

    auto task = client.searchArtist(QStringLiteral("三森すずこ"));
    QVERIFY(task != nullptr);

    QSignalSpy spy(task.get(), &MbSearchTask::finished);
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(task->isFinished());
    const auto &taskRes = task->result();
    QVERIFY(taskRes.ok());
    QCOMPARE(taskRes.value(), directParseRes.value());
}

} // namespace

QTEST_GUILESS_MAIN(TstMusicBrainz)

#include "tst_MusicBrainz.moc"
