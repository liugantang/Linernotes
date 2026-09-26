// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <library/Migrator.h>
#include <library/SearchIndex.h>

namespace {

using linernotes::library::Database;
using linernotes::library::LibraryQuery;
using linernotes::library::Migrator;
using linernotes::library::SearchIndex;
using linernotes::library::SearchResults;

class TstLibrarySearch : public QObject {
    Q_OBJECT

private slots:
    void searchGrouped();
    void blankQueryReturnsEmpty();
};

void TstLibrarySearch::searchGrouped()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_test.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    QSqlQuery q(qDb);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES ('/music', 1000);")));
    const qint64 rootId = q.lastInsertId().toLongLong();

    auto insertFile = [&](const QString &p, qint64 durationMs = 200000) {
        q.prepare(
            QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                           "first_seen_at, scanned_at) VALUES (?, ?, 1024, 1000, ?, 1000, 1000);"));
        q.addBindValue(rootId);
        q.addBindValue(p);
        q.addBindValue(durationMs);
        if (!q.exec()) {
            return qint64 { 0 };
        }
        return q.lastInsertId().toLongLong();
    };

    auto insertArtist = [&](const QString &name) {
        q.prepare(QStringLiteral("INSERT INTO artists (name, created_at) VALUES (?, 1000);"));
        q.addBindValue(name);
        if (!q.exec()) {
            return qint64 { 0 };
        }
        return q.lastInsertId().toLongLong();
    };

    auto insertAlbum = [&](const QString &title, const QString &albumArtist) {
        q.prepare(
            QStringLiteral("INSERT INTO albums (grouping_key, title, album_artist, created_at) "
                           "VALUES (?, ?, ?, 1000);"));
        q.addBindValue(QString(title + u'_' + albumArtist));
        q.addBindValue(title);
        q.addBindValue(albumArtist);
        if (!q.exec()) {
            return qint64 { 0 };
        }
        return q.lastInsertId().toLongLong();
    };

    auto insertTrack = [&](qint64 fileId, qint64 albumId, const QString &title,
                           const QString &artist, const QString &album, const QString &albumArtist,
                           qint64 artistId) {
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, album_id, tags_read_at, created_at) "
                                 "VALUES (?, ?, 1000, 1000);"));
        q.addBindValue(fileId);
        q.addBindValue(albumId);
        if (!q.exec()) {
            return qint64 { 0 };
        }
        const qint64 trackId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral(
            "UPDATE effective_metadata SET title = ?, artist = ?, album = ?, album_artist = ? "
            "WHERE track_id = ?;"));
        q.addBindValue(title);
        q.addBindValue(artist);
        q.addBindValue(album);
        q.addBindValue(albumArtist);
        q.addBindValue(trackId);
        if (!q.exec()) {
            return qint64 { 0 };
        }

        q.prepare(QStringLiteral("INSERT INTO track_artists (track_id, artist_id, role, position) "
                                 "VALUES (?, ?, 'artist', 0);"));
        q.addBindValue(trackId);
        q.addBindValue(artistId);
        if (!q.exec()) {
            return qint64 { 0 };
        }

        return trackId;
    };

    // 2 artists
    const qint64 artistA = insertArtist(QStringLiteral("周杰伦"));
    const qint64 artistB = insertArtist(QStringLiteral("林俊杰"));

    // 3 albums (2 for Artist A, 1 for Artist B)
    const qint64 album1 = insertAlbum(QStringLiteral("范特西"), QStringLiteral("周杰伦"));
    const qint64 album2 = insertAlbum(QStringLiteral("叶惠美"), QStringLiteral("周杰伦"));
    const qint64 album3 = insertAlbum(QStringLiteral("江南"), QStringLiteral("林俊杰"));

    // album_artists links
    q.prepare(QStringLiteral(
        "INSERT INTO album_artists (album_id, artist_id, position) VALUES (?, ?, 0);"));
    q.addBindValue(album1);
    q.addBindValue(artistA);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral(
        "INSERT INTO album_artists (album_id, artist_id, position) VALUES (?, ?, 0);"));
    q.addBindValue(album2);
    q.addBindValue(artistA);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral(
        "INSERT INTO album_artists (album_id, artist_id, position) VALUES (?, ?, 0);"));
    q.addBindValue(album3);
    q.addBindValue(artistB);
    QVERIFY(q.exec());

    // Tracks
    const qint64 f1 = insertFile(QStringLiteral("/music/qiantian.mp3"));
    const qint64 f2 = insertFile(QStringLiteral("/music/shuangjiegun.mp3"));
    const qint64 f3 = insertFile(QStringLiteral("/music/jiandanai.mp3"));
    const qint64 f4 = insertFile(QStringLiteral("/music/jiangnan.mp3"));

    const qint64 t1 = insertTrack(f1, album2, QStringLiteral("晴天"), QStringLiteral("周杰伦"),
        QStringLiteral("叶惠美"), QStringLiteral("周杰伦"), artistA);
    const qint64 t2 = insertTrack(f2, album1, QStringLiteral("双截棍"), QStringLiteral("周杰伦"),
        QStringLiteral("范特西"), QStringLiteral("周杰伦"), artistA);
    const qint64 t3 = insertTrack(f3, album1, QStringLiteral("简单爱"), QStringLiteral("周杰伦"),
        QStringLiteral("范特西"), QStringLiteral("周杰伦"), artistA);
    const qint64 t4 = insertTrack(f4, album3, QStringLiteral("一千年以后"),
        QStringLiteral("林俊杰"), QStringLiteral("江南"), QStringLiteral("林俊杰"), artistB);

    Q_UNUSED(t1);
    Q_UNUSED(t2);
    Q_UNUSED(t3);
    Q_UNUSED(t4);

    SearchIndex index(qDb);
    const auto flushRes = index.flushDirty();
    QVERIFY(flushRes.ok());
    QCOMPARE(flushRes.value(), 4);

    const LibraryQuery libraryQuery(qDb);

    // Search query matching only Artist A
    const auto res = libraryQuery.searchGrouped(QStringLiteral("周杰伦"));
    QVERIFY(res.ok());

    const SearchResults &results = res.value();

    // 1. tracks is non-empty and all tracks belong to Artist A
    QVERIFY(!results.tracks.isEmpty());
    QCOMPARE(results.tracks.size(), 3);
    for (const auto &track : results.tracks) {
        QCOMPARE(track.artist, QStringLiteral("周杰伦"));
    }

    // 2. albums is the albums of Artist A with no duplicates
    QCOMPARE(results.albums.size(), 2);
    QCOMPARE(results.albums.at(0).albumId, album2);
    QCOMPARE(results.albums.at(0).title, QStringLiteral("叶惠美"));
    QCOMPARE(results.albums.at(1).albumId, album1);
    QCOMPARE(results.albums.at(1).title, QStringLiteral("范特西"));

    // 3. artists contains only Artist A
    QCOMPARE(results.artists.size(), 1);
    QCOMPARE(results.artists.first().artistId, artistA);
    QCOMPARE(results.artists.first().name, QStringLiteral("周杰伦"));
}

void TstLibrarySearch::blankQueryReturnsEmpty()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_empty.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const LibraryQuery libraryQuery(qDb);

    for (const auto &input :
        { QStringLiteral(""), QStringLiteral("   "), QStringLiteral("\t\n") }) {
        const auto res = libraryQuery.searchGrouped(input);
        QVERIFY(res.ok());
        QVERIFY(res.value().tracks.isEmpty());
        QVERIFY(res.value().albums.isEmpty());
        QVERIFY(res.value().artists.isEmpty());
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstLibrarySearch)

#include "tst_LibrarySearch.moc"
