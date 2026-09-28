// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>

#include <butler/ArtistCluster.h>
#include <butler/ArtistMergeLlm.h>
#include <butler/Errors.h>
#include <library/ArtistAliasCorrections.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::ArtistEntry;
using linernotes::butler::ArtistMergeCandidatePair;
using linernotes::butler::parseArtistMergeResult;
using linernotes::library::CorrectionSource;
namespace errc = linernotes::butler::errc;

class TstArtistMergeLlm : public QObject {
    Q_OBJECT

private slots:
    void parseCreatesProposalForSame();
    void parseRejectsBadId();
};

void TstArtistMergeLlm::parseCreatesProposalForSame()
{
    const ArtistEntry e1 { .artistId = 1, .name = QStringLiteral("Amamiya Sora"), .trackCount = 2 };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("Sora Amamiya"), .trackCount = 1 };
    const ArtistEntry e3 { .artistId = 3, .name = QStringLiteral("Yuki Kajiura"), .trackCount = 5 };
    const ArtistEntry e4 { .artistId = 4, .name = QStringLiteral("Yuki Kaji"), .trackCount = 5 };
    const ArtistEntry e5 { .artistId = 5, .name = QStringLiteral("Artist X"), .trackCount = 1 };
    const ArtistEntry e6 { .artistId = 6, .name = QStringLiteral("Artist Y"), .trackCount = 1 };

    const QList<ArtistMergeCandidatePair> pairs = {
        ArtistMergeCandidatePair {
            .id = 0,
            .artistA = e1,
            .albumsA = { QStringLiteral("Album 1") },
            .artistB = e2,
            .albumsB = { QStringLiteral("Album 2") },
        },
        ArtistMergeCandidatePair {
            .id = 1,
            .artistA = e3,
            .albumsA = { QStringLiteral("Album 3") },
            .artistB = e4,
            .albumsB = { QStringLiteral("Album 4") },
        },
        ArtistMergeCandidatePair {
            .id = 2,
            .artistA = e5,
            .albumsA = { },
            .artistB = e6,
            .albumsB = { },
        },
    };

    QHash<qint64, ArtistEntry> entriesById;
    entriesById.insert(e1.artistId, e1);
    entriesById.insert(e2.artistId, e2);
    entriesById.insert(e3.artistId, e3);
    entriesById.insert(e4.artistId, e4);
    entriesById.insert(e5.artistId, e5);
    entriesById.insert(e6.artistId, e6);

    const char *jsonStr = R"({
        "pairs": [
            { "id": 0, "same": true, "confidence": 0.9, "reason": "Romanization variation" },
            { "id": 1, "same": false, "confidence": 0.95, "reason": "Different musicians" },
            { "id": 2, "same": true, "confidence": 0.4, "reason": "Uncertain" }
        ]
    })";

    const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
    QVERIFY(doc.isObject());

    const auto res = parseArtistMergeResult(doc.object(), pairs, entriesById);
    QVERIFY(res.ok());
    const auto &proposals = res.value();

    QCOMPARE(proposals.size(), 1);
    const auto &p = proposals.first();
    QCOMPARE(p.canonicalArtistId, 1);
    QCOMPARE(p.alias, QStringLiteral("Sora Amamiya"));
    QVERIFY(!p.locale.has_value());
    QCOMPARE(p.source, CorrectionSource::Llm);
    QCOMPARE(p.confidence, 0.9);
    QCOMPARE(p.reason, QStringLiteral("Romanization variation"));
}

void TstArtistMergeLlm::parseRejectsBadId()
{
    const ArtistEntry e1 { .artistId = 1, .name = QStringLiteral("Artist A"), .trackCount = 2 };
    const ArtistEntry e2 { .artistId = 2, .name = QStringLiteral("Artist B"), .trackCount = 1 };

    const QList<ArtistMergeCandidatePair> pairs = {
        ArtistMergeCandidatePair {
            .id = 0,
            .artistA = e1,
            .albumsA = { },
            .artistB = e2,
            .albumsB = { },
        },
    };

    QHash<qint64, ArtistEntry> entriesById;
    entriesById.insert(e1.artistId, e1);
    entriesById.insert(e2.artistId, e2);

    // 1. Unknown ID 99
    {
        const char *jsonStr = R"({
            "pairs": [
                { "id": 99, "same": true, "confidence": 0.9, "reason": "Test" }
            ]
        })";
        const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
        const auto res = parseArtistMergeResult(doc.object(), pairs, entriesById);
        QVERIFY(!res.ok());
        QCOMPARE(res.error().code, QString(errc::kArtistMergeInvalidResult));
    }

    // 2. Duplicate ID 0
    {
        const char *jsonStr = R"({
            "pairs": [
                { "id": 0, "same": true, "confidence": 0.9, "reason": "Test 1" },
                { "id": 0, "same": false, "confidence": 0.8, "reason": "Test 2" }
            ]
        })";
        const auto doc = QJsonDocument::fromJson(QByteArray(jsonStr));
        const auto res = parseArtistMergeResult(doc.object(), pairs, entriesById);
        QVERIFY(!res.ok());
        QCOMPARE(res.error().code, QString(errc::kArtistMergeInvalidResult));
    }

    // 3. Not an object / missing pairs array
    {
        const auto res = parseArtistMergeResult(QJsonValue(42), pairs, entriesById);
        QVERIFY(!res.ok());
        QCOMPARE(res.error().code, QString(errc::kArtistMergeInvalidResult));

        const auto res2 = parseArtistMergeResult(QJsonObject { }, pairs, entriesById);
        QVERIFY(!res2.ok());
        QCOMPARE(res2.error().code, QString(errc::kArtistMergeInvalidResult));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistMergeLlm)

#include "tst_ArtistMergeLlm.moc"
