// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/TrackMerge.h>

namespace {

using linernotes::library::Database;
using linernotes::library::mergeTrackInto;
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

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 fileId, const QVariant &cueIndex = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, ?, NULL, 1000, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(cueIndex.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : cueIndex);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertPlayEvent(const QSqlDatabase &db, qint64 trackId, qint64 startedAt,
        qint64 playedMs = 50000, int completed = 1, int skipped = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO play_events (track_id, started_at, played_ms, "
                                 "track_duration_ms, completed, skipped) "
                                 "VALUES (?, ?, ?, 60000, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(startedAt);
        q.addBindValue(playedMs);
        q.addBindValue(completed);
        q.addBindValue(skipped);
        return q.exec();
    }

    static bool insertPlayStats(const QSqlDatabase &db, qint64 trackId, int playCount,
        const QVariant &lastPlayedAt, qint64 totalPlayedMs, const QVariant &avgCompletion,
        int skipCount)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO track_play_stats (track_id, play_count, last_played_at, total_played_ms, "
            "avg_completion, skip_count) VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(playCount);
        q.addBindValue(lastPlayedAt);
        q.addBindValue(totalPlayedMs);
        q.addBindValue(avgCompletion);
        q.addBindValue(skipCount);
        return q.exec();
    }

    static bool insertFavorite(const QSqlDatabase &db, qint64 trackId, qint64 createdAt)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('track', ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(createdAt);
        return q.exec();
    }

    static bool insertRating(const QSqlDatabase &db, qint64 trackId, int rating, qint64 updatedAt)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO ratings (track_id, rating, updated_at) VALUES (?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(rating);
        q.addBindValue(updatedAt);
        return q.exec();
    }

    static qint64 insertPlaylist(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO playlists (name, kind, created_at, updated_at) "
                                 "VALUES (?, 'manual', 1000, 1000);"));
        q.addBindValue(name);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertPlaylistItem(
        const QSqlDatabase &db, qint64 playlistId, qint64 trackId, int position)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO playlist_items (playlist_id, track_id, position, added_at) "
                           "VALUES (?, ?, ?, 1000);"));
        q.addBindValue(playlistId);
        q.addBindValue(trackId);
        q.addBindValue(position);
        return q.exec();
    }
};

class TstTrackMerge : public QObject {
    Q_OBJECT

private slots:
    void mergesUserDataAndDeletesFromTrackAndFile();
    void sameTrackIdOrMissingTrackReturnsErrorWithoutModifications();
    void retainsFileIfReferencedByAnotherTrack();
};

