// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <core/PlaySource.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/LibraryActions.h>

using linernotes::core::PlaySource;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;

namespace {

class TstLibraryActions : public QObject {
    Q_OBJECT
private slots:
    void testLibraryActions();
    void testOpenFiles();
};

void TstLibraryActions::testLibraryActions()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
        const auto conn = db.connection().value();
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                      "VALUES (1, '/music', 0, 100);")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO albums (id, grouping_key, title, album_artist, created_at) "
                           "VALUES (1, 'K1', 'Album One', 'Artist One', 100);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                      "mtime, first_seen_at, scanned_at) VALUES "
                                      "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1), "
                                      "(2, 1, '/music/2.mp3', 120000, 1, 1, 2, 1), "
                                      "(3, 1, '/music/3.mp3', 180000, 1, 1, 3, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (1, 1, 1, 1, 1), (2, 2, 1, 1, 1), (3, 3, 1, 1, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'Track 1' WHERE track_id = 1;")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'Track 2' WHERE track_id = 2;")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'Track 3' WHERE track_id = 3;")));
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
        .backupDir = { },
    };

    AppContext ctx(settings, options);
    QVERIFY(ctx.start().ok());
    auto *actions = ctx.actions();
    QVERIFY(actions != nullptr);
    auto *queue = ctx.player()->queue();

    // 1. playTracks({t3, 999, t1}, 2) -> items: t3, t1; current is t1 (index 1)
    actions->playTracks({ 3, 999, 1 }, 2, PlaySource::Library);
    QCOMPARE(queue->count(), 2);
    QCOMPARE(queue->at(0).trackId, 3LL);
    QCOMPARE(queue->at(0).source, QStringLiteral("/music/3.mp3"));
    QCOMPARE(queue->at(0).playSource, PlaySource::Library);
    QCOMPARE(queue->at(1).trackId, 1LL);
    QCOMPARE(queue->at(1).source, QStringLiteral("/music/1.mp3"));
    QCOMPARE(queue->at(1).playSource, PlaySource::Library);
    QCOMPARE(queue->currentIndex(), 1);

    // 2. enqueue -> appends to queue tail; playNext -> inserts after current item (t1 at index 1)
    actions->enqueue({ 2 });
    QCOMPARE(queue->count(), 3);
    QCOMPARE(queue->at(2).trackId, 2LL);
    QCOMPARE(queue->at(2).playSource, PlaySource::Queue);

    actions->playNext({ 3 });
    QCOMPARE(queue->count(), 4);
    QCOMPARE(queue->at(2).trackId, 3LL);
    QCOMPARE(queue->at(2).playSource, PlaySource::Queue);
    QCOMPARE(queue->at(3).trackId, 2LL);
    QCOMPARE(queue->at(3).playSource, PlaySource::Queue);

    // 3. albumInfo returns correct info; non-existent returns empty map
    const auto info = actions->albumInfo(1);
    QCOMPARE(info.value(QStringLiteral("title")).toString(), QStringLiteral("Album One"));
    QCOMPARE(info.value(QStringLiteral("trackCount")).toInt(), 3);

    const auto emptyInfo = actions->albumInfo(999);
    QVERIFY(emptyInfo.isEmpty());
}

void TstLibraryActions::testOpenFiles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    const QString inLibraryFile = tempDir.filePath(QStringLiteral("in_lib.mp3"));
    const QString outLibraryFile = tempDir.filePath(QStringLiteral("out_lib.mp3"));
    const QString nonExistentFile = tempDir.filePath(QStringLiteral("not_found.mp3"));

    // Create real dummy files on disk
    {
        QFile f1(inLibraryFile);
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write("dummy");
        f1.close();

        QFile f2(outLibraryFile);
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write("dummy");
        f2.close();
    }

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
        const auto conn = db.connection().value();
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                      "VALUES (1, '%1', 0, 100);")
                .arg(tempDir.path())));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                      "mtime, first_seen_at, scanned_at) VALUES "
                                      "(10, 1, '%1', 60000, 1, 1, 1, 1);")
                .arg(inLibraryFile)));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                      "VALUES (42, 10, 1, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("UPDATE effective_metadata SET title = 'In Lib' WHERE track_id = 42;")));
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
        .backupDir = { },
    };

    AppContext ctx(settings, options);
    QVERIFY(ctx.start().ok());
    auto *actions = ctx.actions();
    QVERIFY(actions != nullptr);
    auto *queue = ctx.player()->queue();

    // openFiles with in-library file, out-of-library temp file, and non-existent file
    actions->openFiles({ inLibraryFile, outLibraryFile, nonExistentFile });

    QCOMPARE(queue->count(), 2);
    QCOMPARE(queue->at(0).trackId, 42LL);
    QCOMPARE(queue->at(0).source, inLibraryFile);
    QCOMPARE(queue->at(0).playSource, PlaySource::External);

    QCOMPARE(queue->at(1).trackId, -1LL);
    QCOMPARE(queue->at(1).source, outLibraryFile);
    QCOMPARE(queue->at(1).playSource, PlaySource::External);

    QCOMPARE(queue->currentIndex(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstLibraryActions)
#include "tst_LibraryActions.moc"
