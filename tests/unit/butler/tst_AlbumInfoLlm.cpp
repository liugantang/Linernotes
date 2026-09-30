// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include <butler/AlbumInfoLlm.h>
#include <butler/AlbumInfoSource.h>

#include <optional>

namespace {

using linernotes::butler::AlbumInfoInput;
using linernotes::butler::albumInfoPromptVars;
using linernotes::butler::albumInfoSchema;
using linernotes::butler::AlbumInfoTrack;
using linernotes::butler::buildAlbumInfoProposals;
using linernotes::butler::parseAlbumInfoItemKey;
using linernotes::butler::planAlbumInfoBatches;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionSource;
using linernotes::library::TagField;

class TstAlbumInfoLlm : public QObject {
    Q_OBJECT

private slots:
    void normalResult();
    void onlyCompletesMissingFields();
    void discardsInvalidValues();
    void planBatches();
    void promptVarsAndSchema();
};

void TstAlbumInfoLlm::normalResult()
{
    AlbumInfoTrack track1;
    track1.trackId = 101;
    track1.relativePath = QStringLiteral("03. 晴天.mp3");
    track1.title = QStringLiteral("晴天");
    track1.titleUnusable = false;
    track1.artist = QStringLiteral("周杰伦");
    track1.albumArtist = QString();
    track1.year = std::nullopt;
    track1.trackNumber = std::nullopt;
    track1.discNumber = std::nullopt;
    track1.trackTotal = std::nullopt;
    track1.discTotal = std::nullopt;

    AlbumInfoTrack track2;
    track2.trackId = 102;
    track2.relativePath = QStringLiteral("04. 轨迹.mp3");
    track2.title = QStringLiteral("轨迹");
    track2.titleUnusable = false;
    track2.artist = QStringLiteral("周杰伦");
    track2.albumArtist = QString();
    track2.year = std::nullopt;
    track2.trackNumber = 4;
    track2.discNumber = std::nullopt;
    track2.trackTotal = std::nullopt;
    track2.discTotal = std::nullopt;

    AlbumInfoInput album;
    album.albumId = 1;
    album.title = QStringLiteral("叶惠美");
    album.albumArtist = QString();
    album.tracks = { track1, track2 };

    QJsonObject albumObj;
    albumObj.insert(QStringLiteral("id"), 1);

    // Album-level year with knowledge evidence -> fromKnowledge for all tracks missing year
    albumObj.insert(QStringLiteral("year"),
        QJsonObject {
            { QStringLiteral("value"), 2003 },
            { QStringLiteral("evidence"), QStringLiteral("knowledge") },
            { QStringLiteral("confidence"), 0.85 },
        });

    // Album-level albumArtist with tags evidence -> fromEvidence
    albumObj.insert(QStringLiteral("albumArtist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("周杰伦") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.90 },
        });

    // Album-level discTotal with tags evidence -> fromEvidence
    albumObj.insert(QStringLiteral("discTotal"),
        QJsonObject {
            { QStringLiteral("value"), 1 },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.90 },
        });

    // Discs: disc 1 trackTotal with path evidence -> fromEvidence
    QJsonObject disc1Obj;
    disc1Obj.insert(QStringLiteral("disc"), 1);
    disc1Obj.insert(QStringLiteral("trackTotal"),
        QJsonObject {
            { QStringLiteral("value"), 10 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.95 },
        });
    albumObj.insert(QStringLiteral("discs"), QJsonArray { disc1Obj });

    // Tracks: only track 101 with path trackNumber -> fromEvidence
    QJsonObject trk101Obj;
    trk101Obj.insert(QStringLiteral("id"), 101);
    trk101Obj.insert(QStringLiteral("trackNumber"),
        QJsonObject {
            { QStringLiteral("value"), 3 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.95 },
        });
    albumObj.insert(QStringLiteral("tracks"), QJsonArray { trk101Obj });

    QJsonObject root;
    root.insert(QStringLiteral("albums"), QJsonArray { albumObj });

    const auto res = buildAlbumInfoProposals(root, { album });
    QVERIFY(res.ok());
    const auto &proposals = res.value();

