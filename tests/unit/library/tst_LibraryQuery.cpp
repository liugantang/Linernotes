// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <library/Migrator.h>

using namespace linernotes::library;
using namespace Qt::StringLiterals;

namespace {

// 造数据用：失败时直接让测试失败，避免静默插入失败导致的误判
void exec(const QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        QFAIL(qPrintable(sql + u" -> "_s + q.lastError().text()));
    }
}

void execChecked(QSqlQuery &q)
{
    if (!q.exec()) {
        QFAIL(qPrintable(q.lastQuery() + u" -> "_s + q.lastError().text()));
    }
}

struct DbHelper {
    static qint64 insertRoot(const QSqlDatabase &db)
    {
        QSqlQuery q(db);
        if (!q.exec(u"INSERT INTO library_roots (path, added_at) VALUES ('/m', 100);"_s)) {
            return 0;
        }
        return q.lastInsertId().toLongLong();
    }
    static qint64 insertFile(const QSqlDatabase &db, qint64 rId, const QString &p, qint64 ms = 100)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) VALUES (?, ?, 1, 1, ?, 1, 1)"));
        q.addBindValue(rId);
        q.addBindValue(p);
        q.addBindValue(ms);
        execChecked(q);
        return q.lastInsertId().toLongLong();
    }
    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 fId, const QVariant &albId = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, album_id, tags_read_at, created_at) "
                                 "VALUES (?, ?, 1, 1)"));
        q.addBindValue(fId);
        q.addBindValue(albId);
        execChecked(q);
        return q.lastInsertId().toLongLong();
    }
    static qint64 insertAlbum(
        const QSqlDatabase &db, const QString &t, const QVariant &aa = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO albums (grouping_key, title, album_artist, "
                                 "created_at) VALUES (?, ?, ?, 1)"));
        q.addBindValue(QString(t + aa.toString()));
        q.addBindValue(t);
        q.addBindValue(aa);
        execChecked(q);
        return q.lastInsertId().toLongLong();
    }
    static qint64 insertArtist(const QSqlDatabase &db, const QString &n)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, 1)"));
        q.addBindValue(n);
        execChecked(q);
        return q.lastInsertId().toLongLong();
    }
    static void setMeta(const QSqlDatabase &db, qint64 tId, const QVariant &t, const QVariant &a,
        const QVariant &al, const QVariant &aa)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE effective_metadata SET title=?, artist=?, album=?, "
                                 "album_artist=? WHERE track_id=?"));
        q.addBindValue(t);
        q.addBindValue(a);
        q.addBindValue(al);
        q.addBindValue(aa);
        q.addBindValue(tId);
        execChecked(q);
    }
};

class TstLibraryQuery : public QObject {
    Q_OBJECT
private slots:
    void tests()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db(dir.filePath(u"test.db"_s));
        QVERIFY(db.open(Migrator()).ok());
        auto qDb = db.connection().value();
        qint64 r = DbHelper::insertRoot(qDb);
        qint64 alb1 = DbHelper::insertAlbum(qDb, u"Al1"_s);
        qint64 art1 = DbHelper::insertArtist(qDb, u"Ar1"_s);

        qint64 f1 = DbHelper::insertFile(qDb, r, u"1.mp3"_s, 100);
        qint64 t1 = DbHelper::insertTrack(qDb, f1, alb1);
        DbHelper::setMeta(qDb, t1, u"A"_s, u"Ar1"_s, u"Al1"_s, u"AA"_s);
        exec(qDb,
            QStringLiteral(
                "INSERT INTO track_artists (track_id, artist_id, role) VALUES (%1, %2, 'artist')")
                .arg(t1)
                .arg(art1));
        exec(qDb,
            QStringLiteral("INSERT INTO favorites (entity_type, entity_id, created_at) VALUES "
                           "('track', %1, 1)")
                .arg(t1));

        qint64 f2 = DbHelper::insertFile(qDb, r, u"2.mp3"_s, 200);
        qint64 t2 = DbHelper::insertTrack(qDb, f2, alb1);
        DbHelper::setMeta(qDb, t2, u"B"_s, u"Ar2"_s, u"Al1"_s, u"AA"_s);

        qint64 f3 = DbHelper::insertFile(qDb, r, u"3.mp3"_s, 300);
        qint64 t3 = DbHelper::insertTrack(qDb, f3);
        DbHelper::setMeta(qDb, t3, QVariant(), QVariant(), QVariant(), QVariant());

