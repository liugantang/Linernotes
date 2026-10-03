// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QTest>

#include <common/TestSupport.h>
#include <guide/Wikidata.h>
#include <guide/Wikipedia.h>

namespace {

using linernotes::guide::parseWikidataEntities;
using linernotes::guide::parseWikipediaSummary;
using linernotes::guide::preferredSitelink;
using linernotes::guide::wikipediaSummaryUrl;
using linernotes::test::fixturePath;

QByteArray readFixture(const QString &relativePath)
{
    QFile file(fixturePath(relativePath));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

class TstWikipedia : public QObject {
    Q_OBJECT

private slots:
    void parsesSummaryZh();
    void parsesSummaryJa();
    void parsesDisambiguationReturnsError();
    void parsesInvalidJsonReturnsError();
    void prefersSitelink();
    void formatsSummaryUrlWithUnderscores();
};

void TstWikipedia::parsesSummaryZh()
{
    const QByteArray json = readFixture(QStringLiteral("guide/wp_summary_zh_aoi_eir.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseWikipediaSummary(json, QStringLiteral("zh"));
    QVERIFY(res.ok());

    const auto &summary = res.value();
    QCOMPARE(summary.lang, QStringLiteral("zh"));
    QCOMPARE(summary.title, QStringLiteral("藍井艾露"));
    QVERIFY(!summary.extract.isEmpty());
    QCOMPARE(summary.pageUrl.toEncoded(),
        QByteArray("https://zh.wikipedia.org/wiki/%E8%97%8D%E4%BA%95%E8%89%BE%E9%9C%B2"));
}

void TstWikipedia::parsesSummaryJa()
{
    const QByteArray json = readFixture(QStringLiteral("guide/wp_summary_ja_ignite.json"));
    QVERIFY(!json.isEmpty());

    const auto res = parseWikipediaSummary(json, QStringLiteral("ja"));
    QVERIFY(res.ok());

    const auto &summary = res.value();
    QCOMPARE(summary.lang, QStringLiteral("ja"));
    QCOMPARE(summary.title, QStringLiteral("IGNITE (曲)"));
    QVERIFY(!summary.extract.isEmpty());
    QCOMPARE(summary.pageUrl.toEncoded(),
        QByteArray("https://ja.wikipedia.org/wiki/IGNITE_(%E6%9B%B2)"));
}

void TstWikipedia::parsesDisambiguationReturnsError()
{
    const QByteArray json = R"({
        "type": "disambiguation",
        "title": "Ignite",
        "extract": "Ignite may refer to..."
    })";

    const auto res = parseWikipediaSummary(json, QStringLiteral("en"));
    QVERIFY(!res.ok());
}

void TstWikipedia::parsesInvalidJsonReturnsError()
{
    const auto res = parseWikipediaSummary("not json", QStringLiteral("en"));
    QVERIFY(!res.ok());
}

void TstWikipedia::prefersSitelink()
{
    const QByteArray json = readFixture(QStringLiteral("guide/wd_entities_artist_aoi_eir.json"));
    QVERIFY(!json.isEmpty());

    const auto entitiesRes = parseWikidataEntities(json);
    QVERIFY(entitiesRes.ok());

    const auto &artist = entitiesRes.value().value(QStringLiteral("Q4021857"));
    const auto sitelink = preferredSitelink(artist);
    QVERIFY(sitelink.has_value());
    if (sitelink.has_value()) {
        QCOMPARE(sitelink->first, QStringLiteral("zh"));
        QCOMPARE(sitelink->second, QStringLiteral("藍井艾露"));
    }
}

void TstWikipedia::formatsSummaryUrlWithUnderscores()
{
    const QUrl url = wikipediaSummaryUrl(QStringLiteral("ja"), QStringLiteral("IGNITE (曲)"));
    QCOMPARE(url.host(), QStringLiteral("ja.wikipedia.org"));
    QVERIFY(!url.toString().contains(QLatin1Char(' ')));
    QVERIFY(url.path().endsWith(QStringLiteral("IGNITE_(曲)")));
    QCOMPARE(url.toEncoded(),
        QByteArray("https://ja.wikipedia.org/api/rest_v1/page/summary/IGNITE_%28%E6%9B%B2%29"));
}

} // namespace

QTEST_GUILESS_MAIN(TstWikipedia)
#include "tst_Wikipedia.moc"
