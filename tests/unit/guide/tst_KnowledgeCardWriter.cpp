// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTest>

#include <guide/Knowledge.h>
#include <guide/KnowledgeCardWriter.h>
#include <guide/Wikidata.h>

namespace {

using linernotes::guide::factPropertyName;
using linernotes::guide::knowledgeCardPromptVars;
using linernotes::guide::KnowledgeEntry;
using linernotes::guide::KnowledgeFact;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::WikipediaSummary;

class TstKnowledgeCardWriter : public QObject {
    Q_OBJECT

private slots:
    void promptVarsWithSummaryAndFacts();
    void promptVarsWithoutSummary();
    void promptVarsDifferentKinds();
};

void TstKnowledgeCardWriter::promptVarsWithSummaryAndFacts()
{
    KnowledgeEntry entry {
        .kind = KnowledgeKind::Song,
        .qid = QStringLiteral("Q17988679"),
        .label = QStringLiteral("IGNITE"),
        .description = QStringLiteral("single by Eir Aoi"),
        .wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q17988679")),
        .summary = WikipediaSummary {
            .lang = QStringLiteral("zh"),
            .title = QStringLiteral("IGNITE"),
            .extract = QStringLiteral("《IGNITE》是藍井艾露的第7張單曲。"),
            .pageUrl = QUrl(QStringLiteral("https://zh.wikipedia.org/wiki/IGNITE")),
        },
        .facts = {
            KnowledgeFact {
                .property = QStringLiteral("P577"),
                .value = QStringLiteral("2014-08-20"),
            },
            KnowledgeFact {
                .property = QStringLiteral("P175"),
                .value = QStringLiteral("藍井艾露"),
            },
        },
        .fetchedAtMs = 12345678,
    };

    const auto vars = knowledgeCardPromptVars(entry);
    QCOMPARE(vars.value(QStringLiteral("kind")), QStringLiteral("歌曲"));
    QCOMPARE(vars.value(QStringLiteral("name")), QStringLiteral("IGNITE"));
    QCOMPARE(
        vars.value(QStringLiteral("extract")), QStringLiteral("《IGNITE》是藍井艾露的第7張單曲。"));
    QCOMPARE(vars.value(QStringLiteral("extract_lang")), QStringLiteral("zh"));

    const QString factsVal = vars.value(QStringLiteral("facts"));
    const QString expectedP577Line
        = factPropertyName(QStringLiteral("P577")) + QStringLiteral("：2014-08-20");
    const QString expectedP175Line
        = factPropertyName(QStringLiteral("P175")) + QStringLiteral("：藍井艾露");

    QVERIFY(factsVal.contains(expectedP577Line));
    QVERIFY(factsVal.contains(expectedP175Line));
    QCOMPARE(factsVal, expectedP577Line + QLatin1Char('\n') + expectedP175Line);
}

void TstKnowledgeCardWriter::promptVarsWithoutSummary()
{
    KnowledgeEntry entry {
        .kind = KnowledgeKind::Artist,
        .qid = QStringLiteral("Q4021857"),
        .label = QStringLiteral("Eir Aoi"),
        .description = QStringLiteral("Japanese singer"),
        .wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q4021857")),
        .summary = std::nullopt,
        .facts = { },
        .fetchedAtMs = 0,
    };

    const auto vars = knowledgeCardPromptVars(entry);
    QCOMPARE(vars.value(QStringLiteral("kind")), QStringLiteral("艺人"));
    QCOMPARE(vars.value(QStringLiteral("name")), QStringLiteral("Eir Aoi"));
    QCOMPARE(vars.value(QStringLiteral("extract")), QString());
    QCOMPARE(vars.value(QStringLiteral("extract_lang")), QString());
    QCOMPARE(vars.value(QStringLiteral("facts")), QString());
}

void TstKnowledgeCardWriter::promptVarsDifferentKinds()
{
    KnowledgeEntry albumEntry {
        .kind = KnowledgeKind::Album,
        .qid = { },
        .label = QStringLiteral("D'AZUR"),
        .description = { },
        .wikidataUrl = { },
        .summary = std::nullopt,
        .facts = { },
        .fetchedAtMs = 0,
    };
    const auto albumVars = knowledgeCardPromptVars(albumEntry);
    QCOMPARE(albumVars.value(QStringLiteral("kind")), QStringLiteral("专辑"));
}

} // namespace

QTEST_GUILESS_MAIN(TstKnowledgeCardWriter)

#include "tst_KnowledgeCardWriter.moc"