    // Verify fromEvidence:
    // Track 101: albumArtist, discTotal, trackTotal, trackNumber (4 proposals)
    // Track 102: albumArtist, discTotal, trackTotal (3 proposals, trackNumber 4 already exists)
    // Total fromEvidence = 7
    QCOMPARE(proposals.fromEvidence.size(), 7);

    auto findProp = [](const QList<CorrectionProposal> &list, qint64 trkId,
                        TagField f) -> CorrectionProposal {
        for (const auto &p : list) {
            if (p.trackId == trkId && p.field == f) {
                return p;
            }
        }
        return CorrectionProposal { };
    };

    const auto pAlbumArtist1 = findProp(proposals.fromEvidence, 101, TagField::AlbumArtist);
    QCOMPARE(pAlbumArtist1.trackId, 101);
    QCOMPARE(pAlbumArtist1.newValue, QStringLiteral("周杰伦"));
    QCOMPARE(pAlbumArtist1.source, CorrectionSource::Llm);
    QCOMPARE(pAlbumArtist1.confidence, 0.90);
    QCOMPARE(pAlbumArtist1.reason, QStringLiteral("From tags of other tracks"));

    const auto pAlbumArtist2 = findProp(proposals.fromEvidence, 102, TagField::AlbumArtist);
    QCOMPARE(pAlbumArtist2.trackId, 102);
    QCOMPARE(pAlbumArtist2.newValue, QStringLiteral("周杰伦"));

    const auto pTrackTotal1 = findProp(proposals.fromEvidence, 101, TagField::TrackTotal);
    QCOMPARE(pTrackTotal1.trackId, 101);
    QCOMPARE(pTrackTotal1.newValue, QStringLiteral("10"));
    QCOMPARE(pTrackTotal1.source, CorrectionSource::Llm);
    QCOMPARE(pTrackTotal1.confidence, 0.95);
    QCOMPARE(pTrackTotal1.reason, QStringLiteral("From file or folder name"));

    const auto pTrackTotal2 = findProp(proposals.fromEvidence, 102, TagField::TrackTotal);
    QCOMPARE(pTrackTotal2.trackId, 102);
    QCOMPARE(pTrackTotal2.newValue, QStringLiteral("10"));

    const auto pTrkNum1 = findProp(proposals.fromEvidence, 101, TagField::TrackNumber);
    QCOMPARE(pTrkNum1.trackId, 101);
    QCOMPARE(pTrkNum1.newValue, QStringLiteral("3"));
    QCOMPARE(pTrkNum1.source, CorrectionSource::Llm);
    QCOMPARE(pTrkNum1.confidence, 0.95);
    QCOMPARE(pTrkNum1.reason, QStringLiteral("From file or folder name"));

    const auto pTrkNum2 = findProp(proposals.fromEvidence, 102, TagField::TrackNumber);
    QCOMPARE(pTrkNum2.trackId, 0);

    // Verify fromKnowledge:
    // Track 101: year 2003
    // Track 102: year 2003
    QCOMPARE(proposals.fromKnowledge.size(), 2);

    const auto pYear1 = findProp(proposals.fromKnowledge, 101, TagField::Year);
    QCOMPARE(pYear1.trackId, 101);
    QCOMPARE(pYear1.newValue, QStringLiteral("2003"));
    QCOMPARE(pYear1.source, CorrectionSource::Llm);
    QCOMPARE(pYear1.confidence, 0.85);
    QCOMPARE(pYear1.reason, QStringLiteral("From model knowledge, unverified"));

    const auto pYear2 = findProp(proposals.fromKnowledge, 102, TagField::Year);
    QCOMPARE(pYear2.trackId, 102);
    QCOMPARE(pYear2.newValue, QStringLiteral("2003"));
}

