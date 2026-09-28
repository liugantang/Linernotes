// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include <butler/ArtistGroup.h>
#include <butler/ArtistMerge.h>
#include <library/ArtistAliasCorrections.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::ArtistEntry;
using linernotes::butler::ArtistGroup;
using linernotes::butler::groupProposals;
using linernotes::butler::pickCanonical;
using linernotes::library::CorrectionSource;

class TstArtistMerge : public QObject {
    Q_OBJECT

private slots:
    void pickCanonicalPrefersMoreTracks();
    void groupProposalsCreatesRuleProposals();
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

} // namespace

QTEST_GUILESS_MAIN(TstArtistMerge)

#include "tst_ArtistMerge.moc"
