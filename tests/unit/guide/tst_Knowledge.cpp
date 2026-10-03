// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonObject>
#include <QObject>
#include <QTest>
#include <QUrl>

#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>

namespace {

using linernotes::guide::KnowledgeEntry;
using linernotes::guide::knowledgeEntryFromJson;
using linernotes::guide::knowledgeEntryToJson;
using linernotes::guide::KnowledgeFact;
using linernotes::guide::knowledgeKey;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::KnowledgeQuery;
using linernotes::guide::primaryArtist;
using linernotes::guide::WikipediaSummary;

class TstKnowledge : public QObject {
    Q_OBJECT

private slots:
    void generatesKnowledgeKeyWithNormalization();
    void roundtripsKnowledgeEntryJson();
    void extractsPrimaryArtist();
};

void TstKnowledge::generatesKnowledgeKeyWithNormalization()
{
    // Artist normalization
    const KnowledgeQuery a1 {
        .kind = KnowledgeKind::Artist, .artist = QStringLiteral("Aimer"), .title = { }
    };
    const KnowledgeQuery a2 {
        .kind = KnowledgeKind::Artist, .artist = QStringLiteral("Ａｉｍｅｒ"), .title = { }
    };
    const KnowledgeQuery a3 {
        .kind = KnowledgeKind::Artist, .artist = QStringLiteral(" AIMER "), .title = { }
    };
    const KnowledgeQuery a4 {
        .kind = KnowledgeKind::Artist, .artist = QStringLiteral("Aimer / chelly"), .title = { }
    };
    const KnowledgeQuery a5 {
        .kind = KnowledgeKind::Artist, .artist = QStringLiteral("Aimer; chelly"), .title = { }
    };
    const KnowledgeQuery a6 {
        .kind = KnowledgeKind::Artist, .artist = QStringLiteral("Aimer、chelly"), .title = { }
    };

    QCOMPARE(knowledgeKey(a1), QStringLiteral("artist|aimer"));
    QCOMPARE(knowledgeKey(a2), QStringLiteral("artist|aimer"));
    QCOMPARE(knowledgeKey(a3), QStringLiteral("artist|aimer"));
    QCOMPARE(knowledgeKey(a4), QStringLiteral("artist|aimer"));
    QCOMPARE(knowledgeKey(a5), QStringLiteral("artist|aimer"));
    QCOMPARE(knowledgeKey(a6), QStringLiteral("artist|aimer"));

    // Song normalization: full-width, case, version suffix, multi-artist
    const KnowledgeQuery s1 {
        .kind = KnowledgeKind::Song,
        .artist = QStringLiteral("Aimer"),
        .title = QStringLiteral("Brave Shine"),
    };
    const KnowledgeQuery s2 {
        .kind = KnowledgeKind::Song,
        .artist = QStringLiteral("Ａｉｍｅｒ"),
        .title = QStringLiteral("Ｂｒａｖｅ　Ｓｈｉｎｅ"),
    };
    const KnowledgeQuery s3 {
        .kind = KnowledgeKind::Song,
        .artist = QStringLiteral("aimer / chelly"),
        .title = QStringLiteral("Brave Shine (TV size)"),
    };
    const KnowledgeQuery s4 {
        .kind = KnowledgeKind::Song,
        .artist = QStringLiteral("AIMER"),
        .title = QStringLiteral("Brave Shine [Live]"),
    };

    QCOMPARE(knowledgeKey(s1), QStringLiteral("song|aimer|brave shine"));
    QCOMPARE(knowledgeKey(s2), QStringLiteral("song|aimer|brave shine"));
    QCOMPARE(knowledgeKey(s3), QStringLiteral("song|aimer|brave shine"));
    QCOMPARE(knowledgeKey(s4), QStringLiteral("song|aimer|brave shine"));

    // Album normalization
    const KnowledgeQuery al1 {
        .kind = KnowledgeKind::Album,
        .artist = QStringLiteral("Aimer"),
        .title = QStringLiteral("DAWN"),
    };
    const KnowledgeQuery al2 {
        .kind = KnowledgeKind::Album,
        .artist = QStringLiteral("Ａｉｍｅｒ / chelly"),
        .title = QStringLiteral("ＤＡＷＮ (TV Size)"),
    };

    QCOMPARE(knowledgeKey(al1), QStringLiteral("album|aimer|dawn"));
    QCOMPARE(knowledgeKey(al2), QStringLiteral("album|aimer|dawn"));
}

void TstKnowledge::roundtripsKnowledgeEntryJson()
{
    KnowledgeEntry fullEntry;
    fullEntry.kind = KnowledgeKind::Song;
    fullEntry.qid = QStringLiteral("Q17988679");
    fullEntry.label = QStringLiteral("IGNITE");
    fullEntry.description = QStringLiteral("single by Eir Aoi");
    fullEntry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q17988679"));
    fullEntry.fetchedAtMs = 1700000000;
    fullEntry.summary = WikipediaSummary {
        .lang = QStringLiteral("ja"),
        .title = QStringLiteral("IGNITE (藍井エイルの曲)"),
        .extract = QStringLiteral("「IGNITE」は、藍井エイルの7枚目のシングル。"),
        .pageUrl = QUrl(QStringLiteral("https://ja.wikipedia.org/wiki/IGNITE")),
    };
    fullEntry.facts = {
        KnowledgeFact { .property = QStringLiteral("P175"), .value = QStringLiteral("藍井エイル") },
        KnowledgeFact { .property = QStringLiteral("P577"), .value = QStringLiteral("2014-08-20") },
    };

    const QJsonObject json = knowledgeEntryToJson(fullEntry);
    const auto parsed = knowledgeEntryFromJson(json);
    QVERIFY(parsed.has_value());
    if (!parsed.has_value()) {
        return;
    }
    QCOMPARE(parsed.value(), fullEntry);

    KnowledgeEntry minimalEntry;
    minimalEntry.kind = KnowledgeKind::Artist;
    minimalEntry.qid = QStringLiteral("Q4021857");
    minimalEntry.label = QStringLiteral("藍井エイル");
    minimalEntry.description = QStringLiteral("Japanese singer");
    minimalEntry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q4021857"));
    minimalEntry.fetchedAtMs = 1700000000;
    minimalEntry.summary = std::nullopt;
    minimalEntry.facts = { };

    const auto minParsed = knowledgeEntryFromJson(knowledgeEntryToJson(minimalEntry));
    QVERIFY(minParsed.has_value());
    if (!minParsed.has_value()) {
        return;
    }
    QCOMPARE(minParsed.value(), minimalEntry);

    QJsonObject invalidJson;
    invalidJson.insert(QStringLiteral("kind"), QStringLiteral("unknown"));
    QVERIFY(!knowledgeEntryFromJson(invalidJson).has_value());
}

void TstKnowledge::extractsPrimaryArtist()
{
    QCOMPARE(primaryArtist(QStringLiteral("Aimer / chelly")), QStringLiteral("Aimer"));
    QCOMPARE(primaryArtist(QStringLiteral("Aimer; chelly")), QStringLiteral("Aimer"));
    QCOMPARE(primaryArtist(QStringLiteral("Aimer、chelly")), QStringLiteral("Aimer"));
    QCOMPARE(primaryArtist(QStringLiteral("  Aimer  ")), QStringLiteral("Aimer"));
    QCOMPARE(primaryArtist(QString()), QString());
}

} // namespace

QTEST_GUILESS_MAIN(TstKnowledge)

#include "tst_Knowledge.moc"