void TstAlbumInfoLlm::onlyCompletesMissingFields()
{
    AlbumInfoTrack track1;
    track1.trackId = 201;
    track1.relativePath = QStringLiteral("01. Song.mp3");
    track1.title = QStringLiteral("Existing Title");
    track1.titleUnusable = false; // not unusable -> title proposal should be ignored
    track1.artist = QStringLiteral("Existing Artist");
    track1.albumArtist = QStringLiteral("Existing Album Artist");
    track1.year = 2005; // already has year -> year proposal should be ignored
    track1.trackNumber = 1;
    track1.discNumber = 1;
    track1.trackTotal = 10;
    track1.discTotal = 1;

    AlbumInfoTrack track2;
    track2.trackId = 202;
    track2.relativePath = QStringLiteral("02. Song 2.mp3");
    track2.title = QStringLiteral("DamagedTitle???");
    track2.titleUnusable = true; // unusable -> title proposal should be accepted
    track2.artist = QString(); // missing -> should be accepted
    track2.albumArtist = QString(); // missing -> albumArtist should be accepted
    track2.year = std::nullopt; // missing -> year should be accepted
    track2.trackNumber = std::nullopt; // missing -> trackNumber should be accepted
    track2.discNumber = std::nullopt;
    track2.trackTotal = std::nullopt;
    track2.discTotal = std::nullopt;

    AlbumInfoInput album;
    album.albumId = 2;
    album.title = QStringLiteral("Album 2");
    album.albumArtist = QStringLiteral("Existing Album Artist");
    album.tracks = { track1, track2 };

    // Album level returns year and albumArtist
    QJsonObject albumObj;
    albumObj.insert(QStringLiteral("id"), 2);
    albumObj.insert(QStringLiteral("year"),
        QJsonObject {
            { QStringLiteral("value"), 2005 },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.9 },
        });
    albumObj.insert(QStringLiteral("albumArtist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Existing Album Artist") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // Track 1 JSON returns title and artist (even though track 1 already has them)
    QJsonObject trk1Obj;
    trk1Obj.insert(QStringLiteral("id"), 201);
    trk1Obj.insert(QStringLiteral("title"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("New Title") },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.9 },
        });
    trk1Obj.insert(QStringLiteral("artist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("New Artist") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // Track 2 JSON returns title, artist, trackNumber
    QJsonObject trk2Obj;
    trk2Obj.insert(QStringLiteral("id"), 202);
    trk2Obj.insert(QStringLiteral("title"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Fixed Title 2") },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.95 },
        });
    trk2Obj.insert(QStringLiteral("artist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Artist 2") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.8 },
        });
    trk2Obj.insert(QStringLiteral("trackNumber"),
        QJsonObject {
            { QStringLiteral("value"), 2 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.95 },
        });

    albumObj.insert(QStringLiteral("tracks"), QJsonArray { trk1Obj, trk2Obj });

    QJsonObject root;
    root.insert(QStringLiteral("albums"), QJsonArray { albumObj });

    const auto res = buildAlbumInfoProposals(root, { album });
    QVERIFY(res.ok());
    const auto &proposals = res.value();

    // Track 1 should have 0 proposals (all fields were already filled)
    for (const auto &p : proposals.fromEvidence) {
        QVERIFY(p.trackId != 201);
    }
    for (const auto &p : proposals.fromKnowledge) {
        QVERIFY(p.trackId != 201);
    }

    // Track 2 proposals
    auto findProp = [&](TagField f) -> CorrectionProposal {
        for (const auto &p : proposals.fromEvidence) {
            if (p.trackId == 202 && p.field == f) {
                return p;
            }
        }
        for (const auto &p : proposals.fromKnowledge) {
            if (p.trackId == 202 && p.field == f) {
                return p;
            }
        }
        return CorrectionProposal { };
    };

    const auto pTitle = findProp(TagField::Title);
    QCOMPARE(pTitle.trackId, 202);
    QCOMPARE(pTitle.newValue, QStringLiteral("Fixed Title 2"));

    const auto pArtist = findProp(TagField::Artist);
    QCOMPARE(pArtist.trackId, 202);
    QCOMPARE(pArtist.newValue, QStringLiteral("Artist 2"));

    const auto pAlbumArtist = findProp(TagField::AlbumArtist);
    QCOMPARE(pAlbumArtist.trackId, 202);
    QCOMPARE(pAlbumArtist.newValue, QStringLiteral("Existing Album Artist"));

    const auto pYear = findProp(TagField::Year);
    QCOMPARE(pYear.trackId, 202);
    QCOMPARE(pYear.newValue, QStringLiteral("2005"));

    const auto pTrkNum = findProp(TagField::TrackNumber);
    QCOMPARE(pTrkNum.trackId, 202);
    QCOMPARE(pTrkNum.newValue, QStringLiteral("2"));
}

