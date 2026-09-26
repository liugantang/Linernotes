// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QAbstractItemModelTester>
#include <QObject>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <ui/AlbumGridModel.h>
#include <ui/AppContext.h>
#include <ui/ArtistListModel.h>
#include <ui/TrackListModel.h>

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::ui::AlbumGridModel;
using linernotes::ui::AppContext;
using linernotes::ui::ArtistListModel;
using linernotes::ui::TrackListModel;

namespace {

void execSql(const QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        QFAIL(qPrintable(sql + QStringLiteral(" -> ") + q.lastError().text()));
    }
}

class TstTrackListModel : public QObject {
    Q_OBJECT

private slots:
    void testTrackListModelAndSharedModels();
};

void TstTrackListModel::testTrackListModelAndSharedModels()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));

    {
        Database db(dbPath);
        QVERIFY(db.open(Migrator()).ok());
        const auto conn = db.connection().value();

        execSql(conn,
            QStringLiteral(
                "INSERT INTO library_roots (id, path, enabled, added_at) VALUES (1, '/music', 0, "
                "100);"));
        execSql(conn,
            QStringLiteral("INSERT INTO albums (id, grouping_key, title, album_artist, created_at) "
                           "VALUES (1, 'Al1AA', 'Al1', 'AA', 100);"));
        execSql(conn,
            QStringLiteral("INSERT INTO artists (id, name, created_at) VALUES (1, 'Ar1', 100);"));

        // File 1 & Track 1
        execSql(conn,
            QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                           "first_seen_at, scanned_at) "
                           "VALUES (1, 1, '/music/1.mp3', 65000, 1, 1, 1, 1);"));
        execSql(conn,
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (1, 1, 1, 1, 1);"));
        execSql(conn,
            QStringLiteral("UPDATE effective_metadata SET title = 'TitleA', artist = 'Ar1', album "
                           "= 'Al1', album_artist = 'AA' WHERE track_id = 1;"));
        execSql(conn,
            QStringLiteral("INSERT INTO track_artists (track_id, artist_id, role) VALUES (1, 1, "
                           "'artist');"));

        // File 2 & Track 2
        execSql(conn,
            QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                           "first_seen_at, scanned_at) "
                           "VALUES (2, 1, '/music/2.mp3', 120000, 1, 1, 2, 1);"));
        execSql(conn,
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (2, 2, 1, 1, 1);"));
        execSql(conn,
            QStringLiteral("UPDATE effective_metadata SET title = 'TitleB', artist = 'Ar1', album "
                           "= 'Al1', album_artist = 'AA' WHERE track_id = 2;"));

        // File 3 & Track 3 (empty title -> fallback to filename)
        execSql(conn,
            QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                           "first_seen_at, scanned_at) "
                           "VALUES (3, 1, '/music/fallback_track.flac', 3661000, 1, 1, 3, 1);"));
        execSql(conn,
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (3, 3, NULL, 1, 1);"));
        execSql(conn,
            QStringLiteral("UPDATE effective_metadata SET title = NULL, artist = NULL, album = "
                           "NULL WHERE track_id = 3;"));
    }

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
    };

    AppContext ctx(options);
    QVERIFY(ctx.start().ok());

    TrackListModel model;
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::QtTest, &model);
    Q_UNUSED(tester);

    QCOMPARE(model.count(), 0);
    model.setContext(&ctx);

    // 1. 行数与 title、durationText 角色值；title 为空时回退为文件名
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.data(model.index(0, 0), TrackListModel::TitleRole).toString(),
        QStringLiteral("TitleA"));
    QCOMPARE(model.data(model.index(0, 0), TrackListModel::DurationTextRole).toString(),
        QStringLiteral("1:05"));
    QCOMPARE(model.data(model.index(1, 0), TrackListModel::TitleRole).toString(),
        QStringLiteral("TitleB"));
    QCOMPARE(model.data(model.index(1, 0), TrackListModel::DurationTextRole).toString(),
        QStringLiteral("2:00"));
    QCOMPARE(model.data(model.index(2, 0), TrackListModel::TitleRole).toString(),
        QStringLiteral("fallback_track"));
    QCOMPARE(model.data(model.index(2, 0), TrackListModel::DurationTextRole).toString(),
        QStringLiteral("1:01:01"));

    // 2. pageSize 设为 2 时跨页读取正确
    model.setPageSize(2);
    QCOMPARE(model.pageSize(), 2);
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.data(model.index(0, 0), TrackListModel::TrackIdRole).toLongLong(), 1LL);
    QCOMPARE(model.data(model.index(1, 0), TrackListModel::TrackIdRole).toLongLong(), 2LL);
    QCOMPARE(model.data(model.index(2, 0), TrackListModel::TrackIdRole).toLongLong(), 3LL);

    // 3. 修改 sortOrder → modelReset，顺序反转
    {
        QSignalSpy spyReset(&model, &QAbstractItemModel::modelReset);
        model.setSortKey(TrackListModel::SortKey::Title);
        model.setSortOrder(Qt::DescendingOrder);
        QVERIFY(spyReset.count() >= 1);
        // Descending by title: TitleB (2) -> TitleA (1) -> NULL (3)
        QCOMPARE(model.data(model.index(0, 0), TrackListModel::TrackIdRole).toLongLong(), 2LL);
        QCOMPARE(model.data(model.index(1, 0), TrackListModel::TrackIdRole).toLongLong(), 1LL);
        QCOMPARE(model.data(model.index(2, 0), TrackListModel::TrackIdRole).toLongLong(), 3LL);
    }

    // 4. 增加一首后 emit ctx.libraryChanged() → 发 rowsInserted 而不是 modelReset
    {
        const auto conn = ctx.database()->connection().value();
        execSql(conn,
            QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                           "first_seen_at, scanned_at) "
                           "VALUES (4, 1, '/music/4.mp3', 30000, 1, 1, 4, 1);"));
        execSql(conn,
            QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                           "VALUES (4, 4, NULL, 1, 1);"));
        execSql(conn,
            QStringLiteral("UPDATE effective_metadata SET title = 'TitleC' WHERE track_id = 4;"));

        QSignalSpy spyRowsInserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy spyReset(&model, &QAbstractItemModel::modelReset);

        emit ctx.libraryChanged();

        QCOMPARE(spyReset.count(), 0);
        QCOMPARE(spyRowsInserted.count(), 1);
        QCOMPARE(model.count(), 4);
    }

    // 5. allTrackIds 与当前排序一致
    {
        const auto allIds = model.allTrackIds();
        QCOMPARE(allIds.size(), 4);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(allIds.at(i), model.trackIdAt(i));
        }
        const auto subsetIds = model.trackIds({ 0, 2, 99 });
        QCOMPARE(subsetIds.size(), 2);
        QCOMPARE(subsetIds.at(0), allIds.at(0));
        QCOMPARE(subsetIds.at(1), allIds.at(2));
    }

    // AlbumGridModel & ArtistListModel 基础断言
    {
        AlbumGridModel albumModel;
        QAbstractItemModelTester albumTester(
            &albumModel, QAbstractItemModelTester::FailureReportingMode::QtTest, &albumModel);
        Q_UNUSED(albumTester);
        albumModel.setContext(&ctx);
        QCOMPARE(albumModel.count(), 1);
        QCOMPARE(albumModel.data(albumModel.index(0, 0), AlbumGridModel::TitleRole).toString(),
            QStringLiteral("Al1"));

        ArtistListModel artistModel;
        QAbstractItemModelTester artistTester(
            &artistModel, QAbstractItemModelTester::FailureReportingMode::QtTest, &artistModel);
        Q_UNUSED(artistTester);
        artistModel.setContext(&ctx);
        QCOMPARE(artistModel.count(), 1);
        QCOMPARE(artistModel.data(artistModel.index(0, 0), ArtistListModel::NameRole).toString(),
            QStringLiteral("Ar1"));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstTrackListModel)
#include "tst_TrackListModel.moc"
