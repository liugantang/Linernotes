// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryEnums.h>
#include <library/LibraryQuery.h>
#include <library/MarkStore.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::FavoriteKind;
using linernotes::library::LibraryQuery;
using linernotes::library::MarkStore;
using linernotes::library::Migrator;
using linernotes::library::TrackFilter;
using linernotes::library::TrackSortKey;
namespace errc = linernotes::library::errc;

class TstMarkStore : public QObject {
    Q_OBJECT

private slots:
    void testFavorites();
    void testRatings();
    void testFavoritesQueryFilter();
};

void TstMarkStore::testFavorites()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    MarkStore store(db);

    // Initial state: not favorite
    const auto initialTrackFav = store.isFavorite(FavoriteKind::Track, 1);
    QVERIFY(initialTrackFav.ok());
    QCOMPARE(initialTrackFav.value(), false);

    // Set favorites for tracks 1 and 2
    const auto setFavRes = store.setFavorite(FavoriteKind::Track, { 1, 2 }, true);
    QVERIFY(setFavRes.ok());

    // Verify track favorites
    const auto fav1 = store.isFavorite(FavoriteKind::Track, 1);
    QVERIFY(fav1.ok());
    QCOMPARE(fav1.value(), true);

    const auto fav2 = store.isFavorite(FavoriteKind::Track, 2);
    QVERIFY(fav2.ok());
    QCOMPARE(fav2.value(), true);

    const auto fav3 = store.isFavorite(FavoriteKind::Track, 3);
    QVERIFY(fav3.ok());
    QCOMPARE(fav3.value(), false);

    // Repeated favorite does not error
    const auto repeatFavRes = store.setFavorite(FavoriteKind::Track, { 1 }, true);
    QVERIFY(repeatFavRes.ok());
    QCOMPARE(store.isFavorite(FavoriteKind::Track, 1).value(), true);

    // Set favorite for album 10
    const auto setAlbumFavRes = store.setFavorite(FavoriteKind::Album, { 10 }, true);
    QVERIFY(setAlbumFavRes.ok());

    const auto albumFav10 = store.isFavorite(FavoriteKind::Album, 10);
    QVERIFY(albumFav10.ok());
    QCOMPARE(albumFav10.value(), true);

    // Unfavorite track 1
    const auto unfavRes = store.setFavorite(FavoriteKind::Track, { 1 }, false);
    QVERIFY(unfavRes.ok());
    QCOMPARE(store.isFavorite(FavoriteKind::Track, 1).value(), false);
    QCOMPARE(store.isFavorite(FavoriteKind::Track, 2).value(), true);

    // Unfavorite album 10
    const auto unfavAlbumRes = store.setFavorite(FavoriteKind::Album, { 10 }, false);
    QVERIFY(unfavAlbumRes.ok());
    QCOMPARE(store.isFavorite(FavoriteKind::Album, 10).value(), false);
}

void TstMarkStore::testRatings()
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
                                  "VALUES (1, 1, 1, 1), (2, 2, 1, 1);")));

    MarkStore store(db);

    // Initial rating is 0
    const auto initialRating = store.rating(1);
    QVERIFY(initialRating.ok());
    QCOMPARE(initialRating.value(), 0);

    // Write rating
    const auto setRes = store.setRating({ 1, 2 }, 4);
    QVERIFY(setRes.ok());
    QCOMPARE(store.rating(1).value(), 4);
    QCOMPARE(store.rating(2).value(), 4);

    // Update rating
    const auto updateRes = store.setRating({ 1 }, 5);
    QVERIFY(updateRes.ok());
    QCOMPARE(store.rating(1).value(), 5);
    QCOMPARE(store.rating(2).value(), 4);

    // Clear rating with 0
    const auto clearRes = store.setRating({ 1 }, 0);
    QVERIFY(clearRes.ok());
    QCOMPARE(store.rating(1).value(), 0);

    // Rating 6 returns error
    const auto invalidRes1 = store.setRating({ 1 }, 6);
    QVERIFY(!invalidRes1.ok());
    QCOMPARE(invalidRes1.error().code, QString(errc::kRatingInvalid));

    // Rating -1 returns error
    const auto invalidRes2 = store.setRating({ 1 }, -1);
    QVERIFY(!invalidRes2.ok());
    QCOMPARE(invalidRes2.error().code, QString(errc::kRatingInvalid));
}

void TstMarkStore::testFavoritesQueryFilter()
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
                                  "VALUES (1, 1, 1, 1), (2, 2, 1, 1);")));

    MarkStore store(db);
    QVERIFY(store.setFavorite(FavoriteKind::Track, { 1 }, true).ok());

    const LibraryQuery query(conn);
    TrackFilter filter;
    filter.favoritesOnly = true;

    const auto res = query.tracks(filter, TrackSortKey::Default, Qt::AscendingOrder, 0, 10);
    QVERIFY(res.ok());
    QCOMPARE(res.value().size(), 1);
    QCOMPARE(res.value().first().trackId, 1LL);
    QCOMPARE(res.value().first().favorite, true);
}

} // namespace

QTEST_MAIN(TstMarkStore)
#include "tst_MarkStore.moc"
