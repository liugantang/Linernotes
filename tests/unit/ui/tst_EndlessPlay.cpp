// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <core/PlaySource.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <rec/Recommender.h>
#include <rec/SoundIndex.h>
#include <ui/AppSettings.h>
#include <ui/EndlessPlay.h>

#include <algorithm>

namespace {

using linernotes::core::PlaySource;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::player::PlayMode;
using linernotes::player::PlayQueue;
using linernotes::player::QueueItem;
using linernotes::rec::Recommender;
using linernotes::rec::SoundIndex;
using linernotes::test::ManualClock;
using linernotes::ui::EndlessPlay;
using linernotes::ui::kQueueEndless;

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

class TstEndlessPlay : public QObject {
    Q_OBJECT

private slots:
    void extendsWhenEnabledAndSequentialAtTail();
    void noExtendWhenDisabledOrOtherModes();
    void settingPersistence();
};

void TstEndlessPlay::extendsWhenEnabledAndSequentialAtTail()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_endless.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    QList<qint64> trackIds;
    for (int i = 1; i <= 15; ++i) {
        const QString path = QStringLiteral("/music/%1.mp3").arg(i);
        const qint64 fileId = DbHelper::insertFile(conn, rootId, path);
        const qint64 tid = DbHelper::insertTrack(conn, fileId);
        trackIds.append(tid);
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    const ManualClock clock(100000);
    SoundIndex soundIndex(db, clock);
    Recommender recommender(db, soundIndex, clock);

    PlayQueue queue(12345);
    const QList<QueueItem> initialItems {
        QueueItem {
            .source = QStringLiteral("/music/1.mp3"),
            .trackId = trackIds.at(0),
            .uid = 0,
            .playSource = PlaySource::Queue,
        },
        QueueItem {
            .source = QStringLiteral("/music/2.mp3"),
            .trackId = trackIds.at(1),
            .uid = 0,
            .playSource = PlaySource::Queue,
        },
    };
    queue.setItems(initialItems, 1);
    queue.setMode(PlayMode::Sequential);

    EndlessPlay endless(db, queue, recommender, settings, clock);
    QVERIFY(!endless.isEnabled());

    // Trigger check: enabling endless play or changing queue index / mode
    endless.setEnabled(true);

    // Queue should have been extended (initial 2 items + recommended items)
    QVERIFY(queue.count() > 2);
    QCOMPARE(queue.at(0).trackId, trackIds.at(0));
    QCOMPARE(queue.at(1).trackId, trackIds.at(1));

    // Check that excluded tracks (t1, t2) are not in the appended list
    for (int i = 2; i < queue.count(); ++i) {
        const auto &item = queue.at(i);
        QVERIFY(item.trackId != trackIds.at(0));
        QVERIFY(item.trackId != trackIds.at(1));
        QCOMPARE(item.playSource, PlaySource::Queue);
        QVERIFY(!item.source.isEmpty());
    }
}

void TstEndlessPlay::noExtendWhenDisabledOrOtherModes()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_endless_modes.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    QList<qint64> trackIds;
    for (int i = 1; i <= 15; ++i) {
        const QString path = QStringLiteral("/music/%1.mp3").arg(i);
        const qint64 fileId = DbHelper::insertFile(conn, rootId, path);
        const qint64 tid = DbHelper::insertTrack(conn, fileId);
        trackIds.append(tid);
    }

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    const ManualClock clock(100000);
    SoundIndex soundIndex(db, clock);
    Recommender recommender(db, soundIndex, clock);

    PlayQueue queue(12345);
    const QList<QueueItem> initialItems {
        QueueItem {
            .source = QStringLiteral("/music/1.mp3"),
            .trackId = trackIds.at(0),
            .uid = 0,
            .playSource = PlaySource::Queue,
        },
        QueueItem {
            .source = QStringLiteral("/music/2.mp3"),
            .trackId = trackIds.at(1),
            .uid = 0,
            .playSource = PlaySource::Queue,
        },
    };

    EndlessPlay endless(db, queue, recommender, settings, clock);

    // 1. Disabled (default false): at tail of sequential queue -> does not extend
    queue.setItems(initialItems, 1);
    queue.setMode(PlayMode::Sequential);
    QCOMPARE(queue.count(), 2);

    // 2. Enabled, but RepeatAll mode -> does not extend
    queue.setMode(PlayMode::RepeatAll);
    endless.setEnabled(true);
    queue.setItems(initialItems, 1);
    QCOMPARE(queue.count(), 2);

    // 3. Enabled, but RepeatOne mode -> does not extend
    queue.setItems(initialItems, 1);
    queue.setMode(PlayMode::RepeatOne);
    QCOMPARE(queue.count(), 2);

    // 4. Enabled, but Shuffle mode -> does not extend
    queue.setItems(initialItems, 1);
    queue.setMode(PlayMode::Shuffle);
    QCOMPARE(queue.count(), 2);

    // 5. Enabled, Sequential mode, but remaining tracks >= 2 (index 0 of 5) -> does not extend
    QList<QueueItem> fiveItems;
    for (int i = 0; i < 5; ++i) {
        fiveItems.append(QueueItem {
            .source = QStringLiteral("/music/%1.mp3").arg(i + 1),
            .trackId = trackIds.at(i),
            .uid = 0,
            .playSource = PlaySource::Queue,
        });
    }
    queue.setItems(fiveItems, 0); // remaining = 5 - 1 - 0 = 4 >= 2
    queue.setMode(PlayMode::Sequential);
    QCOMPARE(queue.count(), 5);
}

void TstEndlessPlay::settingPersistence()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_endless_settings.db")));
    QVERIFY(db.open(Migrator()).ok());

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    const ManualClock clock(100000);
    SoundIndex soundIndex(db, clock);
    Recommender recommender(db, soundIndex, clock);
    PlayQueue queue(12345);

    EndlessPlay endless1(db, queue, recommender, settings, clock);
    QCOMPARE(endless1.isEnabled(), false);

    endless1.setEnabled(true);
    QCOMPARE(endless1.isEnabled(), true);
    QCOMPARE(settings.value(kQueueEndless), true);

    EndlessPlay endless2(db, queue, recommender, settings, clock);
    QCOMPARE(endless2.isEnabled(), true);

    endless2.setEnabled(false);
    QCOMPARE(endless2.isEnabled(), false);
    QCOMPARE(settings.value(kQueueEndless), false);

    EndlessPlay endless3(db, queue, recommender, settings, clock);
    QCOMPARE(endless3.isEnabled(), false);
}

} // namespace

QTEST_GUILESS_MAIN(TstEndlessPlay)

#include "tst_EndlessPlay.moc"
