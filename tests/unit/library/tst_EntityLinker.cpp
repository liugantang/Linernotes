// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId,
        const QString &path = QStringLiteral("/music/song.mp3"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertRawTag(
        const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(key);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }
};

class TstEntityLinker : public QObject {
    Q_OBJECT

private slots:
    void artistsAndComposersWithIdReuseAndIdempotency();
    void albumGroupingAndYearAggregation();
    void userOverridesAndOrphanCleanup();
};

void TstEntityLinker::artistsAndComposersWithIdReuseAndIdempotency()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("artists.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    // Track 1: Artist A / Artist B, Composer Comp 1
    QVERIFY(TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist A / Artist B")));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, t1, QStringLiteral("COMPOSER"), QStringLiteral("Comp 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1));

    // Track 2: Artist A / Artist C
    QVERIFY(TestDbHelper::insertRawTag(
        conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Artist A / Artist C")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t1).ok()); // Idempotency check
    QVERIFY(linker.linkTrack(t2).ok());

    QSqlQuery q(conn);
    // Verify track 1 has 2 artists and 1 composer
    q.prepare(QStringLiteral("SELECT a.name, ta.role, ta.position FROM track_artists ta "
                             "JOIN artists a ON ta.artist_id = a.id WHERE ta.track_id = ? "
                             "ORDER BY ta.role ASC, ta.position ASC"));
    q.addBindValue(t1);
    QVERIFY(q.exec());
    QVERIFY(q.next() && q.value(0).toString() == QStringLiteral("Artist A")
        && q.value(1).toString() == QStringLiteral("artist") && q.value(2).toInt() == 0);
    QVERIFY(q.next() && q.value(0).toString() == QStringLiteral("Artist B")
        && q.value(1).toString() == QStringLiteral("artist") && q.value(2).toInt() == 1);
    QVERIFY(q.next() && q.value(0).toString() == QStringLiteral("Comp 1")
        && q.value(1).toString() == QStringLiteral("composer") && q.value(2).toInt() == 0);
    QVERIFY(!q.next());

    // Verify Artist A reuse across tracks
    q.prepare(QStringLiteral("SELECT artist_id FROM track_artists WHERE track_id = ? AND position "
                             "= 0 AND role = 'artist'"));
    q.addBindValue(t1);
    QVERIFY(q.exec() && q.next());
    const qint64 a1 = q.value(0).toLongLong();
    q.bindValue(0, t2);
    QVERIFY(q.exec() && q.next());
    const qint64 a2 = q.value(0).toLongLong();
    QVERIFY(a1 > 0 && a1 == a2);

    // Total distinct artists = 4 (Artist A, Artist B, Artist C, Comp 1)
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists")));
    QVERIFY(q.next() && q.value(0).toInt() == 4);
}

void TstEntityLinker::albumGroupingAndYearAggregation()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("albums.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    EntityLinker linker(conn);
    QSqlQuery q(conn);

    // 1. With ALBUMARTIST across different directories -> same album & min year
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir1/f1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir2/f2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"), QStringLiteral("Album AA"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUMARTIST"), QStringLiteral("Band AA"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("DATE"), QStringLiteral("2020-05-12"));
    TestDbHelper::updateTagsReadAt(conn, t1);
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"), QStringLiteral("Album AA"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUMARTIST"), QStringLiteral("Band AA"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("DATE"), QStringLiteral("2015-08-20"));
    TestDbHelper::updateTagsReadAt(conn, t2);
    QVERIFY(linker.linkTrack(t1).ok() && linker.linkTrack(t2).ok());

    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id IN (?, ?)"));
    q.addBindValue(t1);
    q.addBindValue(t2);
    QVERIFY(q.exec() && q.next());
    const qint64 alb1 = q.value(0).toLongLong();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), alb1);

    q.prepare(QStringLiteral("SELECT grouping_key, year FROM albums WHERE id = ?"));
    q.addBindValue(alb1);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(),
        QStringLiteral("aa:Band AA\x1f"
                       "Album AA"));
    QCOMPARE(q.value(1).toInt(), 2015);

    // 2. Without ALBUMARTIST: different directories produce different albums
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir3/f3.mp3"));
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ALBUM"), QStringLiteral("Album AA"));
    TestDbHelper::updateTagsReadAt(conn, t3);
    QVERIFY(linker.linkTrack(t3).ok());
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(t3);
    QVERIFY(q.exec() && q.next());
    QVERIFY(q.value(0).toLongLong() != alb1);

    // 3. Compilation in same directory -> no album_artists
    const qint64 fc1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/comp/c1.mp3"));
    const qint64 fc2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/comp/c2.mp3"));
    const qint64 tc1 = TestDbHelper::insertTrack(conn, fc1);
    const qint64 tc2 = TestDbHelper::insertTrack(conn, fc2);
    TestDbHelper::insertRawTag(conn, tc1, QStringLiteral("ALBUM"), QStringLiteral("VA Hits"));
    TestDbHelper::insertRawTag(conn, tc1, QStringLiteral("ARTIST"), QStringLiteral("Singer 1"));
    TestDbHelper::updateTagsReadAt(conn, tc1);
    TestDbHelper::insertRawTag(conn, tc2, QStringLiteral("ALBUM"), QStringLiteral("VA Hits"));
    TestDbHelper::insertRawTag(conn, tc2, QStringLiteral("ARTIST"), QStringLiteral("Singer 2"));
    TestDbHelper::updateTagsReadAt(conn, tc2);
    QVERIFY(linker.linkTrack(tc1).ok() && linker.linkTrack(tc2).ok());
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(tc1);
    QVERIFY(q.exec() && q.next());
    const qint64 compAlbId = q.value(0).toLongLong();
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM album_artists WHERE album_id = ?"));
    q.addBindValue(compAlbId);
    QVERIFY(q.exec() && q.next() && q.value(0).toInt() == 0);

    // 4. Empty album tag -> null album_id
    const qint64 fe = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/empty.mp3"));
    const qint64 te = TestDbHelper::insertTrack(conn, fe);
    TestDbHelper::insertRawTag(conn, te, QStringLiteral("ARTIST"), QStringLiteral("Solo Artist"));
    TestDbHelper::updateTagsReadAt(conn, te);
    QVERIFY(linker.linkTrack(te).ok());
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(te);
    QVERIFY(q.exec() && q.next() && q.value(0).isNull());
}

