// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/PlaylistController.h>
#include <ui/PlaylistListModel.h>

using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AppContext;
using linernotes::ui::PlaylistController;
using linernotes::ui::PlaylistListModel;
namespace library = linernotes::library;

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

    // 7. createSmart & rule()
    library::SmartRule rule;
    rule.match = library::SmartMatch::All;
    library::SmartCondition c1;
    c1.field = library::SmartField::Year;
    c1.op = library::SmartOp::Between;
    c1.value = 1990;
    c1.value2 = 2000;
    rule.conditions.append(c1);
    rule.sortKey = library::TrackSortKey::Title;
    rule.sortOrder = Qt::DescendingOrder;
    rule.limit = 20;

    // Invalid map (unknown field) returns 0
    library::SmartRule invalidRule = rule;
    library::SmartCondition invC;
    invC.field = library::SmartField::Year;
    invC.op = library::SmartOp::Contains; // Invalid for Year
    invC.value = 123;
    invalidRule.conditions.append(invC);
    QCOMPARE(controller->createSmart(QStringLiteral("Invalid Smart"), invalidRule), 0LL);

    // Valid createSmart
    spyPlaylistsChanged.clear();
    const qint64 smartId = controller->createSmart(QStringLiteral("90s Hits"), rule);
    QVERIFY(smartId > 0);
    QCOMPARE(spyPlaylistsChanged.count(), 1);
    QVERIFY(!controller->isManual(smartId));
    QCOMPARE(model->nameOf(smartId), QStringLiteral("90s Hits"));

    // rule(id) read back equals input
    const library::SmartRule readRule = controller->rule(smartId);
    QCOMPARE(readRule.match, library::SmartMatch::All);
    QCOMPARE(readRule.sortKey, library::TrackSortKey::Title);
    QCOMPARE(readRule.sortOrder, Qt::DescendingOrder);
    QCOMPARE(readRule.limit.value_or(0), 20);
    QCOMPARE(readRule.conditions.size(), 1);
    QCOMPARE(readRule.conditions.at(0).field, library::SmartField::Year);
    QCOMPARE(readRule.conditions.at(0).op, library::SmartOp::Between);
    QCOMPARE(readRule.conditions.at(0).value.toInt(), 1990);
    QCOMPARE(readRule.conditions.at(0).value2.toInt(), 2000);

    // Non-smart playlist rule() returns empty
    QCOMPARE(controller->rule(queuePlaylistId).conditions.size(), 0);

    // 8. setRule emits playlistContentChanged(id)
    spyContentChanged.clear();
    library::SmartRule updatedRule = readRule;
    updatedRule.limit = 50;
    QVERIFY(controller->setRule(smartId, updatedRule));
    QCOMPARE(spyContentChanged.count(), 1);
    QCOMPARE(spyContentChanged.takeFirst().at(0).toLongLong(), smartId);
    QCOMPARE(controller->rule(smartId).limit.value_or(0), 50);

    // 9. moveTracks reorders and emits playlistContentChanged(id)
    spyContentChanged.clear();
    QVERIFY(controller->moveTracks(queuePlaylistId, { 2 }, 0));
    QCOMPARE(spyContentChanged.count(), 1);
    QCOMPARE(spyContentChanged.takeFirst().at(0).toLongLong(), queuePlaylistId);

    // 10. smartOps, smartFieldType, smartFields, smartSortKeys
    const auto yearOps = controller->smartOps(library::SmartField::Year);
    QVERIFY(yearOps.contains(QVariant::fromValue(library::SmartOp::Between)));
    QVERIFY(!yearOps.contains(QVariant::fromValue(library::SmartOp::Contains)));
    QCOMPARE(
        controller->smartFieldKind(library::SmartField::Favorite), library::SmartFieldKind::Bool);
    QCOMPARE(controller->smartFieldKind(library::SmartField::Title), library::SmartFieldKind::Text);
    QCOMPARE(
        controller->smartFieldKind(library::SmartField::Year), library::SmartFieldKind::Number);
    QCOMPARE(
        controller->smartFieldKind(library::SmartField::DateAdded), library::SmartFieldKind::Date);
    QVERIFY(controller->smartFields().contains(QVariant::fromValue(library::SmartField::Title)));
    QVERIFY(
        controller->smartSortKeys().contains(QVariant::fromValue(library::TrackSortKey::Title)));
    QVERIFY(!controller->smartSortKeys().contains(
        QVariant::fromValue(library::TrackSortKey::PlaylistOrder)));
}

} // namespace

QTEST_GUILESS_MAIN(TstPlaylistController)
#include "tst_PlaylistController.moc"
