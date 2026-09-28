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

using linernotes::butler::ArtistEntry;
using linernotes::butler::ArtistGroup;
using linernotes::butler::groupProposals;
using linernotes::butler::musicBrainzProposals;
using linernotes::butler::parseArtistSearch;
using linernotes::butler::pickCanonical;
using linernotes::library::CorrectionSource;

class TstArtistMerge : public QObject {
    Q_OBJECT

private slots:
    void pickCanonicalPrefersMoreTracks();
    void groupProposalsCreatesRuleProposals();
    void musicBrainzMergesAndLocales();
    void musicBrainzIgnoresLowScoreOrNameMismatch();
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

void TstArtistMerge::musicBrainzMergesAndLocales()
{
    const QString mimoriPath
        = linernotes::test::fixturePath(QStringLiteral("musicbrainz/artist_search_mimori.json"));
    QFile file(mimoriPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto parseRes = parseArtistSearch(file.readAll());
    QVERIFY(parseRes.ok());
    const auto &mbResults = parseRes.value();

    const ArtistEntry mimori {
        .artistId = 1, .name = QStringLiteral("三森すずこ"), .trackCount = 20
    };
    const ArtistEntry suzuko {
        .artistId = 2, .name = QStringLiteral("Suzuko Mimori"), .trackCount = 3
    };

    // Case A: Library has both "三森すずこ" and "Suzuko Mimori"
    {
        QHash<QString, ArtistEntry> libraryByName;
        libraryByName.insert(mimori.name, mimori);
        libraryByName.insert(suzuko.name, suzuko);

        const auto proposals = musicBrainzProposals(mimori, mbResults, libraryByName);
        QCOMPARE(proposals.size(), 1);

        const auto &p = proposals.first();
        QCOMPARE(p.canonicalArtistId, 1);
        QCOMPARE(p.alias, QStringLiteral("Suzuko Mimori"));
        QCOMPARE(p.locale, std::optional<QString>(QStringLiteral("en")));
        QCOMPARE(p.source, CorrectionSource::MusicBrainz);
        QCOMPARE(p.confidence, 0.95);
        QCOMPARE(p.reason, QStringLiteral("Matched via MusicBrainz"));
    }

    // Case B: Library only has "三森すずこ"
    {
        QHash<QString, ArtistEntry> libraryByName;
        libraryByName.insert(mimori.name, mimori);

        const auto proposals = musicBrainzProposals(mimori, mbResults, libraryByName);
        QCOMPARE(proposals.size(), 1);

        const auto &p = proposals.first();
        QCOMPARE(p.canonicalArtistId, 1);
        QCOMPARE(p.alias, QStringLiteral("Suzuko Mimori"));
        QCOMPARE(p.locale, std::optional<QString>(QStringLiteral("en")));
        QCOMPARE(p.source, CorrectionSource::MusicBrainz);
        QCOMPARE(p.confidence, 0.9);
        QCOMPARE(p.reason, QStringLiteral("MusicBrainz localized alias"));
    }
}

void TstArtistMerge::musicBrainzIgnoresLowScoreOrNameMismatch()
{
    const QString mimoriPath
        = linernotes::test::fixturePath(QStringLiteral("musicbrainz/artist_search_mimori.json"));
    QFile file(mimoriPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto parseRes = parseArtistSearch(file.readAll());
    QVERIFY(parseRes.ok());
    const auto &mbResults = parseRes.value();

    // In mimori fixture: "中森明菜" has score 80 (< 90), so it should be ignored
    const ArtistEntry nakamori {
        .artistId = 3, .name = QStringLiteral("中森明菜"), .trackCount = 10
    };
    QHash<QString, ArtistEntry> libraryByName;
    libraryByName.insert(nakamori.name, nakamori);

    const auto proposals = musicBrainzProposals(nakamori, mbResults, libraryByName);
    QVERIFY(proposals.isEmpty());

    // Non-existent artist in fixture
    const ArtistEntry unknown {
        .artistId = 99, .name = QStringLiteral("Unknown Artist"), .trackCount = 1
    };
    libraryByName.insert(unknown.name, unknown);
    const auto unknownProposals = musicBrainzProposals(unknown, mbResults, libraryByName);
    QVERIFY(unknownProposals.isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistMerge)

#include "tst_ArtistMerge.moc"
