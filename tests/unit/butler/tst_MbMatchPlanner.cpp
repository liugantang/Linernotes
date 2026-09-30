// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include <butler/AlbumMatch.h>
#include <butler/MbMatchPlanner.h>
#include <butler/MbMatchSource.h>
#include <butler/MusicBrainz.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/LibraryEnums.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace {

using linernotes::butler::buildProposals;
using linernotes::butler::MbAlbumInput;
using linernotes::butler::MbAlbumTrack;
using linernotes::butler::parseRelease;
using linernotes::butler::parseReleaseSearch;
using linernotes::butler::scoreRelease;
using linernotes::butler::selectCandidates;
using linernotes::library::CorrectionSource;
using linernotes::library::TagField;

QByteArray readFixture(const QString &relativePath)
{
    const QString fullPath = linernotes::test::fixturePath(relativePath);
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

class TstMbMatchPlanner : public QObject {
    Q_OBJECT

private slots:
    void testSelectCandidatesFlowerflower();
    void testBuildProposalsFlowerflower();
    void testTitleNeedsOnlineProposal();
};

void TstMbMatchPlanner::testSelectCandidatesFlowerflower()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_search_flowerflower.json"));
    QVERIFY(!json.isEmpty());

    const auto searchRes = parseReleaseSearch(json);
    QVERIFY(searchRes.ok());
    const auto &summaries = searchRes.value();

    const QStringList candidates = selectCandidates(summaries, 5);
    QVERIFY(!candidates.isEmpty());
    QVERIFY(candidates.size() <= 3);

    // Verify deduplication: no duplicate (releaseGroupId, trackCount) among returned candidates
    QSet<QPair<QString, int>> seen;
    for (const auto &candId : candidates) {
        auto it = std::ranges::find_if(summaries, [&](const auto &s) { return s.id == candId; });
        QVERIFY(it != summaries.end());
        if (it != summaries.end()) {
            const auto key = qMakePair(it->releaseGroupId, it->trackCount);
            QVERIFY(!seen.contains(key));
            seen.insert(key);
        }
    }
}

void TstMbMatchPlanner::testBuildProposalsFlowerflower()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto releaseRes = parseRelease(json);
    QVERIFY(releaseRes.ok());
    const auto &release = releaseRes.value();

    MbAlbumInput input;
    input.albumId = 1;
    input.searchTitle = QStringLiteral("宝物");
    input.searchArtist = QStringLiteral("FLOWER FLOWER");

    // 5 tracks matching MB
    const QStringList titles = {
        QStringLiteral("宝物"),
        QStringLiteral("炎"),
        QStringLiteral("とうめいなうた (Live at Shibuya WWW)"),
        QStringLiteral("宝物 (Live at Shibuya WWW)"),
        QStringLiteral("スタートライン (Live at Shibuya WWW)"),
    };
    const QList<qint64> durations = { 292000, 309000, 328000, 298000, 340000 };

    for (int i = 0; i < 5; ++i) {
        MbAlbumTrack trk;
        trk.local.trackId = i + 1;
        trk.local.title = titles.at(i);
        trk.local.durationMs = durations.at(i);
        trk.local.trackNumber = std::nullopt;
        trk.local.discNumber = std::nullopt;
        if (i == 1) {
            trk.year = 2000; // Track 2 already has year 2000
        } else {
            trk.year = std::nullopt; // Others have no year
        }
        input.tracks.append(std::move(trk));
    }

    const auto localAlbum = input.toLocalAlbum();
    const auto match = scoreRelease(localAlbum, release);
    QVERIFY(match.score > 0.9);
    QCOMPARE(match.mapping.size(), 5);

    const auto proposals = buildProposals(input, release, match);
    QVERIFY(!proposals.isEmpty());

    // Track 1 gets year 2016 proposal
    bool foundTrack1Year = false;
    // Track 2 should NOT get year proposal
    bool foundTrack2Year = false;
    // Every track should get track_number proposal
    QSet<qint64> tracksWithTrackNumber;
    // Single disc release -> no disc_number / disc_total proposals
    bool foundDiscNumber = false;
    bool foundDiscTotal = false;

    for (const auto &p : proposals) {
        if (p.field == TagField::Year) {
            if (p.trackId == 1) {
                foundTrack1Year = true;
                QCOMPARE(p.newValue, QStringLiteral("2016"));
                QCOMPARE(p.source, CorrectionSource::MusicBrainz);
            }
            if (p.trackId == 2) {
                foundTrack2Year = true;
            }
        }
        if (p.field == TagField::TrackNumber) {
            tracksWithTrackNumber.insert(p.trackId);
        }
        if (p.field == TagField::DiscNumber) {
            foundDiscNumber = true;
        }
        if (p.field == TagField::DiscTotal) {
            foundDiscTotal = true;
        }
    }

    QVERIFY(foundTrack1Year);
    QVERIFY(!foundTrack2Year);
    QCOMPARE(tracksWithTrackNumber.size(), 5);
    QVERIFY(!foundDiscNumber);
    QVERIFY(!foundDiscTotal);
}

void TstMbMatchPlanner::testTitleNeedsOnlineProposal()
{
    const QByteArray json
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!json.isEmpty());

    const auto releaseRes = parseRelease(json);
    QVERIFY(releaseRes.ok());
    const auto &release = releaseRes.value();

    MbAlbumInput input;
    input.albumId = 2;
    input.searchTitle = QStringLiteral("宝物");
    input.searchArtist = QStringLiteral("FLOWER FLOWER");

    // Track 1: title damaged, titleNeedsOnline = true
    {
        MbAlbumTrack trk1;
        trk1.local.trackId = 101;
        trk1.local.title = QStringLiteral("###DAMAGED###");
        trk1.local.durationMs = 292000;
        trk1.titleNeedsOnline = true;
        input.tracks.append(std::move(trk1));
    }

    // Track 2: title normal, titleNeedsOnline = false
    {
        MbAlbumTrack trk2;
        trk2.local.trackId = 102;
        trk2.local.title = QStringLiteral("炎");
        trk2.local.durationMs = 309000;
        trk2.titleNeedsOnline = false;
        input.tracks.append(std::move(trk2));
    }

    const auto localAlbum = input.toLocalAlbum();
    const auto match = scoreRelease(localAlbum, release);
    QVERIFY(match.score > 0.5);

    const auto proposals = buildProposals(input, release, match);

    bool track1GotTitle = false;
    bool track2GotTitle = false;

    for (const auto &p : proposals) {
        if (p.field == TagField::Title) {
            if (p.trackId == 101) {
                track1GotTitle = true;
                QCOMPARE(p.newValue, QStringLiteral("宝物"));
            }
            if (p.trackId == 102) {
                track2GotTitle = true;
            }
        }
    }

    QVERIFY(track1GotTitle);
    QVERIFY(!track2GotTitle);
}

} // namespace

QTEST_GUILESS_MAIN(TstMbMatchPlanner)

#include "tst_MbMatchPlanner.moc"
