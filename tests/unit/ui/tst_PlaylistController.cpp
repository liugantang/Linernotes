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
#include <ui/PlaylistController.h>
#include <ui/PlaylistListModel.h>

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;
using linernotes::ui::PlaylistController;
using linernotes::ui::PlaylistListModel;

namespace {

class TstPlaylistController : public QObject {
    Q_OBJECT

private slots:
    void testPlaylistController();
};

void TstPlaylistController::testPlaylistController()
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

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
    };

    AppContext ctx(options);
    QVERIFY(ctx.start().ok());
    auto *controller = ctx.playlists();
    QVERIFY(controller != nullptr);
    auto *model = controller->model();
    QVERIFY(model != nullptr);

    // 1. 空名字 createManual 返回 0
    QCOMPARE(controller->createManual(QString()), 0LL);
    QCOMPARE(controller->createManual(QStringLiteral("   ")), 0LL);

    // 2. createManual -> model 行数与名字、isSmart 属性、info()
    QSignalSpy spyPlaylistsChanged(controller, &PlaylistController::playlistsChanged);
    const qint64 id1 = controller->createManual(QStringLiteral("Favorites"), { 1, 2 });
    QVERIFY(id1 > 0);
    QCOMPARE(spyPlaylistsChanged.count(), 1);
    QCOMPARE(model->count(), 1);
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0, 0), PlaylistListModel::NameRole).toString(),
        QStringLiteral("Favorites"));
    QCOMPARE(model->data(model->index(0, 0), PlaylistListModel::PlaylistIdRole).toLongLong(), id1);
    QCOMPARE(model->data(model->index(0, 0), PlaylistListModel::IsSmartRole).toBool(), false);
    QCOMPARE(model->nameOf(id1), QStringLiteral("Favorites"));
    QVERIFY(model->contains(id1));
    QVERIFY(controller->isManual(id1));
    QVERIFY(controller->info(id1).has_value());

    // 3. rename -> model 更新且发出 playlistsChanged
    spyPlaylistsChanged.clear();
    QVERIFY(controller->rename(id1, QStringLiteral("My Favorites")));
    QCOMPARE(spyPlaylistsChanged.count(), 1);
    QCOMPARE(model->nameOf(id1), QStringLiteral("My Favorites"));
    QCOMPARE(model->data(model->index(0, 0), PlaylistListModel::NameRole).toString(),
        QStringLiteral("My Favorites"));

    // 4. addTracks 返回实际加入数并发出 playlistContentChanged(id)
    QSignalSpy spyContentChanged(controller, &PlaylistController::playlistContentChanged);
    const int added = controller->addTracks(id1, { 3, 2 }); // track 2 is already present
    QCOMPARE(added, 1);
    QCOMPARE(spyContentChanged.count(), 1);
    QCOMPARE(spyContentChanged.takeFirst().at(0).toLongLong(), id1);

    // removeTracks 发出 playlistContentChanged(id)
    QVERIFY(controller->removeTracks(id1, { 1 }));
    QCOMPARE(spyContentChanged.count(), 1);
    QCOMPARE(spyContentChanged.takeFirst().at(0).toLongLong(), id1);

    // 5. saveQueue
    auto *queue = ctx.player()->queue();
    queue->setItems({
        { .source = QStringLiteral("/music/1.mp3"), .trackId = 1 },
        { .source = QStringLiteral("/tmp/ext.flac"), .trackId = -1 },
        { .source = QStringLiteral("/music/2.mp3"), .trackId = 2 },
    });
    spyPlaylistsChanged.clear();
    const qint64 queuePlaylistId = controller->saveQueue(QStringLiteral("Queue Playlist"));
    QVERIFY(queuePlaylistId > 0);
    QCOMPARE(spyPlaylistsChanged.count(), 1);
    QCOMPARE(model->count(), 2);
    QCOMPARE(model->nameOf(queuePlaylistId), QStringLiteral("Queue Playlist"));

    // 6. remove -> model 更新且发出 playlistsChanged
    spyPlaylistsChanged.clear();
    QVERIFY(controller->remove(id1));
    QCOMPARE(spyPlaylistsChanged.count(), 1);
    QCOMPARE(model->count(), 1);
    QVERIFY(!model->contains(id1));
    QVERIFY(!controller->info(id1).has_value());
}

} // namespace

QTEST_GUILESS_MAIN(TstPlaylistController)
#include "tst_PlaylistController.moc"
