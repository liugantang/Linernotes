// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <butler/ArtistGroup.h>
#include <butler/ArtistName.h>
#include <common/TestSupport.h>

namespace {

using linernotes::butler::ArtistEntry;
using linernotes::butler::exactKey;
using linernotes::butler::groupArtists;

class TstArtistGroup : public QObject {
    Q_OBJECT

private slots:
    void exactKeySameLongNamesExactOnlyTrue();
    void shortNamesExactOnlyFalse();
    void connectedViaAkaExactOnlyFalse();
    void transitiveGrouping();
    void oversizedGroupEntersOversized();
    void noCommonKeysNoGroups();
    void connectedViaRomanKeyExactOnlyFalse();
};

void TstArtistGroup::exactKeySameLongNamesExactOnlyTrue()
{
    const ArtistEntry e1 { .artistId = 1, .name = QStringLiteral("Jay Chou"), .trackCount = 20 };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("jay chou"), .trackCount = 2 };

    const auto grouping = groupArtists({ e1, e2 }, { }, 12);
    QCOMPARE(grouping.groups.size(), 1);
    QVERIFY(grouping.oversized.isEmpty());

    const auto &group = grouping.groups.first();
    QCOMPARE(group.members.size(), 2);
    QVERIFY(group.exactOnly);
    QCOMPARE(group.members.at(0).artistId, 1);
    QCOMPARE(group.members.at(1).artistId, 2);
}

void TstArtistGroup::shortNamesExactOnlyFalse()
{
    // exactKey is "ab", length = 2 <= 3
    const ArtistEntry e1 { .artistId = 1, .name = QStringLiteral("A B"), .trackCount = 10 };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("ab"), .trackCount = 5 };

    const auto grouping = groupArtists({ e1, e2 }, { }, 12);
    QCOMPARE(grouping.groups.size(), 1);
    QVERIFY(grouping.oversized.isEmpty());

    const auto &group = grouping.groups.first();
    QCOMPARE(group.members.size(), 2);
    QVERIFY(!group.exactOnly);
}

void TstArtistGroup::connectedViaAkaExactOnlyFalse()
{
    const ArtistEntry e1 { .artistId = 1, .name = QStringLiteral("아이유"), .trackCount = 20 };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("IU"), .trackCount = 5 };

    QHash<QString, QStringList> altNames;
    altNames.insert(exactKey(QStringLiteral("아이유")), { QStringLiteral("IU") });

    const auto grouping = groupArtists({ e1, e2 }, altNames, 12);
    QCOMPARE(grouping.groups.size(), 1);
    QVERIFY(grouping.oversized.isEmpty());

    const auto &group = grouping.groups.first();
    QCOMPARE(group.members.size(), 2);
    QVERIFY(!group.exactOnly);
    QCOMPARE(group.members.at(0).artistId, 1);
    QCOMPARE(group.members.at(1).artistId, 2);
}

void TstArtistGroup::transitiveGrouping()
{
    // A-aka-B, B-nameSame-C -> 3 members in 1 group
    const ArtistEntry e1 {
        .artistId = 1, .name = QStringLiteral("Artist Alpha"), .trackCount = 10
    };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("Artist Beta"), .trackCount = 8 };
    const ArtistEntry e3 { .artistId = 3, .name = QStringLiteral("artist beta"), .trackCount = 3 };

    QHash<QString, QStringList> altNames;
    altNames.insert(exactKey(QStringLiteral("Artist Alpha")), { QStringLiteral("Artist Beta") });

    const auto grouping = groupArtists({ e1, e2, e3 }, altNames, 12);
    QCOMPARE(grouping.groups.size(), 1);
    QVERIFY(grouping.oversized.isEmpty());

    const auto &group = grouping.groups.first();
    QCOMPARE(group.members.size(), 3);
    QVERIFY(!group.exactOnly);
    QCOMPARE(group.members.at(0).artistId, 1);
    QCOMPARE(group.members.at(1).artistId, 2);
    QCOMPARE(group.members.at(2).artistId, 3);
}

void TstArtistGroup::oversizedGroupEntersOversized()
{
    QList<ArtistEntry> entries;
    for (int i = 1; i <= 13; ++i) {
        entries.append(ArtistEntry {
            .artistId = i,
            .name = QStringLiteral("Artist %1").arg(i),
            .trackCount = 100 - i,
        });
    }

    // Connect all to "Artist 1" via aka
    QHash<QString, QStringList> altNames;
    QStringList akas;
    for (int i = 2; i <= 13; ++i) {
        akas.append(QStringLiteral("Artist %1").arg(i));
    }
    altNames.insert(exactKey(QStringLiteral("Artist 1")), akas);

    const auto grouping = groupArtists(entries, altNames, 12);
    QVERIFY(grouping.groups.isEmpty());
    QCOMPARE(grouping.oversized.size(), 1);
    QCOMPARE(grouping.oversized.first().size(), 13);
}

void TstArtistGroup::noCommonKeysNoGroups()
{
    const ArtistEntry e1 {
        .artistId = 1, .name = QStringLiteral("Taylor Swift"), .trackCount = 50
    };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("Radiohead"), .trackCount = 30 };

    const auto grouping = groupArtists({ e1, e2 }, { }, 12);
    QVERIFY(grouping.groups.isEmpty());
    QVERIFY(grouping.oversized.isEmpty());
}

void TstArtistGroup::connectedViaRomanKeyExactOnlyFalse()
{
    const ArtistEntry e1 {
        .artistId = 1, .name = QStringLiteral("ハルカトミユキ"), .trackCount = 10
    };
    const ArtistEntry e2 {
        .artistId = 2, .name = QStringLiteral("Haruka to Miyuki"), .trackCount = 5
    };

    const auto grouping = groupArtists({ e1, e2 }, { }, 12);
    QCOMPARE(grouping.groups.size(), 1);
    QVERIFY(grouping.oversized.isEmpty());

    const auto &group = grouping.groups.first();
    QCOMPARE(group.members.size(), 2);
    QVERIFY(!group.exactOnly);
    QCOMPARE(group.members.at(0).artistId, 1);
    QCOMPARE(group.members.at(1).artistId, 2);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistGroup)

#include "tst_ArtistGroup.moc"
