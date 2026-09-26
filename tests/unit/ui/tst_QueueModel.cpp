// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/LibraryActions.h>
#include <ui/QueueModel.h>

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;
using linernotes::ui::QueueModel;

namespace {

class TstQueueModel : public QObject {
    Q_OBJECT
private slots:
    void testQueueModel();
};

void TstQueueModel::testQueueModel()
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
        QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                      "mtime, first_seen_at, scanned_at) "
                                      "VALUES (1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1), (2, 1, "
                                      "'/music/2.mp3', 120000, 1, 1, 2, 1);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                      "VALUES (1, 1, 1, 1), (2, 2, 1, 1);")));
        QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', artist = "
                                      "'Artist 1' WHERE track_id = 1;")));
        QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 2', artist = "
                                      "'Artist 2' WHERE track_id = 2;")));
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
    auto *model = ctx.queueModel();
    auto *actions = ctx.actions();
    auto *queue = ctx.player()->queue();
    QVERIFY(model != nullptr && actions != nullptr && queue != nullptr);

    // 1. 队列 t1, t2 + trackId = -1 项: title 角色分别为曲库标题与文件名
    queue->setItems({
        { .source = QStringLiteral("/music/1.mp3"), .trackId = 1 },
        { .source = QStringLiteral("/music/2.mp3"), .trackId = 2 },
        { .source = QStringLiteral("/tmp/ext.flac"), .trackId = -1 },
    });
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->data(model->index(0, 0), QueueModel::TitleRole).toString(),
        QStringLiteral("Track 1"));
    QCOMPARE(model->data(model->index(1, 0), QueueModel::TitleRole).toString(),
        QStringLiteral("Track 2"));
    QCOMPARE(model->data(model->index(2, 0), QueueModel::TitleRole).toString(),
        QStringLiteral("ext.flac"));

    // 2. 播放第 1 行后 removeRows({0, 1, 2}): 只剩当前行; clear() 在有当前项时保留当前项
    model->playAt(1);
    QCOMPARE(queue->currentIndex(), 1);
    model->removeRows({ 0, 1, 2 });
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(queue->currentIndex(), 0);
    QCOMPARE(model->data(model->index(0, 0), QueueModel::TitleRole).toString(),
        QStringLiteral("Track 2"));

    queue->append({ { .source = QStringLiteral("/music/1.mp3"), .trackId = 1 } });
    QCOMPARE(model->rowCount(), 2);
    model->clear();
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(queue->currentIndex(), 0);

    // 3. saveQueueAsPlaylist("x") 返回非 0, 写入的曲目不包含 trackId = -1 的项
    queue->setItems({
        { .source = QStringLiteral("/music/1.mp3"), .trackId = 1 },
        { .source = QStringLiteral("/tmp/ext.flac"), .trackId = -1 },
        { .source = QStringLiteral("/music/2.mp3"), .trackId = 2 },
    });
    const qint64 playlistId = actions->saveQueueAsPlaylist(QStringLiteral("x"));
    QVERIFY(playlistId > 0);

    auto conn = ctx.database().connection().value();
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT track_id, position FROM playlist_items WHERE playlist_id = ? "
                             "ORDER BY position ASC"));
    q.addBindValue(playlistId);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), 1LL);
    QCOMPARE(q.value(1).toInt(), 0);
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), 2LL);
    QCOMPARE(q.value(1).toInt(), 1);
    QVERIFY(!q.next());
}

} // namespace

QTEST_MAIN(TstQueueModel)
#include "tst_QueueModel.moc"
