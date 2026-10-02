// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/Player.h>
#include <rec/Recommender.h>
#include <rec/SoundIndex.h>
#include <ui/AppSettings.h>
#include <ui/LibraryActions.h>
#include <ui/PlaylistController.h>
#include <ui/RecommendController.h>

namespace {

using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::player::Player;
using linernotes::rec::Recommender;
using linernotes::rec::SoundIndex;
using linernotes::test::ManualClock;
using linernotes::ui::kRecDailyDate;
using linernotes::ui::kRecDailyTracks;
using linernotes::ui::LibraryActions;
using linernotes::ui::PlaylistController;
using linernotes::ui::RecommendController;

struct DbHelper {
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
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                 "duration_ms, first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1024, 2000, 'hash', 60000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 fileId, const QString &title = QString())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                                 "VALUES (?, 1000, 3000);"));
        q.addBindValue(fileId);
        if (!q.exec()) {
            return -1;
        }
        const qint64 trackId = q.lastInsertId().toLongLong();
        QSqlQuery qMeta(db);
        qMeta.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO effective_metadata (track_id, title, updated_at) "
            "VALUES (?, ?, 1000);"));
        qMeta.addBindValue(trackId);
        qMeta.addBindValue(title.isEmpty() ? QStringLiteral("Track %1").arg(trackId) : title);
        if (!qMeta.exec()) {
            return -1;
        }
        return trackId;
    }
};

class TstRecommendController : public QObject {
    Q_OBJECT

private slots:
    void dailyFixedForTodayAndUpdatesNextDay();
    void forYouRefreshChangesSeedAndUpdatesRows();
};

void TstRecommendController::dailyFixedForTodayAndUpdatesNextDay()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_rec_ctrl.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    QList<qint64> trackIds;
    for (int i = 1; i <= 10; ++i) {
        const QString path = QStringLiteral("/music/%1.mp3").arg(i);
        const qint64 fileId = DbHelper::insertFile(conn, rootId, path);
        const qint64 tid = DbHelper::insertTrack(conn, fileId);
        trackIds.append(tid);
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    const qint64 day1Ms = 1790942400000LL;
    ManualClock clock(day1Ms);
    SoundIndex soundIndex(db, clock);
    Recommender recommender(db, soundIndex, clock);
    Player player;
    PlaylistController playlists(db, player);
    LibraryActions actions(db, player, settings);

    RecommendController controller(db, recommender, playlists, actions, settings, clock);
    QCOMPARE(controller.kind(), RecommendController::Kind::Daily);

    QSignalSpy spy(&controller, &RecommendController::rowsChanged);

    // 1. Initial load
    controller.load();
    QCOMPARE(spy.count(), 1);

    const auto rows1 = controller.rows();
    QVERIFY(!rows1.isEmpty());

    const QString date1 = settings.value(kRecDailyDate);
    const QString tracks1 = settings.value(kRecDailyTracks);
    QVERIFY(!date1.isEmpty());
    QVERIFY(!tracks1.isEmpty());

    const QDate today1 = QDateTime::fromMSecsSinceEpoch(day1Ms).toLocalTime().date();
    QCOMPARE(date1, today1.toString(QStringLiteral("yyyy-MM-dd")));

    // 2. Second load on the same day -> result identical, settings unchanged
    controller.load();
    QCOMPARE(spy.count(), 2);
    const auto rows2 = controller.rows();
    QCOMPARE(rows2, rows1);
    QCOMPARE(settings.value(kRecDailyDate), date1);
    QCOMPARE(settings.value(kRecDailyTracks), tracks1);

    // 3. Advance clock by 1 day -> load() regenerates for next day
    const qint64 day2Ms = day1Ms + (24LL * 3600 * 1000) + 1000;
    clock.set(day2Ms);

    controller.load();
    QCOMPARE(spy.count(), 3);

    const QString date2 = settings.value(kRecDailyDate);
    const QDate today2 = QDateTime::fromMSecsSinceEpoch(day2Ms).toLocalTime().date();
    QCOMPARE(date2, today2.toString(QStringLiteral("yyyy-MM-dd")));
    QVERIFY(date2 != date1);
}

void TstRecommendController::forYouRefreshChangesSeedAndUpdatesRows()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_rec_ctrl_foryou.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    for (int i = 1; i <= 20; ++i) {
        const QString path = QStringLiteral("/music/%1.mp3").arg(i);
        const qint64 fileId = DbHelper::insertFile(conn, rootId, path);
        DbHelper::insertTrack(conn, fileId);
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    const qint64 nowMs = 1790942400000LL;
    ManualClock clock(nowMs);
    SoundIndex soundIndex(db, clock);
    Recommender recommender(db, soundIndex, clock);
    Player player;
    PlaylistController playlists(db, player);
    LibraryActions actions(db, player, settings);

    RecommendController controller(db, recommender, playlists, actions, settings, clock);
    controller.setKind(RecommendController::Kind::ForYou);
    QCOMPARE(controller.kind(), RecommendController::Kind::ForYou);

    QSignalSpy spy(&controller, &RecommendController::rowsChanged);

    // Initial load
    controller.load();
    QVERIFY(spy.count() >= 1);
    const auto rows1 = controller.rows();
    QVERIFY(!rows1.isEmpty());

    // Refresh should emit rowsChanged and provide rows
    const auto countBefore = spy.count();
    controller.refresh();
    QCOMPARE(spy.count(), countBefore + 1);

    const auto rows2 = controller.rows();
    QCOMPARE(rows2.size(), rows1.size());
}

} // namespace

QTEST_GUILESS_MAIN(TstRecommendController)

#include "tst_RecommendController.moc"