void TstEntityLinker::userOverridesAndOrphanCleanup()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("override.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"), QStringLiteral("Old Album"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUMARTIST"), QStringLiteral("Old AA"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Old Artist"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"), QStringLiteral("Stay Album"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUMARTIST"), QStringLiteral("Stay AA"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Stay Artist"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    QSqlQuery q(conn);
    // Apply user override on track 1
    q.prepare(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (?, 'album', 'New Album', 2000, 2000), (?, 'album_artist', 'New AA', 2000, 2000), "
        "(?, 'artist', 'New Artist', 2000, 2000);"));
    q.addBindValue(t1);
    q.addBindValue(t1);
    q.addBindValue(t1);
    QVERIFY(q.exec());

    QVERIFY(linker.linkTrack(t1).ok());

    const auto removeRes = linker.removeOrphans();
    QVERIFY(removeRes.ok());
    QCOMPARE(removeRes.value().first, 1); // 1 orphan album ("Old Album")
    QCOMPARE(removeRes.value().second, 2); // 2 orphan artists ("Old AA", "Old Artist")

    QVERIFY(q.exec(QStringLiteral("SELECT title FROM albums ORDER BY title ASC")));
    QVERIFY(q.next() && q.value(0).toString() == QStringLiteral("New Album"));
    QVERIFY(q.next() && q.value(0).toString() == QStringLiteral("Stay Album"));
    QVERIFY(!q.next());

    QVERIFY(q.exec(QStringLiteral("SELECT name FROM artists ORDER BY name ASC")));
    QStringList artists;
    while (q.next()) {
        artists.append(q.value(0).toString());
    }
    QCOMPARE(artists,
        (QStringList { QStringLiteral("New AA"), QStringLiteral("New Artist"),
            QStringLiteral("Stay AA"), QStringLiteral("Stay Artist") }));
}

} // namespace

QTEST_GUILESS_MAIN(TstEntityLinker)

#include "tst_EntityLinker.moc"
