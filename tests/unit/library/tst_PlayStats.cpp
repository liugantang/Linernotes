// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlayCountRule.h>
#include <library/PlayEventStore.h>
#include <library/PlayStats.h>

#include <cmath>
#include <optional>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::PlayCountRule;
using linernotes::library::PlayEvent;
using linernotes::library::PlayEventStore;
using linernotes::library::PlayStats;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(QStringLiteral("/music"));
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(
        const QSqlDatabase &db, qint64 rootId, const QString &path, qint64 durationMs = 200000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1048576, 2000, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertAlbum(const QSqlDatabase &db, qint64 albumId, const QString &title)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO albums (id, grouping_key, title, created_at) VALUES (?, ?, ?, 1000);"));
        q.addBindValue(albumId);
        q.addBindValue(title);
        q.addBindValue(title);
        return q.exec() ? albumId : -1;
    }

    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 trackId, qint64 fileId, const QVariant &albumId = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (?, ?, ?, 1000, 1000);"));
        q.addBindValue(trackId);
        q.addBindValue(fileId);
        q.addBindValue(albumId.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : albumId);
        return q.exec() ? trackId : -1;
    }
};

class TstPlayStats : public QObject {
    Q_OBJECT

private slots:
    void refreshTrackComputesStatsCorrectly();
    void rebuildAllUpdatesRuleAndClearsOrphans();
    void albumCompletionCalculatedCorrectly();
};

void TstPlayStats::refreshTrackComputesStatsCorrectly()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("stats_refresh.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/track1.mp3"), 200000);
    const qint64 trackId = TestDbHelper::insertTrack(conn, 10, fileId);
    QVERIFY(trackId > 0);

    PlayEventStore store(db);

    // ev1: Valid play (120s / 200s = 60% >= 50%)
    PlayEvent ev1;
    ev1.trackId = trackId;
    ev1.startedAtMs = 1000;
    ev1.playedMs = 120000;
    ev1.durationMs = 200000;
    ev1.completed = true;
    ev1.skipped = false;
    QVERIFY(store.insert(ev1).ok());

    // ev2: Invalid play (30s / 200s = 15% < 50%), skipped = 1
    PlayEvent ev2;
    ev2.trackId = trackId;
    ev2.startedAtMs = 2000;
    ev2.playedMs = 30000;
    ev2.durationMs = 200000;
    ev2.completed = false;
    ev2.skipped = true;
    QVERIFY(store.insert(ev2).ok());

    // ev3: Valid play by minSeconds (duration unknown, playedMs 250s >= 240s)
    PlayEvent ev3;
    ev3.trackId = trackId;
    ev3.startedAtMs = 3000;
    ev3.playedMs = 250000;
    ev3.durationMs = std::nullopt;
    ev3.completed = false;
    ev3.skipped = false;
    QVERIFY(store.insert(ev3).ok());

    // ev4: Valid play (220s / 200s capped at 1.0)
    PlayEvent ev4;
    ev4.trackId = trackId;
    ev4.startedAtMs = 4000;
    ev4.playedMs = 220000;
    ev4.durationMs = 200000;
    ev4.completed = true;
    ev4.skipped = false;
    QVERIFY(store.insert(ev4).ok());

    PlayStats playStats(db);
    const PlayCountRule rule { .minPercent = 50, .minSeconds = 240 };
    const auto refreshRes = playStats.refreshTrack(trackId, rule);
    QVERIFY(refreshRes.ok());

    const auto statsRes = playStats.track(trackId);
    QVERIFY(statsRes.ok());
    const auto stats = statsRes.value();

    QCOMPARE(stats.playCount, 3);
    QCOMPARE(stats.lastPlayedAtMs, std::optional<qint64>(4000LL));
    QCOMPARE(stats.totalPlayedMs, 620000LL);
    QCOMPARE(stats.skipCount, 1);
    if (!stats.avgCompletion.has_value()) {
        QFAIL("stats.avgCompletion is nullopt");
        return;
    }
    // avg(120/200, 30/200, min(220/200, 1.0)) = avg(0.6, 0.15, 1.0) = 1.75 / 3
    const double expectedAvg = 1.75 / 3.0;
    QVERIFY(std::abs(*stats.avgCompletion - expectedAvg) < 1e-6);
}

