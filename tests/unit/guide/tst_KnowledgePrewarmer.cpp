// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <common/ManualClock.h>
#include <core/Settings.h>
#include <guide/GuideEnums.h>
#include <guide/GuideSettings.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgePrewarmer.h>
#include <guide/KnowledgeService.h>
#include <guide/KnowledgeStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::core::Settings;
using linernotes::guide::kGuideLastPrewarmAt;
using linernotes::guide::KnowledgeEntry;
using linernotes::guide::knowledgeKey;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::KnowledgePrewarmer;
using linernotes::guide::KnowledgeQuery;
using linernotes::guide::KnowledgeService;
using linernotes::guide::KnowledgeStore;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

class TstKnowledgePrewarmer : public QObject {
    Q_OBJECT

private slots:
    void prewarmAllCachedCompletesSynchronously();
    void maybeStartSkipsWithinSevenDays();
};

void TstKnowledgePrewarmer::prewarmAllCachedCompletesSynchronously()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("prewarm1.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);
    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO library_roots (id, path, enabled, added_at) VALUES (1, '/music', 1, 1);")));
    const qint64 rootId = q.lastInsertId().toLongLong();

    q.prepare(
        QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                       "first_seen_at, scanned_at) VALUES (?, ?, 1000, 1000, 200000, 1, 1);"));
    q.addBindValue(rootId);
    q.addBindValue(QStringLiteral("/music/song.flac"));
    QVERIFY(q.exec());
    const qint64 fileId = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                             "VALUES (?, 1, 1);"));
    q.addBindValue(fileId);
    QVERIFY(q.exec());
    const qint64 trackId = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral("UPDATE effective_metadata SET title = ?, artist = ?, album = ? "
                             "WHERE track_id = ?;"));
    q.addBindValue(QStringLiteral("IGNITE"));
    q.addBindValue(QStringLiteral("藍井エイル"));
    q.addBindValue(QStringLiteral("D'AZUR"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral("INSERT INTO track_play_stats (track_id, play_count, last_played_at) "
                             "VALUES (?, 5, 1000000000);"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());

    KnowledgeStore store(db, clock);

    KnowledgeEntry artistEntry;
    artistEntry.kind = KnowledgeKind::Artist;
    artistEntry.qid = QStringLiteral("Q4021857");
    artistEntry.label = QStringLiteral("藍井エイル");
    artistEntry.fetchedAtMs = 1000000000;
    const KnowledgeQuery artistQ {
        .kind = KnowledgeKind::Artist,
        .artist = QStringLiteral("藍井エイル"),
        .title = { },
    };
    QVERIFY(store.saveFound(knowledgeKey(artistQ), artistEntry).ok());

    KnowledgeEntry songEntry;
    songEntry.kind = KnowledgeKind::Song;
    songEntry.qid = QStringLiteral("Q17988679");
    songEntry.label = QStringLiteral("IGNITE");
    songEntry.fetchedAtMs = 1000000000;
    const KnowledgeQuery songQ {
        .kind = KnowledgeKind::Song,
        .artist = QStringLiteral("藍井エイル"),
        .title = QStringLiteral("IGNITE"),
    };
    QVERIFY(store.saveFound(knowledgeKey(songQ), songEntry).ok());

    const KnowledgeQuery albumQ {
        .kind = KnowledgeKind::Album,
        .artist = QStringLiteral("藍井エイル"),
        .title = QStringLiteral("D'AZUR"),
    };
    QVERIFY(store.saveNotFound(knowledgeKey(albumQ)).ok());

    QNetworkAccessManager nam;
    QSignalSpy namSpy(&nam, &QNetworkAccessManager::finished);

    KnowledgeService service(db, nam, clock);
    KnowledgePrewarmer prewarmer(db, service, clock, settings);
    QSignalSpy runningSpy(&prewarmer, &KnowledgePrewarmer::runningChanged);

    prewarmer.start();

    QTRY_COMPARE(runningSpy.count(), 2);
    QCOMPARE(prewarmer.isRunning(), false);
    QCOMPARE(namSpy.count(), 0);
    QCOMPARE(settings.value(kGuideLastPrewarmAt), 1000000000LL);
}

void TstKnowledgePrewarmer::maybeStartSkipsWithinSevenDays()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("prewarm2.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);
    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    settings.setValue(kGuideLastPrewarmAt, 1000000000LL);

    QNetworkAccessManager nam;
    KnowledgeService service(db, nam, clock);
    KnowledgePrewarmer prewarmer(db, service, clock, settings);
    QSignalSpy runningSpy(&prewarmer, &KnowledgePrewarmer::runningChanged);

    clock.advance(6LL * 24 * 60 * 60 * 1000);
    prewarmer.maybeStart();
    QCOMPARE(runningSpy.count(), 0);
    QCOMPARE(prewarmer.isRunning(), false);

    clock.advance(2LL * 24 * 60 * 60 * 1000);
    prewarmer.maybeStart();
    QTRY_COMPARE(runningSpy.count(), 2);
    QCOMPARE(prewarmer.isRunning(), false);
    QCOMPARE(settings.value(kGuideLastPrewarmAt), clock.nowMs());
}

} // namespace

QTEST_GUILESS_MAIN(TstKnowledgePrewarmer)

#include "tst_KnowledgePrewarmer.moc"