void TstAlbumInfoLlm::discardsInvalidValues()
{
    AlbumInfoTrack track1;
    track1.trackId = 301;
    track1.relativePath = QStringLiteral("01. Track.mp3");

    AlbumInfoInput album;
    album.albumId = 3;
    album.tracks = { track1 };

    QJsonObject albumObj;
    albumObj.insert(QStringLiteral("id"), 3);

    // 1. Year 1800 (out of range [1900, now+1])
    albumObj.insert(QStringLiteral("year"),
        QJsonObject {
            { QStringLiteral("value"), 1800 },
            { QStringLiteral("evidence"), QStringLiteral("knowledge") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // 2. DiscTotal 3
    albumObj.insert(QStringLiteral("discTotal"),
        QJsonObject {
            { QStringLiteral("value"), 3 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // 3. Valid albumArtist
    albumObj.insert(QStringLiteral("albumArtist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Valid Album Artist") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.88 },
        });

    // 4. Discs: disc 1 trackTotal 3
    QJsonObject disc1Obj;
    disc1Obj.insert(QStringLiteral("disc"), 1);
    disc1Obj.insert(QStringLiteral("trackTotal"),
        QJsonObject {
            { QStringLiteral("value"), 3 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.9 },
        });
    albumObj.insert(QStringLiteral("discs"), QJsonArray { disc1Obj });

    QJsonObject trkObj;
    trkObj.insert(QStringLiteral("id"), 301);

    // 5. TrackNumber 0 (invalid < 1)
    trkObj.insert(QStringLiteral("trackNumber"),
        QJsonObject {
            { QStringLiteral("value"), 0 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // 6. DiscNumber 5 (invalid because discNumber 5 > discTotal 3)
    trkObj.insert(QStringLiteral("discNumber"),
        QJsonObject {
            { QStringLiteral("value"), 5 },
            { QStringLiteral("evidence"), QStringLiteral("path") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // 7. Unknown evidence
    trkObj.insert(QStringLiteral("artist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Valid Artist") },
            { QStringLiteral("evidence"), QStringLiteral("unknown_evidence") },
            { QStringLiteral("confidence"), 0.9 },
        });

    // 8. Unknown track id 999
    QJsonObject trkUnknownObj;
    trkUnknownObj.insert(QStringLiteral("id"), 999);
    trkUnknownObj.insert(QStringLiteral("artist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Ghost") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.88 },
        });

    albumObj.insert(QStringLiteral("tracks"), QJsonArray { trkObj, trkUnknownObj });

    // 9. Unknown album id 888
    QJsonObject unknownAlbumObj;
    unknownAlbumObj.insert(QStringLiteral("id"), 888);
    unknownAlbumObj.insert(QStringLiteral("albumArtist"),
        QJsonObject {
            { QStringLiteral("value"), QStringLiteral("Ghost Album") },
            { QStringLiteral("evidence"), QStringLiteral("tags") },
            { QStringLiteral("confidence"), 0.88 },
        });

    QJsonObject root;
    root.insert(QStringLiteral("albums"), QJsonArray { albumObj, unknownAlbumObj });

    const auto res = buildAlbumInfoProposals(root, { album });
    QVERIFY(res.ok());
    const auto &proposals = res.value();

    // Invalid values should be discarded:
    // - Year 1800 discarded -> fromKnowledge is empty
    // - TrackNumber 0 discarded
    // - DiscNumber 5 > DiscTotal 3 discarded
    // - Artist with unknown evidence discarded
    // - Unknown track 999 ignored
    // - Unknown album 888 ignored
    // Only albumArtist, discTotal, and trackTotal for track 301 should survive in fromEvidence
    QCOMPARE(proposals.fromKnowledge.size(), 0);
    QCOMPARE(proposals.fromEvidence.size(), 3);

    for (const auto &p : proposals.fromEvidence) {
        QCOMPARE(p.trackId, 301);
        QVERIFY(p.field == TagField::AlbumArtist || p.field == TagField::DiscTotal
            || p.field == TagField::TrackTotal);
    }
}

void TstAlbumInfoLlm::planBatches()
{
    // maxTracks = 10, maxAlbums = 3
    const QList<QPair<qint64, int>> counts = {
        qMakePair(1LL, 4),
        qMakePair(2LL, 5), // batch 1: [1, 2] (total 9 tracks)
        qMakePair(3LL, 15), // batch 2: [3] (oversized album, standalone)
        qMakePair(4LL, 3),
        qMakePair(5LL, 3),
        qMakePair(6LL, 3), // batch 3: [4, 5, 6] (3 albums, maxAlbums reached)
        qMakePair(7LL, 2), // batch 4: [7]
    };

    const auto batches = planAlbumInfoBatches(counts, 10, 3);
    QCOMPARE(batches.size(), 4);

    const auto b1Res = parseAlbumInfoItemKey(batches.at(0));
    QVERIFY(b1Res.ok());
    QCOMPARE(b1Res.value(), QList<qint64>({ 1, 2 }));

    const auto b2Res = parseAlbumInfoItemKey(batches.at(1));
    QVERIFY(b2Res.ok());
    QCOMPARE(b2Res.value(), QList<qint64>({ 3 }));

    const auto b3Res = parseAlbumInfoItemKey(batches.at(2));
    QVERIFY(b3Res.ok());
    QCOMPARE(b3Res.value(), QList<qint64>({ 4, 5, 6 }));

    const auto b4Res = parseAlbumInfoItemKey(batches.at(3));
    QVERIFY(b4Res.ok());
    QCOMPARE(b4Res.value(), QList<qint64>({ 7 }));

    // Parse invalid keys
    QVERIFY(!parseAlbumInfoItemKey(QStringLiteral("invalid")).ok());
    QVERIFY(!parseAlbumInfoItemKey(QStringLiteral("[]")).ok());
    QVERIFY(!parseAlbumInfoItemKey(QStringLiteral("[-1]")).ok());
    QVERIFY(!parseAlbumInfoItemKey(QStringLiteral("[\"abc\"]")).ok());
}

void TstAlbumInfoLlm::promptVarsAndSchema()
{
    AlbumInfoTrack track1;
    track1.trackId = 10;
    track1.relativePath = QStringLiteral("01 - Song.mp3");
    track1.durationMs = 180000;
    track1.title = QStringLiteral("Song");
    track1.titleUnusable = false;
    track1.artist = QStringLiteral("Artist");

    AlbumInfoInput album;
    album.albumId = 5;
    album.title = QStringLiteral("Album 5");
    album.directory = QStringLiteral("/music/Album 5");
    album.tracks = { track1 };

    const auto vars = albumInfoPromptVars({ album });
    QVERIFY(vars.contains(QStringLiteral("albums")));
    const QString albumsStr = vars.value(QStringLiteral("albums"));
    QVERIFY(albumsStr.contains(QStringLiteral("## Album 5")));
    QVERIFY(albumsStr.contains(QStringLiteral("directory: /music/Album 5")));
    QVERIFY(albumsStr.contains(
        QStringLiteral("album missing: year, albumArtist, discTotal, trackTotal(disc 1)")));
    QVERIFY(albumsStr.contains(QStringLiteral(
        "10 | 01 - Song.mp3 | 180s | title=Song; artist=Artist | trackNumber, discNumber")));

    const auto emptyVars = albumInfoPromptVars({ });
    QCOMPARE(emptyVars.value(QStringLiteral("albums")), QStringLiteral("(none)"));

    const auto schema = albumInfoSchema();
    QVERIFY(!schema.isEmpty());
    QCOMPARE(schema.value(QStringLiteral("title")).toString(), QStringLiteral("AlbumInfo"));
}

} // namespace

QTEST_GUILESS_MAIN(TstAlbumInfoLlm)

#include "tst_AlbumInfoLlm.moc"
