// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
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
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId,
        const QString &path = QStringLiteral("/music/song.mp3"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(1024 * 1024);
        q.addBindValue(2000);
        q.addBindValue(2000);
        q.addBindValue(2000);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId,
        const QVariant &cueIndex = QVariant(), const QVariant &albumId = QVariant(),
        qint64 tagsReadAt = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, ?, ?, ?, ?);"));
        q.addBindValue(fileId);
        q.addBindValue(cueIndex.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : cueIndex);
        q.addBindValue(albumId.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : albumId);
        q.addBindValue(tagsReadAt);
        q.addBindValue(3000);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static bool insertRawTag(const QSqlDatabase &db, qint64 trackId, const QString &tagType,
        int priority, const QString &key, int ordinal, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(tagType);
        q.addBindValue(priority);
        q.addBindValue(key);
        q.addBindValue(ordinal);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp)
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
    void singleAndMultiArtistAndComposerWithIdReuse();
    void withAlbumArtistSameAlbumAcrossDirs();
    void noAlbumArtistSameDirVsDifferentDir();
    void compilationVsSingleArtistWithoutAlbumArtist();
    void emptyAlbumResultsInNullAlbumId();
    void albumYearIsMinimumTrackYear();
    void userOverrideRelinksAndRemoveOrphans();
    void idempotentLinkTrackNoDuplicates();
};

void TstEntityLinker::singleAndMultiArtistAndComposerWithIdReuse()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("artists.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f1.mp3"));
    const qint64 trackId1 = TestDbHelper::insertTrack(conn, fileId1);

    // Track 1: Multi-artist "Artist A / Artist B", Composer "Comp 1 / Comp 2"
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Artist A / Artist B")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId1, QStringLiteral("id3v2"), 0,
        QStringLiteral("COMPOSER"), 0, QStringLiteral("Comp 1 / Comp 2")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId1, 1000));

    EntityLinker linker(conn);
    const auto linkRes1 = linker.linkTrack(trackId1);
    QVERIFY(linkRes1.ok());

    // Verify track 1 track_artists
    struct TrackArtistRecord {
        QString name;
        QString role;
        int position = 0;
    };
    QList<TrackArtistRecord> t1Artists;
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT a.name, ta.role, ta.position FROM track_artists ta "
                             "JOIN artists a ON ta.artist_id = a.id WHERE ta.track_id = ? "
                             "ORDER BY ta.role ASC, ta.position ASC"));
    q.addBindValue(trackId1);
    QVERIFY(q.exec());
    while (q.next()) {
        t1Artists.append(TrackArtistRecord {
            .name = q.value(0).toString(),
            .role = q.value(1).toString(),
            .position = q.value(2).toInt(),
        });
    }

    QCOMPARE(t1Artists.size(), 4);
    QCOMPARE(t1Artists.at(0).name, QStringLiteral("Artist A"));
    QCOMPARE(t1Artists.at(0).role, QStringLiteral("artist"));
    QCOMPARE(t1Artists.at(0).position, 0);

    QCOMPARE(t1Artists.at(1).name, QStringLiteral("Artist B"));
    QCOMPARE(t1Artists.at(1).role, QStringLiteral("artist"));
    QCOMPARE(t1Artists.at(1).position, 1);

    QCOMPARE(t1Artists.at(2).name, QStringLiteral("Comp 1"));
    QCOMPARE(t1Artists.at(2).role, QStringLiteral("composer"));
    QCOMPARE(t1Artists.at(2).position, 0);

    QCOMPARE(t1Artists.at(3).name, QStringLiteral("Comp 2"));
    QCOMPARE(t1Artists.at(3).role, QStringLiteral("composer"));
    QCOMPARE(t1Artists.at(3).position, 1);

    // Track 2: "Artist A / Artist C"
    const qint64 fileId2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f2.mp3"));
    const qint64 trackId2 = TestDbHelper::insertTrack(conn, fileId2);
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Artist A / Artist C")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId2, 1000));

    const auto linkRes2 = linker.linkTrack(trackId2);
    QVERIFY(linkRes2.ok());

    // Check that Artist A in track 2 reuses the exact same artist_id from track 1
    qint64 artistAId1 = 0;
    qint64 artistAId2 = 0;
    q.prepare(QStringLiteral("SELECT artist_id FROM track_artists WHERE track_id = ? AND position "
                             "= 0 AND role = 'artist'"));
    q.addBindValue(trackId1);
    QVERIFY(q.exec() && q.next());
    artistAId1 = q.value(0).toLongLong();

    q.bindValue(0, trackId2);
    QVERIFY(q.exec() && q.next());
    artistAId2 = q.value(0).toLongLong();

    QVERIFY(artistAId1 > 0);
    QCOMPARE(artistAId1, artistAId2);

    // Total distinct artists in artists table: Artist A, Artist B, Artist C, Comp 1, Comp 2 = 5
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 5);
}

