// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <ai/AiConfig.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/PromptLibrary.h>
#include <ai/SecretStore.h>
#include <ai/UsageStore.h>
#include <common/ManualClock.h>
#include <core/Settings.h>
#include <guide/LineExplainer.h>
#include <guide/Lyrics.h>
#include <guide/LyricsService.h>
#include <guide/LyricsStore.h>
#include <guide/LyricsTranslator.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <player/Player.h>
#include <ui/LyricsController.h>
#include <ui/NowPlaying.h>

namespace {

using linernotes::ai::AiConfig;
using linernotes::ai::LlmCache;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmDebugLog;
using linernotes::ai::LlmService;
using linernotes::ai::MemorySecretStore;
using linernotes::ai::PrivacyGuard;
using linernotes::ai::PromptLibrary;
using linernotes::ai::UsageStore;
using linernotes::core::Settings;
using linernotes::guide::LineExplainer;
using linernotes::guide::LyricsService;
using linernotes::guide::LyricsSource;
using linernotes::guide::LyricsStatus;
using linernotes::guide::LyricsTranslator;
using linernotes::library::CoverStore;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::player::Player;
using linernotes::test::ManualClock;
using linernotes::ui::LyricsController;
using linernotes::ui::NowPlaying;

class TstLyricsController : public QObject {
    Q_OBJECT

private slots:
    void testLyricsFlow();
};

void TstLyricsController::testLyricsFlow()
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
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO albums (id, grouping_key, title, album_artist, created_at) "
                       "VALUES (1, 'K1', 'Album One', 'Artist One', 100);")));
    // 同目录放一个同名 .lrc：清除手动歌词后落到它，测试不联网
    const QString audioPath = tempDir.filePath(QStringLiteral("1.mp3"));
    QFile lrc(tempDir.filePath(QStringLiteral("1.lrc")));
    QVERIFY(lrc.open(QIODevice::WriteOnly));
    lrc.write("[00:01.00]A\n[00:02.00]B\n[00:03.00]C\n[00:04.00]D\n");
    lrc.close();
    q.prepare(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, mtime, "
                             "first_seen_at, scanned_at) VALUES (1, 1, ?, 60000, 1, 1, 1, 1);"));
    q.addBindValue(audioPath);
    QVERIFY(q.exec());
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO tracks (id, file_id, album_id, tags_read_at, created_at) "
                       "VALUES (1, 1, 1, 1, 1);")));
    QVERIFY(q.exec(QStringLiteral("UPDATE effective_metadata SET title = 'Track 1', artist = "
                                  "'Artist 1', album = 'Album One' WHERE track_id = 1;")));

    const ManualClock clock(1000000);
    QNetworkAccessManager network;
    LyricsService lyricsService(db, network, clock);
    Player player;
    CoverStore coverStore(tempDir.filePath(QStringLiteral("covers")));
    NowPlaying nowPlaying(db, player, coverStore);

    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    AiConfig aiConfig(settings);
    MemorySecretStore secrets;
    LlmClient client(network);
    LlmCache llmCache(db, clock);
    UsageStore usageStore(db);
    PrivacyGuard privacy(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    LlmDebugLog debugLog(settings);
    LlmService llm(aiConfig, secrets, client, llmCache, usageStore, privacy, debugLog, clock);
    LyricsTranslator translator(llm, prompts, db, clock);
    LineExplainer explainer(llm, prompts);

    LyricsController controller(
        lyricsService, translator, explainer, aiConfig, settings, player, nowPlaying);

    QSignalSpy linesSpy(&controller, &LyricsController::linesChanged);
    QSignalSpy statusSpy(&controller, &LyricsController::statusChanged);
    QSignalSpy indexSpy(&controller, &LyricsController::currentIndexChanged);

    // 1. Initial state: nothing playing
    QCOMPARE(controller.loading(), false);
    QCOMPARE(controller.synced(), false);
    QCOMPARE(controller.currentIndex(), -1);
    QCOMPARE(controller.lines().size(), 0);

    // 2. Play track 1
    player.queue()->setItems({ linernotes::player::QueueItem {
                                 .source = audioPath,
                                 .trackId = 1,
                             } },
        0);
    QCOMPARE(nowPlaying.trackId(), 1LL);

    // 3. Save manual lyrics
    const QString lrcText
        = QStringLiteral("[00:01.00] Line 1\n[00:03.00] Line 2\n[00:05.00] Line 3\n");
    controller.saveManual(lrcText);

    QCOMPARE(controller.status(), LyricsStatus::Found);
    QCOMPARE(controller.source(), LyricsSource::Manual);
    QCOMPARE(controller.synced(), true);
    QCOMPARE(controller.lines().size(), 3);
    QCOMPARE(controller.rawText(), lrcText);

    // 4. Seek to line 1 (time 3.0s)
    controller.seekToLine(1);

    // 5. Clear manual lyrics (save empty string)
    controller.saveManual(QString());
    QCOMPARE(controller.status(), LyricsStatus::Found);
    QCOMPARE(controller.source(), LyricsSource::LrcFile);
    QCOMPARE(controller.lines().size(), 4);
}

} // namespace

QTEST_GUILESS_MAIN(TstLyricsController)

#include "tst_LyricsController.moc"