        LibraryQuery q(qDb);

        // 1. Sorting: Default and Title (ASC/DESC), NULLs last, id break tie
        auto tAsc = q.tracks({ }, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(tAsc.size(), 3);
        QCOMPARE(tAsc.at(0).trackId, t1);
        QCOMPARE(tAsc.at(1).trackId, t2);
        QCOMPARE(tAsc.at(2).trackId, t3);
        auto tDesc = q.tracks({ }, TrackSortKey::Title, Qt::DescendingOrder, 0, 10).value();
        QCOMPARE(tDesc.at(0).trackId, t2);
        QCOMPARE(tDesc.at(1).trackId, t1);
        QCOMPARE(tDesc.at(2).trackId, t3);
        auto dAsc = q.tracks({ }, TrackSortKey::Default, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(dAsc.at(0).trackId, t1);
        QCOMPARE(dAsc.at(1).trackId, t2);
        QCOMPARE(dAsc.at(2).trackId, t3);

        // 2. Pagination: limit 2, concat
        auto p1 = q.tracks({ }, TrackSortKey::Title, Qt::AscendingOrder, 0, 2).value();
        auto p2 = q.tracks({ }, TrackSortKey::Title, Qt::AscendingOrder, 2, 2).value();
        QCOMPARE(p1.size(), 2);
        QCOMPARE(p2.size(), 1);
        QCOMPARE(p1.at(0).trackId, t1);
        QCOMPARE(p1.at(1).trackId, t2);
        QCOMPARE(p2.at(0).trackId, t3);

        // 3. Filters
        TrackFilter fAlb;
        fAlb.albumId = alb1;
        QCOMPARE(q.countTracks(fAlb).value(), 2);
        TrackFilter fArt;
        fArt.artistId = art1;
        QCOMPARE(q.countTracks(fArt).value(), 1);
        TrackFilter fFav;
        fFav.favoritesOnly = true;
        QCOMPARE(q.countTracks(fFav).value(), 1);

        // 4. Missing files
        exec(qDb, QStringLiteral("UPDATE files SET missing_since=100 WHERE id=%1").arg(f1));
        QCOMPARE(q.countTracks({ }).value(), 2);
        QCOMPARE(q.countAlbums({ }).value(), 1); // album 1 still has t2
        QCOMPARE(q.countArtists({ }).value(), 0); // art1 has no visible tracks
        exec(qDb, QStringLiteral("UPDATE files SET missing_since=NULL WHERE id=%1").arg(f1));
        QCOMPARE(q.countTracks({ }).value(), 3);

        // 5. track_sort sync
        exec(qDb,
            QStringLiteral("INSERT INTO user_overrides (track_id, field, value, created_at, "
                           "updated_at) VALUES (%1, 'title', 'Z', 1, 1)")
                .arg(t1));
        auto tAscZ = q.tracks({ }, TrackSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(tAscZ.at(0).trackId, t2);
        QCOMPARE(tAscZ.at(1).trackId, t1); // Z is after B
        exec(qDb, QStringLiteral("UPDATE tracks SET album_id=%1 WHERE id=%2").arg(alb1).arg(t3));
        QCOMPARE(q.countTracks(fAlb).value(), 3);

        // 6. tracksByIds
        auto byIds = q.tracksByIds({ t3, 99, t1, t1 }).value();
        QCOMPARE(byIds.size(), 3);
        QCOMPARE(byIds.at(0).trackId, t3);
        QCOMPARE(byIds.at(1).trackId, t1);
        QCOMPARE(byIds.at(2).trackId, t1);

        // 7. Albums/Artists list & stats
        exec(qDb,
            QStringLiteral("INSERT INTO covers (id, hash, mime, source, created_at) VALUES (1, "
                           "'hash_abc', 'image/jpeg', 'embedded', 1)"));
        exec(qDb, QStringLiteral("UPDATE albums SET cover_id=1 WHERE id=%1").arg(alb1));
        auto albList = q.albums({ }, AlbumSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(albList.size(), 1);
        QCOMPARE(albList.at(0).trackCount, 3);
        QCOMPARE(albList.at(0).coverHash, QStringLiteral("hash_abc"));
        exec(qDb,
            QStringLiteral("INSERT INTO album_artists (album_id, artist_id) VALUES (%1, %2)")
                .arg(alb1)
                .arg(art1));
        auto artList = q.artists({ }, ArtistSortKey::Name, Qt::AscendingOrder, 0, 10).value();
        QCOMPARE(artList.size(), 1);
        QCOMPARE(artList.at(0).trackCount, 1);
        QCOMPARE(artList.at(0).coverHash, QStringLiteral("hash_abc"));

        // 8. album_artist fallback
        qint64 albNoArtist = DbHelper::insertAlbum(qDb, u"NoArtistAlbum"_s, QVariant());
        qint64 f4 = DbHelper::insertFile(qDb, r, u"4.mp3"_s, 400);
        qint64 t4 = DbHelper::insertTrack(qDb, f4, albNoArtist);
        DbHelper::setMeta(qDb, t4, u"Track1"_s, u"FreqArtist"_s, u"NoArtistAlbum"_s, QVariant());

        qint64 f5 = DbHelper::insertFile(qDb, r, u"5.mp3"_s, 500);
        qint64 t5 = DbHelper::insertTrack(qDb, f5, albNoArtist);
        DbHelper::setMeta(qDb, t5, u"Track2"_s, u"FreqArtist"_s, u"NoArtistAlbum"_s, QVariant());

        qint64 f6 = DbHelper::insertFile(qDb, r, u"6.mp3"_s, 600);
        qint64 t6 = DbHelper::insertTrack(qDb, f6, albNoArtist);
        DbHelper::setMeta(qDb, t6, u"Track3"_s, u"OtherArtist"_s, u"NoArtistAlbum"_s, QVariant());

        auto albListFallback
            = q.albums({ }, AlbumSortKey::Title, Qt::AscendingOrder, 0, 10).value();
        bool found = false;
        for (const auto &a : albListFallback) {
            if (a.title == u"NoArtistAlbum"_s) {
                QCOMPARE(a.albumArtist, u"FreqArtist"_s);
                found = true;
            }
        }
        QVERIFY(found);

        auto singleAlb = q.album(albNoArtist).value();
        QVERIFY(singleAlb.has_value());
        QCOMPARE(singleAlb.value_or(AlbumRow { }).albumArtist, u"FreqArtist"_s);

        // 9. trackIds matches tracks() order
        auto allIdsAsc = q.trackIds({ }, TrackSortKey::Title, Qt::AscendingOrder).value();
        auto allTracksAsc = q.tracks({ }, TrackSortKey::Title, Qt::AscendingOrder, 0, 100).value();
        QList<qint64> expectedAsc;
        for (const auto &trk : allTracksAsc) {
            expectedAsc.append(trk.trackId);
        }
        QCOMPARE(allIdsAsc, expectedAsc);
    }

    void migrationFillsTrackSort()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath(u"mig.db"_s);
        const QString migDir = dir.filePath(u"m4"_s);
        QVERIFY(QDir().mkpath(migDir));
        const QDir resources(u":/migrations"_s);
        for (const QFileInfo &fi : resources.entryInfoList(QDir::Files)) {
            if (fi.fileName() < u"0005"_s) {
                QVERIFY(QFile::copy(fi.absoluteFilePath(), migDir + u'/' + fi.fileName()));
            }
        }
        {
            Database db(dbPath);
            QVERIFY(db.open(Migrator(migDir)).ok());
            const auto qDb = db.connection().value();
            exec(qDb, u"INSERT INTO library_roots (id, path, added_at) VALUES (1, '/m', 100)"_s);
            exec(qDb,
                u"INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                "first_seen_at, scanned_at) VALUES (1, 1, '1.mp3', 100, 1, 1, 1, 1)"_s);
            exec(qDb,
                u"INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                "VALUES (1, 1, 1, 1)"_s);
            exec(qDb, u"UPDATE effective_metadata SET title = 'Old' WHERE track_id = 1"_s);
        }
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
        QSqlQuery q(db.connection().value());
        QVERIFY(q.exec(u"SELECT title, duration_ms, visible FROM track_sort WHERE track_id = 1"_s));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), u"Old"_s);
        QCOMPARE(q.value(1).toLongLong(), 100LL);
        QCOMPARE(q.value(2).toInt(), 1);
    }
};

} // namespace

QTEST_GUILESS_MAIN(TstLibraryQuery)
#include "tst_LibraryQuery.moc"
