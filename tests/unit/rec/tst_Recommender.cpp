// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <audio/EmbeddingIndex.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <rec/CandidatePool.h>
#include <rec/Recommender.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace {

using linernotes::audio::EmbeddingIndex;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::rec::Candidate;
using linernotes::rec::loadCandidates;
using linernotes::rec::rank;
using linernotes::rec::RecRequest;
using linernotes::rec::Seed;

Candidate makeCandidate(qint64 id)
{
    Candidate c;
    c.trackId = id;
    return c;
}

Seed makeSeed(qint64 trackId, double weight = 1.0)
{
    Seed s;
    s.trackId = trackId;
    s.weight = weight;
    return s;
}

struct DbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path,
        const QVariant &missingSince = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                 "duration_ms, first_seen_at, scanned_at, missing_since) "
                                 "VALUES (?, ?, 1024, 2000, 'hash', 60000, 2000, 2000, ?);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(missingSince);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertAlbum(
        const QSqlDatabase &db, const QString &title = QStringLiteral("Album 1"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO albums (grouping_key, title, created_at) VALUES (?, ?, 1000);"));
        q.addBindValue(title);
        q.addBindValue(title);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertArtist(
        const QSqlDatabase &db, const QString &name = QStringLiteral("Artist 1"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, 1000);"));
        q.addBindValue(name);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId,
        const QVariant &albumId = QVariant(), const QVariant &workId = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, album_id, work_id, tags_read_at, created_at) "
            "VALUES (?, ?, ?, 1000, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(albumId);
        q.addBindValue(workId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertTrackArtist(const QSqlDatabase &db, qint64 trackId, qint64 artistId,
        const QString &role = QStringLiteral("artist"), int position = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO track_artists (track_id, artist_id, role, position) "
                                 "VALUES (?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(artistId);
        q.addBindValue(role);
        q.addBindValue(position);
        return q.exec();
    }

    static qint64 insertDuplicateGroup(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO duplicate_groups (kind, created_at) VALUES ('exact', 1000);"));
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertDuplicateMember(
        const QSqlDatabase &db, qint64 groupId, qint64 trackId, double keepScore, int recommended)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO duplicate_members (group_id, track_id, keep_score, "
                                 "recommended) VALUES (?, ?, ?, ?);"));
        q.addBindValue(groupId);
        q.addBindValue(trackId);
        q.addBindValue(keepScore);
        q.addBindValue(recommended);
        return q.exec();
    }

    static bool insertFavorite(const QSqlDatabase &db, const QString &entityType, qint64 entityId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES (?, ?, 1000);"));
        q.addBindValue(entityType);
        q.addBindValue(entityId);
        return q.exec();
    }

    static bool insertPlayStats(const QSqlDatabase &db, qint64 trackId, int playCount,
        int skipCount, qint64 lastPlayedAt, qint64 totalPlayedMs = 10000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO track_play_stats (track_id, play_count, skip_count, last_played_at, "
            "total_played_ms) VALUES (?, ?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(playCount);
        q.addBindValue(skipCount);
        q.addBindValue(lastPlayedAt);
        q.addBindValue(totalPlayedMs);
        return q.exec();
    }
};

class TstRecommender : public QObject {
    Q_OBJECT

private slots:
    void audioVectorRankingAndNegativeSeed();
    void noIndexReturnsFavoriteAndFresh();
    void exclusions();
    void diversityPerArtistAndAlbum();
    void determinismAndRandomness();
    void loadCandidatesDatabaseQuery();
};

void TstRecommender::audioVectorRankingAndNegativeSeed()
{
    EmbeddingIndex index(4);

    // Seed positive 1: (0, 1, 0, 0)
    const std::array<float, 4> vSeedPos { 0.0F, 1.0F, 0.0F, 0.0F };
    // Candidate 2 (close to positive seed): (0, 0.9, 0.4359, 0)
    const std::array<float, 4> vCand2 { 0.0F, 0.9F, 0.43588989F, 0.0F };
    // Candidate 3 (orthogonal to positive seed): (1, 0, 0, 0)
    const std::array<float, 4> vCand3 { 1.0F, 0.0F, 0.0F, 0.0F };
    // Negative seed 4 (identical to Candidate 2): (0, 0.9, 0.4359, 0)
    const std::array<float, 4> vSeedNeg { 0.0F, 0.9F, 0.43588989F, 0.0F };

    index.add(1, vSeedPos);
    index.add(2, vCand2);
    index.add(3, vCand3);
    index.add(4, vSeedNeg);

    const QList<Candidate> candidates {
        makeCandidate(1),
        makeCandidate(2),
        makeCandidate(3),
        makeCandidate(4),
    };

    // 1. Positive seed only: Candidate 2 (closer) ranks above Candidate 3
    RecRequest req1;
    req1.seeds = { makeSeed(1, 1.0) };
    req1.recentExcludeMs = 0;

    const auto res1 = rank(candidates, &index, req1, 0);
    QCOMPARE(res1.size(), 3);
    QCOMPARE(res1.at(0), 2);
    QCOMPARE(res1.at(2), 3);

    // 2. Add negative seed 4: Candidate 2 is near negative seed, so its rank drops below Candidate
    // 3
    RecRequest req2;
    req2.seeds = {
        makeSeed(1, 1.0),
        makeSeed(4, -1.0),
    };
    req2.recentExcludeMs = 0;

    const auto res2 = rank(candidates, &index, req2, 0);
    QCOMPARE(res2.size(), 2); // Seed 1 and Seed 4 are both excluded
    QCOMPARE(res2.at(0), 3);
    QCOMPARE(res2.at(1), 2);
}

void TstRecommender::noIndexReturnsFavoriteAndFresh()
{
    auto c1 = makeCandidate(1);
    c1.favorite = true;

    auto c2 = makeCandidate(2);
    c2.playCount = 10;
    c2.lastPlayedAtMs = 0;

    auto c3 = makeCandidate(3);
    c3.playCount = 5;
    c3.skipCount = 5;
    c3.lastPlayedAtMs = 0;

    const QList<Candidate> candidates { c1, c2, c3 };

    RecRequest req;
    req.recentExcludeMs = 0;
    req.freshnessWeight = 0.5;

    const auto res = rank(candidates, nullptr, req, 0);
    QCOMPARE(res.size(), 3);
    QCOMPARE(res.at(0), 1);
    QCOMPARE(res.at(1), 2);
    QCOMPARE(res.at(2), 3);
}

void TstRecommender::exclusions()
{
    auto c1 = makeCandidate(1);
    c1.workId = 100;
    auto c2 = makeCandidate(2);
    auto c3 = makeCandidate(3);
    auto c4 = makeCandidate(4);
    c4.workId = 100;
    auto c5 = makeCandidate(5);
    c5.lastPlayedAtMs = 90000;
    auto c6 = makeCandidate(6);
    c6.lastPlayedAtMs = 10000;

    const QList<Candidate> candidates { c1, c2, c3, c4, c5, c6 };

    RecRequest req;
    req.seeds = {
        makeSeed(1, 1.0),
        makeSeed(2, -1.0),
    };
    req.exclude = { 3 };
    req.recentExcludeMs = 50000;

    const auto res = rank(candidates, nullptr, req, 100000);
    QCOMPARE(res.size(), 1);
    QCOMPARE(res.at(0), 6);
}

void TstRecommender::diversityPerArtistAndAlbum()
{
    auto c1 = makeCandidate(1);
    c1.albumId = 100;
    c1.artistIds = { 10 };
    c1.favorite = true;

    auto c2 = makeCandidate(2);
    c2.albumId = 100;
    c2.artistIds = { 10 };

    auto c3 = makeCandidate(3);
    c3.albumId = 200;
    c3.artistIds = { 10 };
    c3.playCount = 1;
    c3.lastPlayedAtMs = 1000;

    auto c4 = makeCandidate(4);
    c4.albumId = 300;
    c4.artistIds = { 10 };
    c4.playCount = 2;
    c4.lastPlayedAtMs = 1000;

    auto c5 = makeCandidate(5);
    c5.albumId = 400;
    c5.artistIds = { 20 };
    c5.playCount = 3;
    c5.lastPlayedAtMs = 1000;

    auto c6 = makeCandidate(6);
    c6.albumId = 500;
    c6.workId = 999;
    c6.artistIds = { 30 };
    c6.playCount = 4;
    c6.lastPlayedAtMs = 1000;

    auto c7 = makeCandidate(7);
    c7.albumId = 600;
    c7.workId = 999;
    c7.artistIds = { 40 };
    c7.playCount = 5;
    c7.lastPlayedAtMs = 1000;

    const QList<Candidate> candidates { c1, c2, c3, c4, c5, c6, c7 };

    RecRequest req;
    req.recentExcludeMs = 0;
    req.maxPerAlbum = 1;
    req.maxPerArtist = 2;
    req.count = 10;

    const auto res = rank(candidates, nullptr, req, 1000);
    // Candidate 1: chosen (artist 10: 1, album 100: 1)
    // Candidate 2: skipped (album 100 reached maxPerAlbum = 1)
    // Candidate 3: chosen (artist 10: 2, album 200: 1)
    // Candidate 4: skipped (artist 10 reached maxPerArtist = 2)
    // Candidate 5: chosen (artist 20: 1, album 400: 1)
    // Candidate 6: chosen (workId 999)
    // Candidate 7: skipped (workId 999 already selected)
    QCOMPARE(res, QList<qint64>({ 1, 3, 5, 6 }));
}

void TstRecommender::determinismAndRandomness()
{
    QList<Candidate> candidates;
    for (int i = 1; i <= 20; ++i) {
        candidates.append(makeCandidate(i));
    }

    RecRequest req1;
    req1.randomness = 5.0;
    req1.randomSeed = 123456789ULL;
    req1.recentExcludeMs = 0;

    const auto res1a = rank(candidates, nullptr, req1, 0);
    const auto res1b = rank(candidates, nullptr, req1, 0);
    QCOMPARE(res1a, res1b);

    RecRequest req2;
    req2.randomness = 5.0;
    req2.randomSeed = 987654321ULL;
    req2.recentExcludeMs = 0;

    const auto res2 = rank(candidates, nullptr, req2, 0);
    QVERIFY(res1a != res2);
}

void TstRecommender::loadCandidatesDatabaseQuery()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_rec_candidates.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.flac"));
    const qint64 f2 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.flac"));
    const qint64 f3 = DbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/3.flac"), 1000); // missing_since set -> visible = 0
    const qint64 f4 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/4.flac"));
    const qint64 f5 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/5.flac"));

    const qint64 a1 = DbHelper::insertAlbum(conn, QStringLiteral("Album Fav"));
    const qint64 a2 = DbHelper::insertAlbum(conn, QStringLiteral("Album Normal"));

    const qint64 art1 = DbHelper::insertArtist(conn, QStringLiteral("Artist Lead"));

    const qint64 t1 = DbHelper::insertTrack(conn, f1, a1);
    const qint64 t2 = DbHelper::insertTrack(conn, f2, a2);
    const qint64 t3 = DbHelper::insertTrack(conn, f3, a2);
    const qint64 t4 = DbHelper::insertTrack(conn, f4, a2);
    const qint64 t5 = DbHelper::insertTrack(conn, f5, a2);

    QVERIFY(DbHelper::insertTrackArtist(conn, t1, art1, QStringLiteral("artist"), 0));

    const qint64 dupGroup = DbHelper::insertDuplicateGroup(conn);
    QVERIFY(DbHelper::insertDuplicateMember(conn, dupGroup, t2, 0.2, 0)); // recommended = 0
    QVERIFY(DbHelper::insertDuplicateMember(conn, dupGroup, t4, 0.8, 1)); // recommended = 1

    QVERIFY(DbHelper::insertFavorite(conn, QStringLiteral("album"), a1));
    QVERIFY(DbHelper::insertPlayStats(conn, t5, 12, 3, 88888, 50000));

    const auto res = loadCandidates(db);
    QVERIFY(res.ok());
    const auto &cands = res.value();

    // t3 (visible = 0) and t2 (recommended = 0 duplicate) should be excluded
    // t1, t4, t5 should be included
    QCOMPARE(cands.size(), 3);
    QVERIFY(std::ranges::none_of(cands, [t3](const Candidate &c) { return c.trackId == t3; }));

    QCOMPARE(cands.at(0).trackId, t1);
    QCOMPARE(cands.at(0).albumId, std::optional<qint64>(a1));
    QVERIFY(cands.at(0).favorite); // Album a1 is favorite
    QCOMPARE(cands.at(0).artistIds, QList<qint64>({ art1 }));

    QCOMPARE(cands.at(1).trackId, t4);
    QVERIFY(!cands.at(1).favorite);

    QCOMPARE(cands.at(2).trackId, t5);
    QCOMPARE(cands.at(2).playCount, 12);
    QCOMPARE(cands.at(2).skipCount, 3);
    QCOMPARE(cands.at(2).lastPlayedAtMs, std::optional<qint64>(88888));
}

} // namespace

QTEST_GUILESS_MAIN(TstRecommender)

#include "tst_Recommender.moc"
