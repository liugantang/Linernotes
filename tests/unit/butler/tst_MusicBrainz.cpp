// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <butler/Errors.h>
#include <butler/MusicBrainz.h>
#include <butler/MusicBrainzClient.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/Migrator.h>

#include <memory>
#include <optional>

namespace {

using linernotes::butler::luceneEscape;
using linernotes::butler::MbFetch;
using linernotes::butler::MusicBrainzClient;
using linernotes::butler::parseRecordingSearch;
using linernotes::butler::parseRelease;
using linernotes::butler::parseReleaseSearch;
using linernotes::butler::recordingSearchUrl;
using linernotes::butler::releaseSearchUrl;
using linernotes::butler::releaseUrl;
using linernotes::butler::yearFromDate;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
namespace errc = linernotes::butler::errc;

QByteArray readFixture(const QString &relativePath)
{
    const QString fullPath = linernotes::test::fixturePath(relativePath);
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

class TstMusicBrainz : public QObject {
    Q_OBJECT

private slots:
    void parseReleaseSearchRealFixture();
    void parseReleaseFlowerflowerFixture();
    void parseReleaseKalafinaFixture();
    void parseRecordingSearchDaokoFixture();
    void parseRejectsInvalid();
    void yearFromDateHelper();
    void luceneEscapeHelper();
    void urlBuilders();
    void cacheHitReturnsWithoutNetwork();
};

void TstMusicBrainz::parseReleaseSearchRealFixture()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_search_flowerflower.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseReleaseSearch(json);
    QVERIFY(res.ok());
    const auto &summaries = res.value();
    QCOMPARE(summaries.size(), 5);

    // 第一条
    const auto &first = summaries.at(0);
    QCOMPARE(first.id, QStringLiteral("11ade0fa-6831-4169-830b-baf5b827878e"));
    QCOMPARE(first.score, 100);
    QCOMPARE(first.title, QStringLiteral("宝物"));
    QCOMPARE(first.artist, QStringLiteral("FLOWER FLOWER"));
    QCOMPARE(first.date, QStringLiteral("2016-09-07"));
    QCOMPARE(first.country, QStringLiteral("JP"));
    QCOMPARE(first.trackCount, 5);
    QCOMPARE(first.discCount, 1);
    const QStringList expectedFirstFmt { QStringLiteral("Digital Media") };
    QCOMPARE(first.formats, expectedFirstFmt);
    QCOMPARE(first.releaseGroupId, QStringLiteral("ffb76de5-227e-4acb-9b6f-9bfc292433f8"));
    QCOMPARE(first.primaryType, QStringLiteral("Single"));

    // CD 版（第四条，id: 3919ba5a-ae75-42e6-a47a-0c7c3af6abdc）
    const auto &cdRelease = summaries.at(3);
    QCOMPARE(cdRelease.id, QStringLiteral("3919ba5a-ae75-42e6-a47a-0c7c3af6abdc"));
    QCOMPARE(cdRelease.discCount, 2);
    const QStringList expectedCdFormats { QStringLiteral("CD"), QStringLiteral("CD") };
    QCOMPARE(cdRelease.formats, expectedCdFormats);
    QCOMPARE(cdRelease.primaryType, QStringLiteral("Single"));
}

void TstMusicBrainz::parseReleaseFlowerflowerFixture()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &rel = res.value();

    QCOMPARE(rel.id, QStringLiteral("11ade0fa-6831-4169-830b-baf5b827878e"));
    QCOMPARE(rel.title, QStringLiteral("宝物"));
    QCOMPARE(rel.artist, QStringLiteral("FLOWER FLOWER"));
    QCOMPARE(rel.date, QStringLiteral("2016-09-07"));
    QCOMPARE(rel.country, QStringLiteral("JP"));
    QCOMPARE(rel.originalDate, QStringLiteral("2016-09-07"));
    QCOMPARE(rel.releaseGroupId, QStringLiteral("ffb76de5-227e-4acb-9b6f-9bfc292433f8"));
    QCOMPARE(rel.primaryType, QStringLiteral("Single"));

    // label 为 null 时不报错，labels 为空
    QVERIFY(rel.labels.isEmpty());
    QCOMPARE(rel.discCount, 1);
    QCOMPARE(rel.tracks.size(), 5);

    // 第 1 首
    const auto &t1 = rel.tracks.at(0);
    QCOMPARE(t1.disc, 1);
    QCOMPARE(t1.position, 1);
    QCOMPARE(t1.number, QStringLiteral("1"));
    QCOMPARE(t1.title, QStringLiteral("宝物"));
    QCOMPARE(t1.artist, QStringLiteral("FLOWER FLOWER"));
    QCOMPARE(t1.lengthMs, 292000);
    QCOMPARE(t1.recordingId, QStringLiteral("ebe94de6-4d51-447c-8c27-8efd751a0eaa"));
}