void TstEntityLinker::withAlbumArtistSameAlbumAcrossDirs()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("album_artist.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir1/f1.mp3"));
    const qint64 fileId2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir2/f2.mp3"));
    const qint64 trackId1 = TestDbHelper::insertTrack(conn, fileId1);
    const qint64 trackId2 = TestDbHelper::insertTrack(conn, fileId2);

    // Both tracks have same album and album_artist across different directories
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Great Album")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Band A")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId1, 1000));

    QVERIFY(TestDbHelper::insertRawTag(conn, trackId2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Great Album")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Band A")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId2, 1000));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId1).ok());
    QVERIFY(linker.linkTrack(trackId2).ok());

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id IN (?, ?)"));
    q.addBindValue(trackId1);
    q.addBindValue(trackId2);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    const qint64 alb1 = q.value(0).toLongLong();
    QVERIFY(q.next());
    const qint64 alb2 = q.value(0).toLongLong();

    QVERIFY(alb1 > 0);
    QCOMPARE(alb1, alb2);

    // Check albums row grouping_key
    q.prepare(QStringLiteral("SELECT grouping_key, title, album_artist FROM albums WHERE id = ?"));
    q.addBindValue(alb1);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("aa:Band A\x1fGreat Album"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("Great Album"));
    QCOMPARE(q.value(2).toString(), QStringLiteral("Band A"));

    // Check album_artists
    q.prepare(QStringLiteral("SELECT a.name, aa.position FROM album_artists aa JOIN artists a ON "
                             "aa.artist_id = a.id WHERE aa.album_id = ?"));
    q.addBindValue(alb1);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Band A"));
    QCOMPARE(q.value(1).toInt(), 0);
    QVERIFY(!q.next());
}

