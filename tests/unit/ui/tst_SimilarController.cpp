// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/PlaylistController.h>
#include <ui/SimilarController.h>

namespace {

using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;

class TstSimilarController : public QObject {
    Q_OBJECT

private slots:
    void playlistFromTrack();
    void playlistFromAlbum();
    void saveAsPlaylist();
    void findResetsPlaylistMode();
};

void setupDatabase(const QString &dbPath)
{
    Database db(dbPath);
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();
    QSqlQuery q(conn);

    QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                  "VALUES (1, '/music', 0, 100);")));

    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO albums (id, grouping_key, title, album_artist, year, created_at) VALUES "
        "(1, 'K1', 'Album One', 'Artist One', 2020, 100), "
        "(2, 'K2', 'Album Two', 'Artist Two', 2021, 100), "
        "(3, 'K3', 'Album Three', 'Artist Three', 2022, 100);")));

    QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                                  "first_seen_at, scanned_at) "
                                  "VALUES "
                                  "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1), "
                                  "(2, 1, '/music/2.mp3', 120000, 1, 1, 2, 1), "
                                  "(3, 1, '/music/3.mp3', 180000, 1, 1, 3, 1), "
                                  "(4, 1, '/music/4.mp3', 240000, 1, 1, 4, 1), "
                                  "(5, 1, '/music/5.mp3', 300000, 1, 1, 5, 1), "
                                  "(6, 1, '/music/6.mp3', 360000, 1, 1, 6, 1);")));

    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) VALUES "
        "(1, 1, 1, 1, 1), (2, 2, 1, 1, 1), "
        "(3, 3, 2, 1, 1), (4, 4, 2, 1, 1), "
        "(5, 5, 3, 1, 1), (6, 6, 3, 1, 1);")));

    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', artist = "
                                  "'Artist One', album = 'Album One' "
                                  "WHERE track_id = 1;")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 2', artist = "
                                  "'Artist One', album = 'Album One' "
                                  "WHERE track_id = 2;")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 3', artist = "
                                  "'Artist Two', album = 'Album Two' "
                                  "WHERE track_id = 3;")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 4', artist = "
                                  "'Artist Two', album = 'Album Two' "
                                  "WHERE track_id = 4;")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 5', artist = "
                                  "'Artist Three', album = 'Album Three' "
                                  "WHERE track_id = 5;")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 6', artist = "
                                  "'Artist Three', album = 'Album Three' "
                                  "WHERE track_id = 6;")));
}

void TstSimilarController::playlistFromTrack()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    setupDatabase(dbPath);

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

    auto *similar = ctx.similar();
    QVERIFY(similar != nullptr);

    QSignalSpy resultsSpy(similar, &linernotes::ui::SimilarController::resultsChanged);
    QSignalSpy openSpy(similar, &linernotes::ui::SimilarController::openRequested);

    similar->playlistFromTrack(1);

    QCOMPARE(resultsSpy.count(), 1);
    QCOMPARE(openSpy.count(), 1);
    QVERIFY(similar->isPlaylistMode());
    QVERIFY(similar->isSeedAnalyzed());
    QVERIFY(!similar->seedTitle().isEmpty());

    const QVariantList rows = similar->rows();
    QVERIFY(!rows.isEmpty());

    // First row is the seed track
    const auto firstRowMap = rows.first().toMap();
    QCOMPARE(firstRowMap.value(QStringLiteral("trackId")).toLongLong(), 1LL);

    // Remaining rows do not contain the seed track
    for (int i = 1; i < rows.size(); ++i) {
        const auto map = rows.at(i).toMap();
        QVERIFY(map.value(QStringLiteral("trackId")).toLongLong() != 1LL);
    }
}

void TstSimilarController::playlistFromAlbum()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    setupDatabase(dbPath);

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

    auto *similar = ctx.similar();
    QVERIFY(similar != nullptr);

    QSignalSpy resultsSpy(similar, &linernotes::ui::SimilarController::resultsChanged);
    QSignalSpy openSpy(similar, &linernotes::ui::SimilarController::openRequested);

    similar->playlistFromAlbum(1);

    QCOMPARE(resultsSpy.count(), 1);
    QCOMPARE(openSpy.count(), 1);
    QVERIFY(similar->isPlaylistMode());
    QVERIFY(similar->isSeedAnalyzed());
    QVERIFY(!similar->seedTitle().isEmpty());

    const QVariantList rows = similar->rows();
    QVERIFY(!rows.isEmpty());

    // Results must not contain any tracks from Album 1 (tracks 1 and 2)
    for (const auto &row : rows) {
        const auto map = row.toMap();
        const qint64 trackId = map.value(QStringLiteral("trackId")).toLongLong();
        QVERIFY(trackId != 1LL);
        QVERIFY(trackId != 2LL);
        const qint64 albumId = map.value(QStringLiteral("albumId")).toLongLong();
        QVERIFY(albumId != 1LL);
    }
}

void TstSimilarController::saveAsPlaylist()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    setupDatabase(dbPath);

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

    auto *similar = ctx.similar();
    QVERIFY(similar != nullptr);

    similar->playlistFromTrack(1);
    const auto rowCount = similar->rows().size();
    QVERIFY(rowCount > 0);

    const qint64 playlistId = similar->saveAsPlaylist(QStringLiteral("Similar to Track 1"));
    QVERIFY(playlistId > 0);

    auto *playlists = ctx.playlists();
    QVERIFY(playlists != nullptr);

    const auto infoOpt = playlists->info(playlistId);
    QVERIFY(infoOpt.has_value());
    if (infoOpt.has_value()) {
        QCOMPARE(infoOpt->name, QStringLiteral("Similar to Track 1"));
    }

    Database db(dbPath);
    QVERIFY(db.open(Migrator()).ok());
    QSqlQuery q(db.connection().value());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = %1;")
            .arg(playlistId)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), static_cast<qint64>(rowCount));
}

void TstSimilarController::findResetsPlaylistMode()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    setupDatabase(dbPath);

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

    auto *similar = ctx.similar();
    QVERIFY(similar != nullptr);

    similar->playlistFromTrack(1);
    QVERIFY(similar->isPlaylistMode());

    similar->find(1);
    QVERIFY(!similar->isPlaylistMode());
}

} // namespace

QTEST_GUILESS_MAIN(TstSimilarController)
#include "tst_SimilarController.moc"
