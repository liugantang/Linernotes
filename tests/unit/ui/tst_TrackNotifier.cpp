// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/TestSupport.h>
#include <core/PlaySource.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/Player.h>
#include <ui/AppContext.h>
#include <ui/LibraryActions.h>
#include <ui/NotificationSink.h>
#include <ui/NowPlaying.h>
#include <ui/SettingsController.h>
#include <ui/TrackNotifier.h>

#include <vector>

using linernotes::core::PlaySource;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::player::Player;
using linernotes::test::fixturePath;
using linernotes::ui::AppContext;
using linernotes::ui::NotificationSink;
using linernotes::ui::TrackNotifier;

namespace {

struct NotificationRecord {
    QString summary;
    QString body;
    QString iconPath;
};

class FakeNotificationSink : public NotificationSink {
public:
    void show(const QString &summary, const QString &body, const QString &iconPath) override
    {
        records.push_back({ .summary = summary, .body = body, .iconPath = iconPath });
    }

    std::vector<NotificationRecord> records;
};

class TstTrackNotifier : public QObject {
    Q_OBJECT

private slots:
    void testTrackNotificationAndDeduplication();
    void testNotificationDisabled();
};

void populateDatabase(const QString &dbPath)
{
    Database db(dbPath);
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                  "VALUES (1, '/music', 0, 100);")));
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO albums (id, grouping_key, title, album_artist, created_at) "
                       "VALUES (1, 'K1', 'Album One', 'Artist One', 100);")));

    const QString file1 = fixturePath(QStringLiteral("audio/silence_5s.flac"));
    const QString file2 = fixturePath(QStringLiteral("audio/tone_440_1s.flac"));
    q.prepare(
        QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                       "first_seen_at, scanned_at) "
                       "VALUES (1, 1, :f1, 5000, 1, 1, 1, 1), (2, 1, :f2, 1000, 1, 1, 2, 1);"));
    q.bindValue(QStringLiteral(":f1"), file1);
    q.bindValue(QStringLiteral(":f2"), file2);
    QVERIFY(q.exec());

    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                       "VALUES (1, 1, 1, 1, 1), (2, 2, 1, 1, 1);")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', artist = "
                                  "'Artist 1', album = 'Album One' WHERE track_id = 1;")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 2', artist = "
                                  "'Artist 2', album = 'Album One' WHERE track_id = 2;")));
}

void TstTrackNotifier::testTrackNotificationAndDeduplication()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    populateDatabase(dbPath);

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
        .backupDir = { },
    };

    AppContext ctx(settings, options);
    QVERIFY(ctx.start().ok());

    FakeNotificationSink sink;
    TrackNotifier notifier(
        *ctx.nowPlaying(), *ctx.player(), *ctx.coverStore(), *ctx.settings(), sink);

    auto *actions = ctx.actions();
    QVERIFY(actions != nullptr);

    // 1. Play Track 1 -> notifies once
    actions->playTracks({ 1, 2 }, 0, PlaySource::Library);
    QTRY_COMPARE(sink.records.size(), static_cast<std::size_t>(1));
    QCOMPARE(sink.records.at(0).summary, QStringLiteral("Track 1"));
    QVERIFY(sink.records.at(0).body.contains(QStringLiteral("Artist 1")));
    QCOMPARE(sink.records.at(0).body, QStringLiteral("Artist 1 — Album One"));

    // 2. Same track changed / refresh -> no duplicate notification
    ctx.nowPlaying()->refresh();
    QTest::qWait(200);
    QCOMPARE(sink.records.size(), static_cast<std::size_t>(1));

    // 3. Switch to Track 2 -> notifies second track
    actions->playTracks({ 1, 2 }, 1, PlaySource::Library);
    QTRY_COMPARE(sink.records.size(), static_cast<std::size_t>(2));
    QCOMPARE(sink.records.at(1).summary, QStringLiteral("Track 2"));
    QVERIFY(sink.records.at(1).body.contains(QStringLiteral("Artist 2")));
    QCOMPARE(sink.records.at(1).body, QStringLiteral("Artist 2 — Album One"));
}

void TstTrackNotifier::testNotificationDisabled()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    populateDatabase(dbPath);

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));

    AppContext::Options options {
        .databasePath = dbPath,
        .coverCacheDir = tempDir.filePath(QStringLiteral("covers")),
        .playerOptions = { { QStringLiteral("ao"), QStringLiteral("null") } },
        .uiStatePath = tempDir.filePath(QStringLiteral("ui-state.ini")),
        .playbackStatePath = { },
        .backupDir = { },
    };

    AppContext ctx(settings, options);
    QVERIFY(ctx.start().ok());

    ctx.settings()->setTrackChangeNotifications(false);

    FakeNotificationSink sink;
    TrackNotifier notifier(
        *ctx.nowPlaying(), *ctx.player(), *ctx.coverStore(), *ctx.settings(), sink);

    auto *actions = ctx.actions();
    QVERIFY(actions != nullptr);

    // Play Track 1 when notifications are disabled -> no notification
    actions->playTracks({ 1, 2 }, 0, PlaySource::Library);
    QTRY_COMPARE(ctx.player()->state(), Player::PlaybackState::Playing);
    QTest::qWait(200);
    QVERIFY(sink.records.empty());

    // Switch to Track 2 -> still no notification
    actions->playTracks({ 1, 2 }, 1, PlaySource::Library);
    QTRY_COMPARE(ctx.player()->state(), Player::PlaybackState::Playing);
    QTest::qWait(200);
    QVERIFY(sink.records.empty());
}

} // namespace

QTEST_GUILESS_MAIN(TstTrackNotifier)
#include "tst_TrackNotifier.moc"