void TstEntityLinker::noAlbumArtistSameDirVsDifferentDir()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("no_album_artist.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir1/f1.mp3"));
    const qint64 fileId2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir1/f2.mp3"));
    const qint64 fileId3
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/dir2/f3.mp3"));

    const qint64 trackId1 = TestDbHelper::insertTrack(conn, fileId1);
    const qint64 trackId2 = TestDbHelper::insertTrack(conn, fileId2);
    const qint64 trackId3 = TestDbHelper::insertTrack(conn, fileId3);

    // Track 1 & 2 in /music/dir1 with Album = "Self Titled"
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Self Titled")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId1, 1000));

    QVERIFY(TestDbHelper::insertRawTag(conn, trackId2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Self Titled")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId2, 1000));

    // Track 3 in /music/dir2 with Album = "Self Titled"
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId3, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Self Titled")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId3, 1000));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId1).ok());
    QVERIFY(linker.linkTrack(trackId2).ok());
    QVERIFY(linker.linkTrack(trackId3).ok());

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(trackId1);
    QVERIFY(q.exec() && q.next());
    const qint64 alb1 = q.value(0).toLongLong();

    q.bindValue(0, trackId2);
    QVERIFY(q.exec() && q.next());
    const qint64 alb2 = q.value(0).toLongLong();

    q.bindValue(0, trackId3);
    QVERIFY(q.exec() && q.next());
    const qint64 alb3 = q.value(0).toLongLong();

    QCOMPARE(alb1, alb2);
    QVERIFY(alb1 != alb3);

    // Total 2 albums
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
}

void TstEntityLinker::compilationVsSingleArtistWithoutAlbumArtist()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("comp.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // Compilation: same directory /music/comp, different artists per track, no album_artist
    const qint64 c1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/comp/c1.mp3"));
    const qint64 c2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/comp/c2.mp3"));
    const qint64 tc1 = TestDbHelper::insertTrack(conn, c1);
    const qint64 tc2 = TestDbHelper::insertTrack(conn, c2);

    QVERIFY(TestDbHelper::insertRawTag(conn, tc1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("V.A. Hits")));
    QVERIFY(TestDbHelper::insertRawTag(conn, tc1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Singer 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, tc1, 1000));

    QVERIFY(TestDbHelper::insertRawTag(conn, tc2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("V.A. Hits")));
    QVERIFY(TestDbHelper::insertRawTag(conn, tc2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Singer 2")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, tc2, 1000));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(tc1).ok());
    QVERIFY(linker.linkTrack(tc2).ok());

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(tc1);
    QVERIFY(q.exec() && q.next());
    const qint64 compAlbId = q.value(0).toLongLong();

    // Check album_artists for compAlbId is EMPTY
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM album_artists WHERE album_id = ?"));
    q.addBindValue(compAlbId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // Single Artist Album: same directory /music/solo, all tracks have same first artist "Solo
    // Singer", no album_artist
    const qint64 s1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/solo/s1.mp3"));
    const qint64 s2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/solo/s2.mp3"));
    const qint64 ts1 = TestDbHelper::insertTrack(conn, s1);
    const qint64 ts2 = TestDbHelper::insertTrack(conn, s2);

    QVERIFY(TestDbHelper::insertRawTag(conn, ts1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Solo Hits")));
    QVERIFY(TestDbHelper::insertRawTag(conn, ts1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Solo Singer / Guest A")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, ts1, 1000));

    QVERIFY(TestDbHelper::insertRawTag(conn, ts2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Solo Hits")));
    QVERIFY(TestDbHelper::insertRawTag(conn, ts2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Solo Singer / Guest B")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, ts2, 1000));

    QVERIFY(linker.linkTrack(ts1).ok());
    QVERIFY(linker.linkTrack(ts2).ok());

    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(ts1);
    QVERIFY(q.exec() && q.next());
    const qint64 soloAlbId = q.value(0).toLongLong();

    // Check album_artists for soloAlbId has 1 row: "Solo Singer"
    q.prepare(QStringLiteral("SELECT a.name FROM album_artists aa JOIN artists a ON aa.artist_id = "
                             "a.id WHERE aa.album_id = ?"));
    q.addBindValue(soloAlbId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Solo Singer"));
    QVERIFY(!q.next());
}

void TstEntityLinker::emptyAlbumResultsInNullAlbumId()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("empty_alb.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/track.mp3"));
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    // Only artist, no album tag
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Solo Artist")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 1000));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());
    QVERIFY(q.value(0).isNull());

    // 0 albums created
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstEntityLinker::albumYearIsMinimumTrackYear()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("year.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/y1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/y2.mp3"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/y3.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);

    // Track 1: 2020
    QVERIFY(TestDbHelper::insertRawTag(conn, t1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Chronology")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Artist Y")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t1, QStringLiteral("id3v2"), 0, QStringLiteral("DATE"),
        0, QStringLiteral("2020-05-12")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1, 1000));

    // Track 2: 2015
    QVERIFY(TestDbHelper::insertRawTag(conn, t2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Chronology")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Artist Y")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t2, QStringLiteral("id3v2"), 0, QStringLiteral("DATE"),
        0, QStringLiteral("2015-08-20")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2, 1000));

    // Track 3: No date
    QVERIFY(TestDbHelper::insertRawTag(conn, t3, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Chronology")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t3, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Artist Y")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t3, 1000));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());
    QVERIFY(linker.linkTrack(t3).ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT year FROM albums WHERE title = 'Chronology'")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2015);

    // Second album: All tracks have NULL year
    const qint64 f4 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/y4.mp3"));
    const qint64 t4 = TestDbHelper::insertTrack(conn, f4);
    QVERIFY(TestDbHelper::insertRawTag(conn, t4, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("No Year Album")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t4, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Artist Z")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t4, 1000));

    QVERIFY(linker.linkTrack(t4).ok());

    QVERIFY(q.exec(QStringLiteral("SELECT year FROM albums WHERE title = 'No Year Album'")));
    QVERIFY(q.next());
    QVERIFY(q.value(0).isNull());
}

void TstEntityLinker::userOverrideRelinksAndRemoveOrphans()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("override.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/f2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    // Track 1 on Old Album
    QVERIFY(TestDbHelper::insertRawTag(conn, t1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Old Album")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Old AA")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t1, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Old Artist")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1, 1000));

    // Track 2 on Stay Album
    QVERIFY(TestDbHelper::insertRawTag(conn, t2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Stay Album")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Stay AA")));
    QVERIFY(TestDbHelper::insertRawTag(conn, t2, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Stay Artist")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2, 1000));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);

    // Apply user override on track 1: change album and artist
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'album', 'New Album', 2000, 2000);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'album_artist', 'New AA', 2000, 2000);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'artist', 'New Artist', 2000, 2000);")));

    // Relink track 1
    QVERIFY(linker.linkTrack(t1).ok());

    // Call removeOrphans
    const auto removeRes = linker.removeOrphans();
    QVERIFY(removeRes.ok());
    QCOMPARE(removeRes.value().first, 1); // 1 orphan album ("Old Album")
    QCOMPARE(removeRes.value().second, 2); // 2 orphan artists ("Old AA", "Old Artist")

    // Verify albums table has 2 albums: "New Album" and "Stay Album"
    QVERIFY(q.exec(QStringLiteral("SELECT title FROM albums ORDER BY title ASC")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("New Album"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Stay Album"));
    QVERIFY(!q.next());

    // Verify artists table has 4 artists: "New AA", "New Artist", "Stay AA", "Stay Artist"
    QVERIFY(q.exec(QStringLiteral("SELECT name FROM artists ORDER BY name ASC")));
    QStringList artists;
    while (q.next()) {
        artists.append(q.value(0).toString());
    }
    QCOMPARE(artists,
        (QStringList { QStringLiteral("New AA"), QStringLiteral("New Artist"),
            QStringLiteral("Stay AA"), QStringLiteral("Stay Artist") }));
}

void TstEntityLinker::idempotentLinkTrackNoDuplicates()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("idempotent.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song.mp3"));
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUM"), 0, QStringLiteral("Idem Alb")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ALBUMARTIST"), 0, QStringLiteral("Idem AA")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Art 1 / Art 2")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("COMPOSER"), 0, QStringLiteral("Comp 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 1000));

    EntityLinker linker(conn);
    // Link multiple times
    QVERIFY(linker.linkTrack(trackId).ok());
    QVERIFY(linker.linkTrack(trackId).ok());
    QVERIFY(linker.linkTrack(trackId).ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = 1")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 3); // Art 1, Art 2, Comp 1

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM album_artists")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1); // Idem AA

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstEntityLinker)

#include "tst_EntityLinker.moc"