void TstMusicBrainz::parseReleaseKalafinaFixture()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_kalafina_best_3cd.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRelease(json);
    QVERIFY(res.ok());
    const auto &rel = res.value();

    QCOMPARE(rel.id, QStringLiteral("81b8318a-797e-425b-ad3a-f2681f610959"));
    QCOMPARE(rel.title, QStringLiteral("Kalafina All Time Best 2008–2018"));
    QCOMPARE(rel.artist, QStringLiteral("Kalafina"));
    QCOMPARE(rel.discCount, 3);
    QCOMPARE(rel.tracks.size(), 36);

    // 第二张碟第一首（索引 12）
    const auto &disc2Track1 = rel.tracks.at(12);
    QCOMPARE(disc2Track1.disc, 2);
    QCOMPARE(disc2Track1.position, 1);
    QCOMPARE(disc2Track1.number, QStringLiteral("1"));
    QCOMPARE(disc2Track1.title, QStringLiteral("I have a dream"));
    QCOMPARE(disc2Track1.artist, QStringLiteral("Kalafina"));

    // 厂牌去重保序
    const QStringList expectedLabels { QStringLiteral("SACRA MUSIC") };
    QCOMPARE(rel.labels, expectedLabels);

    QCOMPARE(rel.primaryType, QStringLiteral("Album"));
    QCOMPARE(rel.originalDate, QStringLiteral("2018-10-24"));
}

void TstMusicBrainz::parseRecordingSearchDaokoFixture()
{
    const QByteArray json = readFixture(QStringLiteral("musicbrainz/recording_search_daoko.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseRecordingSearch(json);
    QVERIFY(res.ok());
    const auto &hits = res.value();
    QCOMPARE(hits.size(), 1);

    const auto &hit = hits.at(0);
    QCOMPARE(hit.id, QStringLiteral("d065cdfb-762c-4e74-a073-879b18389f2c"));
    QCOMPARE(hit.score, 100);
    QCOMPARE(hit.title, QStringLiteral("拝啓グッバイさようなら"));
    QCOMPARE(hit.artist, QStringLiteral("DAOKO"));
    QCOMPARE(hit.lengthMs, 226000);
    QVERIFY(!hit.releases.isEmpty());
    QCOMPARE(hit.releases.at(0).title, QStringLiteral("THANK YOU BLUE"));
}

void TstMusicBrainz::parseRejectsInvalid()
{
    // release search
    {
        const auto emptyRes = parseReleaseSearch(QByteArray());
        QVERIFY(!emptyRes.ok());
        QCOMPARE(emptyRes.error().code, QString(errc::kMbInvalidResponse));

        const auto arrRes = parseReleaseSearch(QByteArray("[]"));
        QVERIFY(!arrRes.ok());
        QCOMPARE(arrRes.error().code, QString(errc::kMbInvalidResponse));

        const auto objRes = parseReleaseSearch(QByteArray("{}"));
        QVERIFY(!objRes.ok());
        QCOMPARE(objRes.error().code, QString(errc::kMbInvalidResponse));
    }

    // release detail
    {
        const auto emptyRes = parseRelease(QByteArray());
        QVERIFY(!emptyRes.ok());
        QCOMPARE(emptyRes.error().code, QString(errc::kMbInvalidResponse));

        const auto arrRes = parseRelease(QByteArray("[]"));
        QVERIFY(!arrRes.ok());
        QCOMPARE(arrRes.error().code, QString(errc::kMbInvalidResponse));

        const auto objRes = parseRelease(QByteArray("{}"));
        QVERIFY(!objRes.ok());
        QCOMPARE(objRes.error().code, QString(errc::kMbInvalidResponse));
    }

    // recording search
    {
        const auto emptyRes = parseRecordingSearch(QByteArray());
        QVERIFY(!emptyRes.ok());
        QCOMPARE(emptyRes.error().code, QString(errc::kMbInvalidResponse));

        const auto arrRes = parseRecordingSearch(QByteArray("[]"));
        QVERIFY(!arrRes.ok());
        QCOMPARE(arrRes.error().code, QString(errc::kMbInvalidResponse));

        const auto objRes = parseRecordingSearch(QByteArray("{}"));
        QVERIFY(!objRes.ok());
        QCOMPARE(objRes.error().code, QString(errc::kMbInvalidResponse));
    }
}

void TstMusicBrainz::yearFromDateHelper()
{
    QCOMPARE(yearFromDate(QStringLiteral("2016-09-07")), std::optional<int>(2016));
    QCOMPARE(yearFromDate(QStringLiteral("2016")), std::optional<int>(2016));
    QCOMPARE(yearFromDate(QStringLiteral("2016-09")), std::optional<int>(2016));
    QCOMPARE(yearFromDate(QStringLiteral("")), std::nullopt);
    QCOMPARE(yearFromDate(QStringLiteral("   ")), std::nullopt);
    QCOMPARE(yearFromDate(QStringLiteral("invalid")), std::nullopt);
    QCOMPARE(yearFromDate(QStringLiteral("abcd-01-01")), std::nullopt);
}

void TstMusicBrainz::luceneEscapeHelper()
{
    const QString raw = QStringLiteral("AC/DC: \"Live\" && [Rock] || + - ! ( ) { } ^ ~ * ? \\");
    const QString escaped = luceneEscape(raw);
    const QString expected
        = QStringLiteral("AC\\/DC\\: \\\"Live\\\" \\&\\& \\[Rock\\] \\|\\| \\+ \\- \\! \\( \\) "
                         "\\{ \\} \\^ \\~ \\* \\? \\\\");
    QCOMPARE(escaped, expected);
}

void TstMusicBrainz::urlBuilders()
{
    // releaseSearchUrl 含 artist
    {
        const QUrl url
            = releaseSearchUrl(QStringLiteral("宝物"), QStringLiteral("FLOWER FLOWER"), 10);
        QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("fmt")), QStringLiteral("json"));
        QCOMPARE(query.queryItemValue(QStringLiteral("limit")), QStringLiteral("10"));
        QCOMPARE(query.queryItemValue(QStringLiteral("query")),
            QStringLiteral("release:(宝物) AND artist:(FLOWER FLOWER)"));
    }

    // releaseSearchUrl 不含 artist
    {
        const QUrl url = releaseSearchUrl(QStringLiteral("宝物"), QString(), 5);
        QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("fmt")), QStringLiteral("json"));
        QCOMPARE(query.queryItemValue(QStringLiteral("limit")), QStringLiteral("5"));
        QCOMPARE(query.queryItemValue(QStringLiteral("query")), QStringLiteral("release:(宝物)"));
        QVERIFY(!query.queryItemValue(QStringLiteral("query")).contains(QStringLiteral("AND")));
    }

    // releaseUrl
    {
        const QUrl url = releaseUrl(QStringLiteral("11ade0fa-6831-4169-830b-baf5b827878e"));
        QCOMPARE(url.path(), QStringLiteral("/ws/2/release/11ade0fa-6831-4169-830b-baf5b827878e"));
        QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("inc")),
            QStringLiteral("recordings+artist-credits+labels+release-groups"));
        QCOMPARE(query.queryItemValue(QStringLiteral("fmt")), QStringLiteral("json"));
    }

    // recordingSearchUrl 含/不含 artist
    {
        const QUrl url = recordingSearchUrl(
            QStringLiteral("拝啓グッバイさようなら"), QStringLiteral("DAOKO"), 10);
        QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("query")),
            QStringLiteral("recording:(拝啓グッバイさようなら) AND artist:(DAOKO)"));

        const QUrl noArt
            = recordingSearchUrl(QStringLiteral("拝啓グッバイさようなら"), QString(), 10);
        QUrlQuery qNoArt(noArt);
        QCOMPARE(qNoArt.queryItemValue(QStringLiteral("query")),
            QStringLiteral("recording:(拝啓グッバイさようなら)"));
        QVERIFY(!qNoArt.queryItemValue(QStringLiteral("query")).contains(QStringLiteral("AND")));
    }
}

