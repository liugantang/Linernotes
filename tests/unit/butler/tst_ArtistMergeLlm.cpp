// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>

#include <butler/ArtistGroup.h>
#include <butler/ArtistMergeLlm.h>
#include <butler/Errors.h>
#include <library/ArtistAliasCorrections.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::ArtistMergeGroup;
using linernotes::butler::ArtistMergeMember;
using linernotes::butler::artistMergePromptVars;
using linernotes::butler::parseArtistMergeResult;
using linernotes::library::CorrectionSource;
namespace errc = linernotes::butler::errc;

class TstArtistMergeLlm : public QObject {
    Q_OBJECT

private slots:
    void parsePartitionsGroupIntoSubsets();
    void parseSkipsGroupWithInvalidMember();
    void parseIgnoresLowConfidenceAndSingleMember();
    void promptVarsContainsMemberAka();
    void parseRejectsBadTopLevel();
};

void TstArtistMergeLlm::parsePartitionsGroupIntoSubsets()
{
    const ArtistMergeMember m1 {
        .entry = { .artistId = 1, .name = QStringLiteral("Amamiya Sora"), .trackCount = 10 },
        .albums = { QStringLiteral("Album 1") },
        .aka = { QStringLiteral("Sora Amamiya") },
    };
    const ArtistMergeMember m2 {
        .entry = { .artistId = 2, .name = QStringLiteral("Sora Amamiya"), .trackCount = 2 },
        .albums = { QStringLiteral("Album 2") },
        .aka = { },
    };
    const ArtistMergeMember m3 {
        .entry = { .artistId = 3, .name = QStringLiteral("Yuki Kajiura"), .trackCount = 8 },
        .albums = { QStringLiteral("Album 3") },
        .aka = { },
    };
    const ArtistMergeMember m4 {
        .entry = { .artistId = 4, .name = QStringLiteral("FictionJunction"), .trackCount = 5 },
        .albums = { QStringLiteral("Album 4") },
        .aka = { },
    };

    const QList<ArtistMergeGroup> groups = {
        ArtistMergeGroup {
            .id = 0,
            .members = { m1, m2, m3, m4 },
        },
    };

    const char *jsonStr = R"({
        "groups": [
            {
                "id": 0,
                "subsets": [
                    {
                        "members": [1, 2],
                        "confidence": 0.95,
                        "reason": "Romanization variant"
                    },
                    {
                        "members": [3, 4],
                        "confidence": 0.9,
                        "reason": "Same musical project"
                    }
                ]
            }
        ]
    })";

    const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
    QVERIFY(doc.isObject());

    const auto res = parseArtistMergeResult(doc.object(), groups);
    QVERIFY(res.ok());
    const auto &proposals = res.value();

    QCOMPARE(proposals.size(), 2);

    const auto &p1 = proposals.at(0);
    QCOMPARE(p1.canonicalArtistId, 1);
    QCOMPARE(p1.alias, QStringLiteral("Sora Amamiya"));
    QVERIFY(!p1.locale.has_value());
    QCOMPARE(p1.source, CorrectionSource::Llm);
    QCOMPARE(p1.confidence, 0.95);
    QCOMPARE(p1.reason, QStringLiteral("Romanization variant"));

    const auto &p2 = proposals.at(1);
    QCOMPARE(p2.canonicalArtistId, 3);
    QCOMPARE(p2.alias, QStringLiteral("FictionJunction"));
    QVERIFY(!p2.locale.has_value());
    QCOMPARE(p2.source, CorrectionSource::Llm);
    QCOMPARE(p2.confidence, 0.9);
    QCOMPARE(p2.reason, QStringLiteral("Same musical project"));
}