void TstTrackMerge::mergesUserDataAndDeletesFromTrackAndFile()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_merge.db")));
    QVERIFY(db.open(Migrator()).ok());
    auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileFrom
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/from.flac"));
    const qint64 fileTo = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/to.flac"));
    const qint64 tFrom = TestDbHelper::insertTrack(conn, fileFrom);
    const qint64 tTo = TestDbHelper::insertTrack(conn, fileTo);

    QVERIFY(tFrom > 0);
    QVERIFY(tTo > 0);

    // from: 3 play_events, play_stats, favorite, rating, in a playlist
    QVERIFY(TestDbHelper::insertPlayEvent(conn, tFrom, 100, 50000, 1, 0));
    QVERIFY(TestDbHelper::insertPlayEvent(conn, tFrom, 200, 50000, 1, 0));
    QVERIFY(TestDbHelper::insertPlayEvent(conn, tFrom, 300, 50000, 1, 1));
    QVERIFY(TestDbHelper::insertPlayStats(conn, tFrom, 3, 300, 150000, 0.8, 1));
    QVERIFY(TestDbHelper::insertFavorite(conn, tFrom, 1000));
    QVERIFY(TestDbHelper::insertRating(conn, tFrom, 4, 1100));

    const qint64 plId = TestDbHelper::insertPlaylist(conn, QStringLiteral("Favorites"));
    QVERIFY(plId > 0);
    QVERIFY(TestDbHelper::insertPlaylistItem(conn, plId, tFrom, 0));

    // to: 1 play_event, play_stats, no favorite, no rating
    QVERIFY(TestDbHelper::insertPlayEvent(conn, tTo, 250, 60000, 1, 0));
    QVERIFY(TestDbHelper::insertPlayStats(conn, tTo, 1, 250, 60000, 0.6, 0));

    // Perform merge
    const auto mergeRes = mergeTrackInto(conn, tFrom, tTo);
    QVERIFY(mergeRes.ok());

    // 1. play_events: to has 4 events, from has 0
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM play_events WHERE track_id = ?;"));
    q.addBindValue(tTo);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 4);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM play_events WHERE track_id = ?;"));
    q.addBindValue(tFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // 2. track_play_stats: play_count=4, last_played_at=300, total_played_ms=210000,
    // avg_completion=0.75, skip_count=1
    q.prepare(QStringLiteral(
        "SELECT play_count, last_played_at, total_played_ms, avg_completion, skip_count "
        "FROM track_play_stats WHERE track_id = ?;"));
    q.addBindValue(tTo);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 4);
    QCOMPARE(q.value(1).toLongLong(), 300);
    QCOMPARE(q.value(2).toLongLong(), 210000);
    QCOMPARE(q.value(3).toDouble(), 0.75);
    QCOMPARE(q.value(4).toInt(), 1);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM track_play_stats WHERE track_id = ?;"));
    q.addBindValue(tFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // 3. favorites: to is favorite (created_at=1000), from is not
    q.prepare(QStringLiteral(
        "SELECT created_at FROM favorites WHERE entity_type = 'track' AND entity_id = ?;"));
    q.addBindValue(tTo);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toLongLong(), 1000);

    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM favorites WHERE entity_type = 'track' AND entity_id = ?;"));
    q.addBindValue(tFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // 4. ratings: to has rating 4
    q.prepare(QStringLiteral("SELECT rating FROM ratings WHERE track_id = ?;"));
    q.addBindValue(tTo);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 4);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM ratings WHERE track_id = ?;"));
    q.addBindValue(tFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // 5. playlist_items: points to toTrackId
    q.prepare(QStringLiteral("SELECT track_id FROM playlist_items WHERE playlist_id = ?;"));
    q.addBindValue(plId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toLongLong(), tTo);

    // 6. tracks & files: from track and from file deleted; to track and to file exist
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(tFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE id = ?;"));
    q.addBindValue(fileFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(tTo);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE id = ?;"));
    q.addBindValue(fileTo);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstTrackMerge::sameTrackIdOrMissingTrackReturnsErrorWithoutModifications()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_merge_errors.db")));
    QVERIFY(db.open(Migrator()).ok());
    auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 file1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song1.flac"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, file1);

    // 1. Same track ID -> error
    const auto sameRes = mergeTrackInto(conn, t1, t1);
    QVERIFY(!sameRes.ok());

    // 2. fromTrack does not exist -> error
    const auto missingFromRes = mergeTrackInto(conn, 9999, t1);
    QVERIFY(!missingFromRes.ok());

    // 3. toTrack does not exist -> error
    const auto missingToRes = mergeTrackInto(conn, t1, 9999);
    QVERIFY(!missingToRes.ok());

    // Verify t1 still exists unmodified
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(t1);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstTrackMerge::retainsFileIfReferencedByAnotherTrack()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_merge_cue.db")));
    QVERIFY(db.open(Migrator()).ok());
    auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 cueFile
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/album.flac"));
    const qint64 otherFile
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/single.flac"));

    // Two tracks referencing cueFile (cueIndex 1 and 2)
    const qint64 tFrom = TestDbHelper::insertTrack(conn, cueFile, 1);
    const qint64 tKeepCue = TestDbHelper::insertTrack(conn, cueFile, 2);
    const qint64 tTo = TestDbHelper::insertTrack(conn, otherFile);

    const auto res = mergeTrackInto(conn, tFrom, tTo);
    QVERIFY(res.ok());

    // tFrom is deleted
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(tFrom);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // tKeepCue is untouched, so cueFile still exists
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?;"));
    q.addBindValue(tKeepCue);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE id = ?;"));
    q.addBindValue(cueFile);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstTrackMerge)

#include "tst_TrackMerge.moc"