void TstMusicBrainz::cacheHitReturnsWithoutNetwork()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const Migrator migrator;
    const auto migRes = migrator.migrate(conn);
    QVERIFY(migRes.ok());

    const QByteArray fixtureBody
        = readFixture(QStringLiteral("musicbrainz/release_search_flowerflower.json"));
    QVERIFY(!fixtureBody.isEmpty());

    const ManualClock clock(1000000);
    const QUrl url = releaseSearchUrl(QStringLiteral("宝物"), QStringLiteral("FLOWER FLOWER"));

    // 写入 mb_cache
    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO mb_cache (url, body, fetched_at) VALUES (?, ?, ?);"));
    q.addBindValue(url.toString());
    q.addBindValue(QString::fromUtf8(fixtureBody));
    q.addBindValue(clock.nowMs());
    QVERIFY(q.exec());

    QNetworkAccessManager network;
    MusicBrainzClient client(network, db, clock);

    // 1. cached() 读取
    const auto cachedOpt = client.cached(url);
    QVERIFY(cachedOpt.has_value());
    if (cachedOpt.has_value()) {
        QCOMPARE(*cachedOpt, fixtureBody);
    }

    const QUrl uncachedUrl
        = releaseSearchUrl(QStringLiteral("NotCached"), QStringLiteral("Nobody"));
    QVERIFY(!client.cached(uncachedUrl).has_value());

    // 2. get() 异步命中缓存
    const auto fetch = client.get(url);
    QVERIFY(fetch != nullptr);
    QVERIFY(!fetch->isFinished());

    QSignalSpy spy(fetch.get(), &MbFetch::finished);
    QTRY_COMPARE(spy.count(), 1);

    QVERIFY(fetch->isFinished());
    QVERIFY(fetch->fromCache());
    QVERIFY(fetch->result().ok());
    QCOMPARE(fetch->result().value(), fixtureBody);
}

} // namespace

QTEST_GUILESS_MAIN(TstMusicBrainz)

#include "tst_MusicBrainz.moc"
