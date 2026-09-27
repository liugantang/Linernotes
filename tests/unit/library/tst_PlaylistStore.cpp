// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryQuery.h>
#include <library/Migrator.h>
#include <library/PlaylistStore.h>
#include <library/SmartRule.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::PlaylistInfo;
using linernotes::library::PlaylistKind;
using linernotes::library::PlaylistStore;
using linernotes::library::SmartCondition;
using linernotes::library::SmartField;
using linernotes::library::SmartMatch;
using linernotes::library::SmartOp;
using linernotes::library::SmartRule;
using linernotes::library::TrackSortKey;
namespace errc = linernotes::library::errc;

class TstPlaylistStore : public QObject {
    Q_OBJECT

private slots:
    void createManualPlaylist();
    void rejectEmptyName();
    void smartPlaylistLifecycle();
    void manualPlaylistTrackOperations();
    void typeRestrictions();
};

void TstPlaylistStore::createManualPlaylist()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto conn = db.connection().value();
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                  "VALUES (1, '/music', 1, 100);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                  "mtime, first_seen_at, scanned_at) VALUES "
                                  "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1), "
                                  "(2, 1, '/music/2.mp3', 120000, 1, 1, 2, 1);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                  "VALUES (10, 1, 1, 1), (20, 2, 1, 1);")));

    PlaylistStore store(db);

    // Test with duplicate track IDs (20, 10, 20) -> should dedup to 20, 10
    const auto res = store.createManual(QStringLiteral("  我的歌单 "), { 20, 10, 20 });
    QVERIFY(res.ok());
    const qint64 playlistId = res.value();
    QVERIFY(playlistId > 0);

    // Verify playlists row
    QVERIFY(
        q.prepare(QStringLiteral("SELECT name, kind, rule, position FROM playlists WHERE id = ?")));
    q.addBindValue(playlistId);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("我的歌单"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("manual"));
    QVERIFY(q.isNull(2));
    QCOMPARE(q.value(3).toLongLong(), 0LL);

    // Verify playlist_items
    QVERIFY(q.prepare(QStringLiteral("SELECT track_id, position FROM playlist_items WHERE "
                                     "playlist_id = ? ORDER BY position ASC")));
    q.addBindValue(playlistId);
    QVERIFY(q.exec());

    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), 20LL);
    QCOMPARE(q.value(1).toInt(), 0);

    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), 10LL);
    QCOMPARE(q.value(1).toInt(), 1);

    QVERIFY(!q.next());
}

void TstPlaylistStore::rejectEmptyName()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    PlaylistStore store(db);

    const auto res1 = store.createManual(QStringLiteral(""), { 1 });
    QVERIFY(!res1.ok());
    QCOMPARE(res1.error().code, QString(errc::kPlaylistInvalid));

    const auto res2 = store.createManual(QStringLiteral("   \t\n "), { 1 });
    QVERIFY(!res2.ok());
    QCOMPARE(res2.error().code, QString(errc::kPlaylistInvalid));
}

void TstPlaylistStore::smartPlaylistLifecycle()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    PlaylistStore store(db);

    SmartRule rule;
    rule.match = SmartMatch::All;
    rule.conditions = {
        SmartCondition {
            .field = SmartField::Year,
            .op = SmartOp::Greater,
            .value = 2010,
            .value2 = { },
        },
    };
    rule.sortKey = TrackSortKey::Title;
    rule.sortOrder = Qt::AscendingOrder;
    rule.limit = 20;

    const auto createRes = store.createSmart(QStringLiteral("智能歌单1"), rule);
    QVERIFY(createRes.ok());
    const qint64 smartId = createRes.value();

    // Verify list()
    const auto listRes = store.list();
    QVERIFY(listRes.ok());
    QCOMPARE(listRes.value().size(), 1);
    const auto &info = listRes.value().first();
    QCOMPARE(info.id, smartId);
    QCOMPARE(info.name, QStringLiteral("智能歌单1"));
    QCOMPARE(info.kind, PlaylistKind::Smart);
    QVERIFY(info.rule.has_value());
    QCOMPARE(info.rule.value_or(SmartRule { }), rule);

    // Verify playlist()
    const auto getRes = store.playlist(smartId);
    QVERIFY(getRes.ok());
    const auto &pl = getRes.value();
    QVERIFY(pl.has_value());
    QCOMPARE(pl.value_or(PlaylistInfo { }).rule, std::make_optional(rule));

    // Verify rename()
    QVERIFY(store.rename(smartId, QStringLiteral("重命名智能歌单")).ok());
    const auto renamedPl = store.playlist(smartId);
    QVERIFY(renamedPl.ok());
    QCOMPARE(renamedPl.value().value_or(PlaylistInfo { }).name, QStringLiteral("重命名智能歌单"));

    // Verify setRule()
    SmartRule updatedRule = rule;
    updatedRule.limit = 100;
    QVERIFY(store.setRule(smartId, updatedRule).ok());
    const auto updatedPl = store.playlist(smartId);
    QVERIFY(updatedPl.ok());
    QCOMPARE(updatedPl.value().value_or(PlaylistInfo { }).rule, std::make_optional(updatedRule));

    // Verify remove()
    QVERIFY(store.remove(smartId).ok());
    const auto remPl = store.playlist(smartId);
    QVERIFY(remPl.ok());
    QVERIFY(!remPl.value().has_value());
}