void TstArtistMergeLlm::parseSkipsGroupWithInvalidMember()
{
    const ArtistMergeMember m1 {
        .entry = { .artistId = 1, .name = QStringLiteral("Artist A"), .trackCount = 10 },
        .albums = { },
        .aka = { },
    };
    const ArtistMergeMember m2 {
        .entry = { .artistId = 2, .name = QStringLiteral("Artist B"), .trackCount = 5 },
        .albums = { },
        .aka = { },
    };

    const QList<ArtistMergeGroup> groups = {
        ArtistMergeGroup {
            .id = 0,
            .members = { m1, m2 },
        },
    };

    // Case 1: Member 999 does not belong to group 0 -> skip group 0 (return empty, no error)
    {
        const char *jsonStr = R"({
            "groups": [
                {
                    "id": 0,
                    "subsets": [
                        {
                            "members": [1, 999],
                            "confidence": 0.9,
                            "reason": "Test"
                        }
                    ]
                }
            ]
        })";
        const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
        const auto res = parseArtistMergeResult(doc.object(), groups);
        QVERIFY(res.ok());
        QVERIFY(res.value().isEmpty());
    }

    // Case 2: Member 1 repeated across multiple subsets in same group -> skip group 0
    {
        const char *jsonStr = R"({
            "groups": [
                {
                    "id": 0,
                    "subsets": [
                        {
                            "members": [1, 2],
                            "confidence": 0.9,
                            "reason": "Test 1"
                        },
                        {
                            "members": [1, 2],
                            "confidence": 0.9,
                            "reason": "Test 2"
                        }
                    ]
                }
            ]
        })";
        const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
        const auto res = parseArtistMergeResult(doc.object(), groups);
        QVERIFY(res.ok());
        QVERIFY(res.value().isEmpty());
    }
}

void TstArtistMergeLlm::parseIgnoresLowConfidenceAndSingleMember()
{
    const ArtistMergeMember m1 {
        .entry = { .artistId = 1, .name = QStringLiteral("Artist A"), .trackCount = 10 },
        .albums = { },
        .aka = { },
    };
    const ArtistMergeMember m2 {
        .entry = { .artistId = 2, .name = QStringLiteral("Artist B"), .trackCount = 5 },
        .albums = { },
        .aka = { },
    };
    const ArtistMergeMember m3 {
        .entry = { .artistId = 3, .name = QStringLiteral("Artist C"), .trackCount = 3 },
        .albums = { },
        .aka = { },
    };

    const QList<ArtistMergeGroup> groups = {
        ArtistMergeGroup {
            .id = 0,
            .members = { m1, m2, m3 },
        },
    };

    // Subset with confidence 0.4 (< 0.5) is ignored; subset with single member [3] is ignored
    const char *jsonStr = R"({
        "groups": [
            {
                "id": 0,
                "subsets": [
                    {
                        "members": [1, 2],
                        "confidence": 0.4,
                        "reason": "Low confidence"
                    },
                    {
                        "members": [3],
                        "confidence": 0.95,
                        "reason": "Single member"
                    }
                ]
            }
        ]
    })";

    const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
    const auto res = parseArtistMergeResult(doc.object(), groups);
    QVERIFY(res.ok());
    QVERIFY(res.value().isEmpty());
}

void TstArtistMergeLlm::promptVarsContainsMemberAka()
{
    const ArtistMergeMember m1 {
        .entry = { .artistId = 1, .name = QStringLiteral("아이유"), .trackCount = 20 },
        .albums = { QStringLiteral("Palette"), QStringLiteral("LILAC") },
        .aka = { QStringLiteral("IU"), QStringLiteral("Lee Ji-eun") },
    };

    const QList<ArtistMergeGroup> groups = {
        ArtistMergeGroup {
            .id = 0,
            .members = { m1 },
        },
    };

    const auto vars = artistMergePromptVars(groups);
    QVERIFY(vars.contains(QStringLiteral("groups")));
    const QString text = vars.value(QStringLiteral("groups"));
    QVERIFY(text.contains(QStringLiteral("Group ID 0:")));
    QVERIFY(text.contains(QStringLiteral("아이유")));
    QVERIFY(text.contains(QStringLiteral("IU, Lee Ji-eun")));
    QVERIFY(text.contains(QStringLiteral("Palette, LILAC")));
}

void TstArtistMergeLlm::parseRejectsBadTopLevel()
{
    const QList<ArtistMergeGroup> groups;

    // 1. Not an object
    {
        const auto res = parseArtistMergeResult(QJsonValue(123), groups);
        QVERIFY(!res.ok());
        QCOMPARE(res.error().code, QString(errc::kArtistMergeInvalidResult));
    }

    // 2. Missing "groups" array
    {
        const auto res = parseArtistMergeResult(QJsonObject { }, groups);
        QVERIFY(!res.ok());
        QCOMPARE(res.error().code, QString(errc::kArtistMergeInvalidResult));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistMergeLlm)

#include "tst_ArtistMergeLlm.moc"
