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
#include <rec/TasteSeeds.h>

#include <algorithm>
#include <cmath>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::rec::Seed;
using linernotes::rec::tasteSeeds;

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

    static bool insertFavorite(
        const QSqlDatabase &db, const QString &entityType, qint64 entityId, qint64 createdAt)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO favorites (entity_type, entity_id, created_at) "
                                 "VALUES (?, ?, ?);"));
        q.addBindValue(entityType);
        q.addBindValue(entityId);
        q.addBindValue(createdAt);
        return q.exec();
    }
};

class TstTasteSeeds : public QObject {
    Q_OBJECT

private slots:
    void ignoresEventsOlderThanThirtyDaysAndRanksRecency();
    void favoritesWeightAndMergingWithPlayEvents();
};

void TstTasteSeeds::ignoresEventsOlderThanThirtyDaysAndRanksRecency()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_taste_window.db")));
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

    constexpr qint64 kDayMs = 24LL * 3600 * 1000;
    const qint64 nowMs = 100LL * kDayMs;

    // t1: 1 day ago, completed -> c = 1.0, w = 1.0 * (0.5^(1/14))
    QVERIFY(DbHelper::insertPlayEvent(conn, t1, nowMs - (1LL * kDayMs), 60000, 60000, true, false));
    // t2: 14 days ago, completed -> c = 1.0, w = 1.0 * (0.5^(14/14)) = 0.5
    QVERIFY(
        DbHelper::insertPlayEvent(conn, t2, nowMs - (14LL * kDayMs), 60000, 60000, true, false));
    // t3: 31 days ago (older than 30 days) -> should be ignored
    QVERIFY(
        DbHelper::insertPlayEvent(conn, t3, nowMs - (31LL * kDayMs), 60000, 60000, true, false));
    // t4: 2 days ago, skipped early (10s / 60s < 0.5) -> skipped && c < 0.5 -> ignored
    QVERIFY(DbHelper::insertPlayEvent(conn, t4, nowMs - (2LL * kDayMs), 10000, 60000, false, true));

    const auto res = tasteSeeds(db, nowMs);
    QVERIFY(res.ok());
    auto seeds = res.value();

    QCOMPARE(seeds.size(), 2);
    std::ranges::sort(seeds, { }, &Seed::trackId);

    const double expectedT1Weight = 1.0 * std::pow(0.5, 1.0 / 14.0);
    const double expectedT2Weight = 0.5;

    QCOMPARE(seeds.at(0).trackId, t1);
    QVERIFY(std::abs(seeds.at(0).weight - expectedT1Weight) < 1e-6);

    QCOMPARE(seeds.at(1).trackId, t2);
    QVERIFY(std::abs(seeds.at(1).weight - expectedT2Weight) < 1e-6);

    QVERIFY(seeds.at(0).weight > seeds.at(1).weight);
}

void TstTasteSeeds::favoritesWeightAndMergingWithPlayEvents()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_taste_fav.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 f3 = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));

    const qint64 t1 = DbHelper::insertTrack(conn, f1);
    const qint64 t2 = DbHelper::insertTrack(conn, f2);
    const qint64 t3 = DbHelper::insertTrack(conn, f3);

    constexpr qint64 kDayMs = 24LL * 3600 * 1000;
    const qint64 nowMs = 50LL * kDayMs;

    // t1: played at nowMs (c = 1.0, decay = 1.0) AND favorited (+0.5)
    QVERIFY(DbHelper::insertPlayEvent(conn, t1, nowMs, 60000, 60000, true, false));
    QVERIFY(DbHelper::insertFavorite(conn, QStringLiteral("track"), t1, nowMs));

    // t2: only favorited (+0.5)
    QVERIFY(DbHelper::insertFavorite(conn, QStringLiteral("track"), t2, nowMs));

    // t3: only played at nowMs (c = 1.0, decay = 1.0)
    QVERIFY(DbHelper::insertPlayEvent(conn, t3, nowMs, 60000, 60000, true, false));

    const auto res = tasteSeeds(db, nowMs);
    QVERIFY(res.ok());
    auto seeds = res.value();

    QCOMPARE(seeds.size(), 3);
    std::ranges::sort(seeds, { }, &Seed::trackId);

    // t1: 1.0 + 0.5 = 1.5
    QCOMPARE(seeds.at(0).trackId, t1);
    QVERIFY(std::abs(seeds.at(0).weight - 1.5) < 1e-6);

    // t2: 0.5
    QCOMPARE(seeds.at(1).trackId, t2);
    QVERIFY(std::abs(seeds.at(1).weight - 0.5) < 1e-6);

    // t3: 1.0
    QCOMPARE(seeds.at(2).trackId, t3);
    QVERIFY(std::abs(seeds.at(2).weight - 1.0) < 1e-6);
}

} // namespace

QTEST_GUILESS_MAIN(TstTasteSeeds)

#include "tst_TasteSeeds.moc"
