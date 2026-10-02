// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <library/Database.h>
#include <library/Migrator.h>
#include <rec/Recommender.h>
#include <rec/SessionSeeds.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::rec::Seed;
using linernotes::rec::sessionSeeds;

struct DbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                 "duration_ms, first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1024, 2000, 'hash', 60000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                                 "VALUES (?, 1000, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertPlayEvent(const QSqlDatabase &db, qint64 trackId, qint64 startedAt,
        qint64 playedMs, const QVariant &durationMs, bool completed, bool skipped)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO play_events ("
            "  track_id, started_at, played_ms, track_duration_ms, completed, skipped"
            ") VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(startedAt);
        q.addBindValue(playedMs);
        q.addBindValue(durationMs);
        q.addBindValue(completed ? 1 : 0);
        q.addBindValue(skipped ? 1 : 0);
        return q.exec();
    }
};

class TstSessionSeeds : public QObject {
    Q_OBJECT

private slots:
    void positiveAndNegativeWeightsWithRecencyDecay();
    void ignoresEventsOlderThanFourHours();
    void currentTrackAndMerging();
};

void TstSessionSeeds::positiveAndNegativeWeightsWithRecencyDecay()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_seeds_decay.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 f3 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));
    const qint64 f4 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/4.mp3"));

    const qint64 t1 = DbHelper::insertTrack(conn, f1);
    const qint64 t2 = DbHelper::insertTrack(conn, f2);
    const qint64 t3 = DbHelper::insertTrack(conn, f3);
    const qint64 t4 = DbHelper::insertTrack(conn, f4);

    const qint64 nowMs = 1000000;

    // Event 0 (most recent, started_at = 100000): t1 completed -> c = 1.0, w = 1.0 * (0.8^0) = 1.0
    QVERIFY(DbHelper::insertPlayEvent(conn, t1, 100000, 60000, 60000, true, false));
    // Event 1 (started_at = 90000): t2 skipped early (10s/60s < 0.5) -> w = -0.7 * (0.8^1) = -0.56
    QVERIFY(DbHelper::insertPlayEvent(conn, t2, 90000, 10000, 60000, false, true));
    // Event 2 (started_at = 80000): t3 completed with null duration -> c = 1.0, w = 1.0 * (0.8^2) =
    // 0.64
    QVERIFY(DbHelper::insertPlayEvent(conn, t3, 80000, 40000, QVariant(), true, false));
    // Event 3 (started_at = 70000): t4 not completed, 0 duration -> c = 0.5, w = 0.5 * (0.8^3) =
    // 0.256
    QVERIFY(DbHelper::insertPlayEvent(conn, t4, 70000, 0, 0, false, false));

    const auto res = sessionSeeds(db, nowMs, std::nullopt);
    QVERIFY(res.ok());
    auto seeds = res.value();
    QCOMPARE(seeds.size(), 4);

    std::ranges::sort(seeds, { }, &Seed::trackId);

    QCOMPARE(seeds.at(0).trackId, t1);
    QVERIFY(std::abs(seeds.at(0).weight - 1.0) < 1e-6);

    QCOMPARE(seeds.at(1).trackId, t2);
    QVERIFY(std::abs(seeds.at(1).weight - (-0.56)) < 1e-6);

    QCOMPARE(seeds.at(2).trackId, t3);
    QVERIFY(std::abs(seeds.at(2).weight - 0.64) < 1e-6);

    QCOMPARE(seeds.at(3).trackId, t4);
    QVERIFY(std::abs(seeds.at(3).weight - 0.256) < 1e-6);
}

void TstSessionSeeds::ignoresEventsOlderThanFourHours()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_seeds_window.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f3 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));
    const qint64 f2 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));

    const qint64 t1 = DbHelper::insertTrack(conn, f1);
    const qint64 t2 = DbHelper::insertTrack(conn, f2);
    const qint64 t3 = DbHelper::insertTrack(conn, f3);

    const qint64 nowMs = 20000000;
    const qint64 fourHoursMs = 4LL * 60 * 60 * 1000; // 14400000

    // Within window: started 1 hour ago
    QVERIFY(DbHelper::insertPlayEvent(conn, t1, nowMs - 3600000, 60000, 60000, true, false));
    // Exactly at window boundary: started 4 hours ago
    QVERIFY(DbHelper::insertPlayEvent(conn, t2, nowMs - fourHoursMs, 60000, 60000, true, false));
    // Older than 4 hours: started 4 hours + 1 ms ago
    QVERIFY(
        DbHelper::insertPlayEvent(conn, t3, nowMs - fourHoursMs - 1, 60000, 60000, true, false));

    const auto res = sessionSeeds(db, nowMs, std::nullopt);
    QVERIFY(res.ok());
    const auto &seeds = res.value();

    QCOMPARE(seeds.size(), 2);
    QVERIFY(std::ranges::any_of(seeds, [t1](const Seed &s) { return s.trackId == t1; }));
    QVERIFY(std::ranges::any_of(seeds, [t2](const Seed &s) { return s.trackId == t2; }));
    QVERIFY(std::ranges::none_of(seeds, [t3](const Seed &s) { return s.trackId == t3; }));
}

void TstSessionSeeds::currentTrackAndMerging()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_seeds_merge.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));

    const qint64 t1 = DbHelper::insertTrack(conn, f1);
    const qint64 t2 = DbHelper::insertTrack(conn, f2);

    const qint64 nowMs = 30000000;

    // t1 event 0: started_at = 30000000 (i=0) -> w = 1.0 * (0.8^0) = 1.0
    QVERIFY(DbHelper::insertPlayEvent(conn, t1, 30000000, 60000, 60000, true, false));
    // t1 event 1: started_at = 29000000 (i=1) -> w = 0.5 * (0.8^1) = 0.4
    QVERIFY(DbHelper::insertPlayEvent(conn, t1, 29000000, 30000, 60000, false, false));
    // t2 event 2: started_at = 28000000 (i=2) -> w = 1.0 * (0.8^2) = 0.64
    QVERIFY(DbHelper::insertPlayEvent(conn, t2, 28000000, 60000, 60000, true, false));

    // Pass currentTrackId = t1 (+1.0 to t1)
    const auto res = sessionSeeds(db, nowMs, t1);
    QVERIFY(res.ok());
    auto seeds = res.value();

    QCOMPARE(seeds.size(), 2);
    std::ranges::sort(seeds, { }, &Seed::trackId);

    // t1 total weight: 1.0 (event 0) + 0.4 (event 1) + 1.0 (currentTrackId) = 2.4
    QCOMPARE(seeds.at(0).trackId, t1);
    QVERIFY(std::abs(seeds.at(0).weight - 2.4) < 1e-6);

    // t2 total weight: 0.64
    QCOMPARE(seeds.at(1).trackId, t2);
    QVERIFY(std::abs(seeds.at(1).weight - 0.64) < 1e-6);
}

} // namespace

QTEST_GUILESS_MAIN(TstSessionSeeds)

#include "tst_SessionSeeds.moc"
