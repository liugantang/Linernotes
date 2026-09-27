// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDBusObjectPath>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <core/PlaySource.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/LibraryActions.h>
#include <ui/NowPlaying.h>
#include <ui/mpris/Mpris.h>
#include <ui/mpris/MprisPlayerAdaptor.h>
#include <ui/mpris/MprisRootAdaptor.h>

using linernotes::core::PlaySource;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::player::PlayMode;
using linernotes::ui::AppContext;
using linernotes::ui::Mpris;
using linernotes::ui::MprisPlayerAdaptor;
using linernotes::ui::MprisRootAdaptor;

namespace {

class TstMpris : public QObject {
    Q_OBJECT

private slots:
    void testInitialState();
    void testMetadataWithTrack();
    void testLoopAndShuffle();
    void testVolume();
    void testRootAdaptor();
};

void TstMpris::testInitialState()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
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

    Mpris mpris(*ctx.player(), *ctx.nowPlaying(), *ctx.coverStore());
    auto *playerAdaptor = mpris.findChild<MprisPlayerAdaptor *>();
    QVERIFY(playerAdaptor != nullptr);

    QCOMPARE(playerAdaptor->playbackStatus(), QStringLiteral("Stopped"));
    const auto meta = playerAdaptor->metadata();
    const auto trackIdPath = meta.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>();
    QCOMPARE(trackIdPath.path(), QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
}

void TstMpris::testMetadataWithTrack()
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
                                      "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1);")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (1, 1, 1, 1, 1);")));
        QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', artist = "
                                      "'Artist 1', album = 'Album One' WHERE track_id = 1;")));
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

    Mpris mpris(*ctx.player(), *ctx.nowPlaying(), *ctx.coverStore());
    auto *playerAdaptor = mpris.findChild<MprisPlayerAdaptor *>();
    QVERIFY(playerAdaptor != nullptr);

    ctx.actions()->playTracks({ 1 }, 0, PlaySource::Library);

    const auto meta = playerAdaptor->metadata();
    const auto trackIdPath = meta.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>();
    QCOMPARE(trackIdPath.path(), QStringLiteral("/org/linernotes/track/1"));
    QCOMPARE(meta.value(QStringLiteral("xesam:title")).toString(), QStringLiteral("Track 1"));
    QCOMPARE(meta.value(QStringLiteral("xesam:artist")).toStringList(),
        QStringList { QStringLiteral("Artist 1") });
    QCOMPARE(meta.value(QStringLiteral("xesam:album")).toString(), QStringLiteral("Album One"));
    QCOMPARE(meta.value(QStringLiteral("mpris:length")).toLongLong(), 60000000LL);
}

void TstMpris::testLoopAndShuffle()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
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

    Mpris mpris(*ctx.player(), *ctx.nowPlaying(), *ctx.coverStore());
    auto *playerAdaptor = mpris.findChild<MprisPlayerAdaptor *>();
    QVERIFY(playerAdaptor != nullptr);

    playerAdaptor->setLoopStatus(QStringLiteral("Track"));
    QCOMPARE(ctx.player()->queue()->mode(), PlayMode::RepeatOne);
    QCOMPARE(playerAdaptor->loopStatus(), QStringLiteral("Track"));

    playerAdaptor->setLoopStatus(QStringLiteral("Playlist"));
    QCOMPARE(ctx.player()->queue()->mode(), PlayMode::RepeatAll);
    QCOMPARE(playerAdaptor->loopStatus(), QStringLiteral("Playlist"));

    playerAdaptor->setLoopStatus(QStringLiteral("None"));
    QCOMPARE(ctx.player()->queue()->mode(), PlayMode::Sequential);
    QCOMPARE(playerAdaptor->loopStatus(), QStringLiteral("None"));

    playerAdaptor->setShuffle(true);
    QCOMPARE(ctx.player()->queue()->mode(), PlayMode::Shuffle);
    QCOMPARE(playerAdaptor->shuffle(), true);

    playerAdaptor->setShuffle(false);
    QCOMPARE(ctx.player()->queue()->mode(), PlayMode::Sequential);
    QCOMPARE(playerAdaptor->shuffle(), false);
}

void TstMpris::testVolume()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
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

    Mpris mpris(*ctx.player(), *ctx.nowPlaying(), *ctx.coverStore());
    auto *playerAdaptor = mpris.findChild<MprisPlayerAdaptor *>();
    QVERIFY(playerAdaptor != nullptr);

    playerAdaptor->setVolume(0.5);
    QTRY_COMPARE(ctx.player()->volume(), 50);
    QTRY_COMPARE(playerAdaptor->volume(), 0.5);

    playerAdaptor->setVolume(0.0);
    QTRY_COMPARE(ctx.player()->volume(), 0);
    QTRY_COMPARE(playerAdaptor->volume(), 0.0);

    playerAdaptor->setVolume(1.0);
    QTRY_COMPARE(ctx.player()->volume(), 100);
    QTRY_COMPARE(playerAdaptor->volume(), 1.0);
}

void TstMpris::testRootAdaptor()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
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

    Mpris mpris(*ctx.player(), *ctx.nowPlaying(), *ctx.coverStore());
    auto *rootAdaptor = mpris.findChild<MprisRootAdaptor *>();
    QVERIFY(rootAdaptor != nullptr);

    QCOMPARE(rootAdaptor->identity(), QStringLiteral("Linernotes"));
    QCOMPARE(rootAdaptor->desktopEntry(), QStringLiteral("linernotes"));
    QCOMPARE(rootAdaptor->canQuit(), true);
    QCOMPARE(rootAdaptor->canRaise(), true);
    QCOMPARE(rootAdaptor->canSetFullscreen(), false);
    QCOMPARE(rootAdaptor->hasTrackList(), false);

    QSignalSpy raiseSpy(&mpris, &Mpris::raiseRequested);
    rootAdaptor->Raise();
    QCOMPARE(raiseSpy.count(), 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstMpris)
#include "tst_Mpris.moc"
