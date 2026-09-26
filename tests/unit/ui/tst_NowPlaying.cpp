// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/LibraryActions.h>
#include <ui/NowPlaying.h>

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;
using linernotes::ui::NowPlaying;

namespace {

class TstNowPlaying : public QObject {
    Q_OBJECT
private slots:
    void testNowPlaying();
};

void TstNowPlaying::testNowPlaying()
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
                                      "(2, 1, '/music/2.mp3', 120000, 1, 1, 2, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (1, 1, 1, 1, 1), (2, 2, 1, 1, 1);")));
        QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', artist = "
                                      "'Artist 1', album = 'Album One' WHERE track_id = 1;")));
        QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 2', artist = "
                                      "'Artist 2', album = 'Album One' WHERE track_id = 2;")));
    }

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    AppContext ctx(options);
    QVERIFY(ctx.start().ok());
    auto *actions = ctx.actions();
    QVERIFY(actions != nullptr);
    auto *nowPlaying = ctx.nowPlaying();
    QVERIFY(nowPlaying != nullptr);
    auto *queue = ctx.player()->queue();
    QVERIFY(queue != nullptr);

    QSignalSpy spy(nowPlaying, &NowPlaying::changed);

    // 1. playTracks({1, 2}, 1) -> nowPlaying reflects track 2 data and changed is emitted
    actions->playTracks({ 1, 2 }, 1);
    QVERIFY(spy.count() > 0);
    QVERIFY(nowPlaying->hasTrack());
    QCOMPARE(nowPlaying->trackId(), 2LL);
    QCOMPARE(nowPlaying->title(), QStringLiteral("Track 2"));
    QCOMPARE(nowPlaying->artist(), QStringLiteral("Artist 2"));
    QCOMPARE(nowPlaying->album(), QStringLiteral("Album One"));
    QCOMPARE(nowPlaying->albumId(), 1LL);
    QCOMPARE(nowPlaying->durationSeconds(), 120.0);

    // 2. Put a trackId = -1 item in queue -> title is filename, albumId = 0
    spy.clear();
    queue->setItems({ linernotes::player::QueueItem {
                        .source = QStringLiteral("/tmp/external_song.flac"),
                        .trackId = -1,
                    } },
        0);
    QVERIFY(nowPlaying->hasTrack());
    QCOMPARE(nowPlaying->trackId(), -1LL);
    QCOMPARE(nowPlaying->title(), QStringLiteral("external_song.flac"));
    QCOMPARE(nowPlaying->albumId(), 0LL);
    QCOMPARE(nowPlaying->artist(), QString());
    QCOMPARE(nowPlaying->album(), QString());

    // 3. Clear queue -> hasTrack == false
    spy.clear();
    queue->clear();
    QVERIFY(!nowPlaying->hasTrack());
    QCOMPARE(nowPlaying->trackId(), -1LL);
    QCOMPARE(nowPlaying->title(), QString());
    QCOMPARE(nowPlaying->albumId(), 0LL);
}

} // namespace

QTEST_GUILESS_MAIN(TstNowPlaying)
#include "tst_NowPlaying.moc"