void TstPlaylistStore::manualPlaylistTrackOperations()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const auto conn = db.connection().value();
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                  "VALUES (1, '/music', 1, 100);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO files (id, root_id, path, duration_ms, size, mtime, first_seen_at, "
        "scanned_at) VALUES "
        "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1), (2, 1, '/music/2.mp3', 60000, 1, 1, 1, 1), "
        "(3, 1, '/music/3.mp3', 60000, 1, 1, 1, 1), (4, 1, '/music/4.mp3', 60000, 1, 1, 1, 1), "
        "(5, 1, '/music/5.mp3', 60000, 1, 1, 1, 1);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                  "VALUES (10, 1, 1, 1), (20, 2, 1, 1), (30, 3, 1, 1), (40, 4, 1, "
                                  "1), (50, 5, 1, 1);")));

    PlaylistStore store(db);

    const auto createRes = store.createManual(QStringLiteral("手动歌单"), { 10, 20 });
    QVERIFY(createRes.ok());
    const qint64 plId = createRes.value();

    // 1. addTracks (append): 20 already exists, adds 30, 40 -> returns 2
    const auto addRes1 = store.addTracks(plId, { 20, 30, 40 });
    QVERIFY(addRes1.ok());
    QCOMPARE(addRes1.value(), 2);

    // 2. addTracks (insert at beforePosition = 1): inserts 50 before 20 -> items: 10, 50, 20, 30,
    // 40
    const auto addRes2 = store.addTracks(plId, { 50 }, 1);
    QVERIFY(addRes2.ok());
    QCOMPARE(addRes2.value(), 1);

    auto verifyItems = [&](const QList<qint64> &expectedIds) {
        QSqlQuery query(conn);
        query.prepare(QStringLiteral("SELECT track_id, position FROM playlist_items WHERE "
                                     "playlist_id = ? ORDER BY position ASC"));
        query.addBindValue(plId);
        QVERIFY(query.exec());
        QList<qint64> actualIds;
        int pos = 0;
        while (query.next()) {
            actualIds.append(query.value(0).toLongLong());
            QCOMPARE(query.value(1).toInt(), pos);
            ++pos;
        }
        QCOMPARE(actualIds, expectedIds);
    };

    verifyItems({ 10, 50, 20, 30, 40 });

    // 3. moveTracks forward: move {30, 40} to beforePosition 0 -> items: 30, 40, 10, 50, 20
    QVERIFY(store.moveTracks(plId, { 30, 40 }, 0).ok());
    verifyItems({ 30, 40, 10, 50, 20 });

    // 4. moveTracks backward: move {30} to beforePosition 5 (end) -> items: 40, 10, 50, 20, 30
    QVERIFY(store.moveTracks(plId, { 30 }, 5).ok());
    verifyItems({ 40, 10, 50, 20, 30 });

    // 5. removeTracks: remove 50 and non-existent 999 -> items: 40, 10, 20, 30
    QVERIFY(store.removeTracks(plId, { 50, 999 }).ok());
    verifyItems({ 40, 10, 20, 30 });

    // 6. remove playlist -> items cascade deleted
    QVERIFY(store.remove(plId).ok());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = ?"));
    q.addBindValue(plId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstPlaylistStore::typeRestrictions()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    PlaylistStore store(db);

    const auto smartIdRes = store.createSmart(QStringLiteral("Smart"), SmartRule { });
    QVERIFY(smartIdRes.ok());
    const qint64 smartId = smartIdRes.value();

    const auto manualIdRes = store.createManual(QStringLiteral("Manual"), { });
    QVERIFY(manualIdRes.ok());
    const qint64 manualId = manualIdRes.value();

    // Smart playlist rejects track operations
    const auto addRes = store.addTracks(smartId, { 1 });
    QVERIFY(!addRes.ok());
    QCOMPARE(addRes.error().code, QString(errc::kPlaylistInvalid));

    const auto remRes = store.removeTracks(smartId, { 1 });
    QVERIFY(!remRes.ok());
    QCOMPARE(remRes.error().code, QString(errc::kPlaylistInvalid));

    const auto moveRes = store.moveTracks(smartId, { 1 }, 0);
    QVERIFY(!moveRes.ok());
    QCOMPARE(moveRes.error().code, QString(errc::kPlaylistInvalid));

    // Manual playlist rejects setRule
    const auto ruleRes = store.setRule(manualId, SmartRule { });
    QVERIFY(!ruleRes.ok());
    QCOMPARE(ruleRes.error().code, QString(errc::kPlaylistInvalid));
}

} // namespace

QTEST_MAIN(TstPlaylistStore)
#include "tst_PlaylistStore.moc"
