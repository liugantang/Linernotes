// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <core/PlaySource.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlayEventStore.h>

#include <vector>

namespace {

using linernotes::core::PlaySource;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::PlayEvent;
using linernotes::library::PlayEventStore;

class TstPlayEventStore : public QObject {
    Q_OBJECT

private slots:
    void insertUpdateAndRoundtrip();
    void playSourceEnumRoundtrip();
    void eventsBetweenBoundaries();
};

void TstPlayEventStore::insertUpdateAndRoundtrip()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    // Insert dummy track with id = 42 so foreign key constraints are satisfied
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                      "VALUES (1, '/music', 1, 100);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                      "mtime, first_seen_at, scanned_at) VALUES "
                                      "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                      "VALUES (42, 1, 1, 1);")));
    }

    PlayEventStore store(db);

    // 1. Insert event with minimal / nullopt fields
    PlayEvent ev1;
    ev1.startedAtMs = 1000;
    ev1.playedMs = 0;
    ev1.pausedMs = 0;
    ev1.playSource = PlaySource::Library;

    const auto idRes1 = store.insert(ev1);
    QVERIFY(idRes1.ok());
    ev1.id = idRes1.value();
    QVERIFY(ev1.id > 0);

    // Read back ev1
    const auto readRes1 = store.eventsBetween(500, 1500);
    QVERIFY(readRes1.ok());
    QCOMPARE(readRes1.value().size(), 1);
    QCOMPARE(readRes1.value().first(), ev1);

    // 2. Update ev1 with full fields
    ev1.trackId = 42;
    ev1.endedAtMs = 61000;
    ev1.playedMs = 50000;
    ev1.pausedMs = 10000;
    ev1.durationMs = 180000;
    ev1.completed = false;
    ev1.skipped = true;
    ev1.skipPositionMs = 50000;
    ev1.device = QStringLiteral("Default Audio");
    ev1.snapshot = QStringLiteral(R"({"title":"Test Song","artist":"Test Artist"})");

    const auto updateRes = store.update(ev1);
    QVERIFY(updateRes.ok());

    // Read back updated ev1
    const auto readRes2 = store.eventsBetween(500, 1500);
    QVERIFY(readRes2.ok());
    QCOMPARE(readRes2.value().size(), 1);
    QCOMPARE(readRes2.value().first(), ev1);
}

void TstPlayEventStore::playSourceEnumRoundtrip()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    PlayEventStore store(db);

    const std::vector<PlaySource> sources = {
        PlaySource::Unknown,
        PlaySource::Library,
        PlaySource::Album,
        PlaySource::Artist,
        PlaySource::Playlist,
        PlaySource::Search,
        PlaySource::Queue,
        PlaySource::Nlq,
        PlaySource::Dj,
        PlaySource::External,
    };

    qint64 t = 10000;
    for (const auto src : sources) {
        PlayEvent ev;
        ev.startedAtMs = t;
        ev.playSource = src;
        const auto res = store.insert(ev);
        QVERIFY(res.ok());
        ev.id = res.value();

        const auto fetched = store.eventsBetween(t, t + 1);
        QVERIFY(fetched.ok());
        QCOMPARE(fetched.value().size(), 1);
        QCOMPARE(fetched.value().first().playSource, src);

        t += 1000;
    }
}

void TstPlayEventStore::eventsBetweenBoundaries()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    PlayEventStore store(db);

    PlayEvent ev1;
    ev1.startedAtMs = 1000;
    PlayEvent ev2;
    ev2.startedAtMs = 2000;
    PlayEvent ev3;
    ev3.startedAtMs = 3000;

    QVERIFY(store.insert(ev1).ok());
    QVERIFY(store.insert(ev2).ok());
    QVERIFY(store.insert(ev3).ok());

    // [1000, 3000) should contain 1000 and 2000, but not 3000
    const auto res1 = store.eventsBetween(1000, 3000);
    QVERIFY(res1.ok());
    QCOMPARE(res1.value().size(), 2);
    QCOMPARE(res1.value().at(0).startedAtMs, 1000);
    QCOMPARE(res1.value().at(1).startedAtMs, 2000);

    // [2000, 4000) should contain 2000 and 3000
    const auto res2 = store.eventsBetween(2000, 4000);
    QVERIFY(res2.ok());
    QCOMPARE(res2.value().size(), 2);
    QCOMPARE(res2.value().at(0).startedAtMs, 2000);
    QCOMPARE(res2.value().at(1).startedAtMs, 3000);

    // [1500, 2500) should contain only 2000
    const auto res3 = store.eventsBetween(1500, 2500);
    QVERIFY(res3.ok());
    QCOMPARE(res3.value().size(), 1);
    QCOMPARE(res3.value().at(0).startedAtMs, 2000);

    // [3000, 3000) empty
    const auto res4 = store.eventsBetween(3000, 3000);
    QVERIFY(res4.ok());
    QVERIFY(res4.value().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstPlayEventStore)
#include "tst_PlayEventStore.moc"
