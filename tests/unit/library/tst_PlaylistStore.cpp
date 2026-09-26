// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/PlaylistStore.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::PlaylistStore;

class TstPlaylistStore : public QObject {
    Q_OBJECT

private slots:
    void createManualPlaylist();
    void rejectEmptyName();
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

    const auto res = store.createManual(QStringLiteral("  我的歌单 "), { 20, 10 });
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

    const auto res2 = store.createManual(QStringLiteral("   \t\n "), { 1 });
    QVERIFY(!res2.ok());
}

} // namespace

QTEST_MAIN(TstPlaylistStore)
#include "tst_PlaylistStore.moc"
