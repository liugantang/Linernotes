// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include <butler/ArtistGroup.h>
#include <butler/ArtistMerge.h>
#include <butler/MusicBrainz.h>
#include <common/TestSupport.h>
#include <library/ArtistAliasCorrections.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::adoptMbArtist;
using linernotes::butler::ArtistEntry;
using linernotes::butler::ArtistGroup;
using linernotes::butler::groupProposals;
using linernotes::butler::musicBrainzAliasProposals;
using linernotes::butler::parseArtistSearch;
using linernotes::butler::pickCanonical;
using linernotes::library::CorrectionSource;

class TstArtistMerge : public QObject {
    Q_OBJECT

private slots:
    void pickCanonicalPrefersMoreTracks();
    void groupProposalsCreatesRuleProposals();
    void adoptMbArtistMatches();
    void musicBrainzAliasProposalsGeneratesAliases();
};

void TstArtistMerge::pickCanonicalPrefersMoreTracks()
{
    // Case 1: Higher track count wins
    const ArtistEntry e1 { .artistId = 1, .name = QStringLiteral("Artist B"), .trackCount = 5 };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("Artist A"), .trackCount = 10 };
    QCOMPARE(pickCanonical({ e1, e2 }), 2);

    // Case 2: Same track count -> smaller name string wins
    const ArtistEntry e3 { .artistId = 3, .name = QStringLiteral("Beta"), .trackCount = 8 };
    const ArtistEntry e4 { .artistId = 4, .name = QStringLiteral("Alpha"), .trackCount = 8 };
    QCOMPARE(pickCanonical({ e3, e4 }), 4);

    // Case 3: Same track count and same name -> smaller artistId wins
    const ArtistEntry e5 { .artistId = 10, .name = QStringLiteral("Same"), .trackCount = 3 };
    const ArtistEntry e6 { .artistId = 5, .name = QStringLiteral("Same"), .trackCount = 3 };
    QCOMPARE(pickCanonical({ e5, e6 }), 5);

    // Case 4: Empty list returns 0
    QCOMPARE(pickCanonical({ }), 0);
}

void TstArtistMerge::groupProposalsCreatesRuleProposals()
{
    const ArtistEntry m1 { .artistId = 1, .name = QStringLiteral("Jay Chou"), .trackCount = 20 };
    const ArtistEntry m2 { .artistId = 2, .name = QStringLiteral("jay chou"), .trackCount = 2 };
    const ArtistGroup exactGroup {
        .members = { m1, m2 },
        .exactOnly = true,
    };

    const auto proposals = groupProposals(exactGroup);
    QCOMPARE(proposals.size(), 1);
    const auto &p = proposals.first();
    QCOMPARE(p.canonicalArtistId, 1);
    QCOMPARE(p.alias, QStringLiteral("jay chou"));
    QVERIFY(!p.locale.has_value());
    QCOMPARE(p.source, CorrectionSource::Rule);
    QCOMPARE(p.confidence, 0.95);
    QCOMPARE(p.reason, QStringLiteral("Same name with different spelling"));
}

void TstArtistMerge::adoptMbArtistMatches()
{
    const QString mimoriPath
        = linernotes::test::fixturePath(QStringLiteral("musicbrainz/artist_search_mimori.json"));
    QFile file(mimoriPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto parseRes = parseArtistSearch(file.readAll());
    QVERIFY(parseRes.ok());
    const auto &mbResults = parseRes.value();

    // 1. Direct name match
    const auto adoptedByName = adoptMbArtist(QStringLiteral("三森すずこ"), mbResults);
    QVERIFY(adoptedByName.has_value());
    if (adoptedByName.has_value()) {
        QCOMPARE(adoptedByName->name, QStringLiteral("三森すずこ"));
    }

    // 2. Alias match
    const auto adoptedByAlias = adoptMbArtist(QStringLiteral("Suzuko Mimori"), mbResults);
    QVERIFY(adoptedByAlias.has_value());
    if (adoptedByAlias.has_value()) {
        QCOMPARE(adoptedByAlias->name, QStringLiteral("三森すずこ"));
    }

    // 3. Score too low (< 90) -> nullopt (fixture has 中森明菜 with score 80)
    const auto adoptedLowScore = adoptMbArtist(QStringLiteral("中森明菜"), mbResults);
    QVERIFY(!adoptedLowScore.has_value());

    // 4. Unknown name -> nullopt
    const auto adoptedUnknown = adoptMbArtist(QStringLiteral("Unknown Artist"), mbResults);
    QVERIFY(!adoptedUnknown.has_value());
}

void TstArtistMerge::musicBrainzAliasProposalsGeneratesAliases()
{
    const QString mimoriPath
        = linernotes::test::fixturePath(QStringLiteral("musicbrainz/artist_search_mimori.json"));
    QFile file(mimoriPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto parseRes = parseArtistSearch(file.readAll());
    QVERIFY(parseRes.ok());
    const auto &mbResults = parseRes.value();

    const auto adopted = adoptMbArtist(QStringLiteral("三森すずこ"), mbResults);
    QVERIFY(adopted.has_value());
    if (!adopted.has_value()) {
        return;
    }

    const ArtistEntry mimori {
        .artistId = 1, .name = QStringLiteral("三森すずこ"), .trackCount = 20
    };

    // Case A: Library doesn't have "Suzuko Mimori" -> proposal emitted
    {
        const QSet<QString> libraryNames = { QStringLiteral("三森すずこ") };
        const auto proposals = musicBrainzAliasProposals(mimori, adopted.value(), libraryNames);
        QCOMPARE(proposals.size(), 1);

        const auto &p = proposals.first();
        QCOMPARE(p.canonicalArtistId, 1);
        QCOMPARE(p.alias, QStringLiteral("Suzuko Mimori"));
        QCOMPARE(p.locale, std::optional<QString>(QStringLiteral("en")));
        QCOMPARE(p.source, CorrectionSource::MusicBrainz);
        QCOMPARE(p.confidence, 0.9);
        QCOMPARE(p.reason, QStringLiteral("MusicBrainz localized alias"));
    }

    // Case B: Library already has "Suzuko Mimori" -> skipped
    {
        const QSet<QString> libraryNames
            = { QStringLiteral("三森すずこ"), QStringLiteral("Suzuko Mimori") };
        const auto proposals = musicBrainzAliasProposals(mimori, adopted.value(), libraryNames);
        QCOMPARE(proposals.size(), 0);
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistMerge)

#include "tst_ArtistMerge.moc"