void TstPlayStats::rebuildAllUpdatesRuleAndClearsOrphans()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("stats_rebuild.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 file1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/t1.mp3"), 200000);
    const qint64 file2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/t2.mp3"), 100000);
    TestDbHelper::insertTrack(conn, 10, file1);
    TestDbHelper::insertTrack(conn, 20, file2);

    PlayEventStore store(db);

    // Track 10: ev1 (60%), ev2 (250s, no dur), ev3 (100%)
    PlayEvent ev1;
    ev1.trackId = 10;
    ev1.startedAtMs = 1000;
    ev1.playedMs = 120000;
    ev1.durationMs = 200000;
    QVERIFY(store.insert(ev1).ok());

    PlayEvent ev2;
    ev2.trackId = 10;
    ev2.startedAtMs = 2000;
    ev2.playedMs = 250000;
    ev2.durationMs = std::nullopt;
    QVERIFY(store.insert(ev2).ok());

    PlayEvent ev3;
    ev3.trackId = 10;
    ev3.startedAtMs = 3000;
    ev3.playedMs = 200000;
    ev3.durationMs = 200000;
    QVERIFY(store.insert(ev3).ok());

    // Track 20: ev4 (60%)
    PlayEvent ev4;
    ev4.trackId = 20;
    ev4.startedAtMs = 4000;
    ev4.playedMs = 60000;
    ev4.durationMs = 100000;
    QVERIFY(store.insert(ev4).ok());

    PlayStats playStats(db);
    const PlayCountRule defaultRule { .minPercent = 50, .minSeconds = 240 };
    QVERIFY(playStats.rebuildAll(defaultRule).ok());

    QCOMPARE(playStats.track(10).value().playCount, 3);
    QCOMPARE(playStats.track(20).value().playCount, 1);

    // Change rule to 80%, 300s -> ev1 (60% < 80%), ev2 (250s < 300s), ev4 (60% < 80%) become
    // invalid
    const PlayCountRule strictRule { .minPercent = 80, .minSeconds = 300 };
    QVERIFY(playStats.rebuildAll(strictRule).ok());

    const auto stats10 = playStats.track(10).value();
    QCOMPARE(stats10.playCount, 1);
    QCOMPARE(stats10.lastPlayedAtMs, std::optional<qint64>(3000LL));

    const auto stats20 = playStats.track(20).value();
    QCOMPARE(stats20.playCount, 0);
    QCOMPARE(stats20.lastPlayedAtMs, std::nullopt);

    // Delete all events for Track 20 -> rebuildAll should clear stats for Track 20
    QSqlQuery delQ(conn);
    QVERIFY(delQ.exec(QStringLiteral("DELETE FROM play_events WHERE track_id = 20;")));
    QVERIFY(playStats.rebuildAll(strictRule).ok());

    const auto emptyStats20 = playStats.track(20).value();
    QCOMPARE(emptyStats20.playCount, 0);
    QCOMPARE(emptyStats20.totalPlayedMs, 0LL);
    QCOMPARE(emptyStats20.skipCount, 0);
    QCOMPARE(emptyStats20.lastPlayedAtMs, std::nullopt);
    QCOMPARE(emptyStats20.avgCompletion, std::nullopt);
}

void TstPlayStats::albumCompletionCalculatedCorrectly()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("stats_album.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 albumId = TestDbHelper::insertAlbum(conn, 100, QStringLiteral("Album 100"));
    const qint64 f1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/a1.mp3"), 100000);
    const qint64 f2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/a2.mp3"), 100000);
    const qint64 f3
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/a3.mp3"), 100000);
    TestDbHelper::insertTrack(conn, 101, f1, albumId);
    TestDbHelper::insertTrack(conn, 102, f2, albumId);
    TestDbHelper::insertTrack(conn, 103, f3, albumId);

    PlayEventStore store(db);

    // Track 101 played
    PlayEvent ev1;
    ev1.trackId = 101;
    ev1.startedAtMs = 1000;
    ev1.playedMs = 80000;
    ev1.durationMs = 100000;
    QVERIFY(store.insert(ev1).ok());

    // Track 102 played
    PlayEvent ev2;
    ev2.trackId = 102;
    ev2.startedAtMs = 2000;
    ev2.playedMs = 90000;
    ev2.durationMs = 100000;
    QVERIFY(store.insert(ev2).ok());

    PlayStats playStats(db);
    const PlayCountRule rule { .minPercent = 50, .minSeconds = 240 };
    QVERIFY(playStats.rebuildAll(rule).ok());

    // 3 tracks in album, 2 played -> completion = 2 / 3
    const auto albumRes = playStats.album(albumId);
    QVERIFY(albumRes.ok());
    if (!albumRes.value().has_value()) {
        QFAIL("albumCompletion is nullopt");
        return;
    }
    const auto &comp = *albumRes.value();

    QCOMPARE(comp.trackCount, 3);
    QCOMPARE(comp.playedTrackCount, 2);
    QVERIFY(std::abs(comp.completion - (2.0 / 3.0)) < 1e-6);

    // Non-existent album
    const auto nonExistRes = playStats.album(9999);
    QVERIFY(nonExistRes.ok());
    QCOMPARE(nonExistRes.value(), std::nullopt);
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayStats)
#include "tst_PlayStats.moc"
