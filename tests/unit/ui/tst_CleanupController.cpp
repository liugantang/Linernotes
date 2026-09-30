// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <ai/AiConfig.h>
#include <ai/JobQueue.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/PromptLibrary.h>
#include <ai/SecretStore.h>
#include <butler/AlbumInfoJobHandler.h>
#include <butler/AlbumInfoSource.h>
#include <butler/ArtistCredit.h>
#include <butler/ArtistCreditJobHandler.h>
#include <butler/ArtistCreditStore.h>
#include <butler/ArtistMergeJobHandler.h>
#include <butler/DuplicateFinder.h>
#include <butler/DuplicateJobHandler.h>
#include <butler/DuplicateSource.h>
#include <butler/FingerprintJobHandler.h>
#include <butler/MojibakeJobHandler.h>
#include <butler/VersionLinkJobHandler.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <core/Settings.h>
#include <library/ArtistAliasCorrections.h>
#include <library/CorrectionStore.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/FingerprintStore.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>
#include <ui/CleanupController.h>

namespace {

using linernotes::ai::AiConfig;
using linernotes::ai::JobQueue;
using linernotes::ai::LlmCache;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmDebugLog;
using linernotes::ai::LlmService;
using linernotes::ai::MemorySecretStore;
using linernotes::ai::PrivacyGuard;
using linernotes::ai::PromptLibrary;
using linernotes::ai::UsageStore;
using linernotes::butler::AlbumInfoJobHandler;
using linernotes::butler::AlbumInfoSource;
using linernotes::butler::ArtistCredit;
using linernotes::butler::ArtistCreditJobHandler;
using linernotes::butler::ArtistCreditStore;
using linernotes::butler::ArtistMergeJobHandler;
using linernotes::butler::CreditPerformer;
using linernotes::butler::DuplicateJobHandler;
using linernotes::butler::DuplicateKind;
using linernotes::butler::DuplicateSource;
using linernotes::butler::FingerprintJobHandler;
using linernotes::butler::MojibakeJobHandler;
using linernotes::butler::VersionLinkJobHandler;
using linernotes::core::Settings;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionStatus;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::FingerprintStore;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
using linernotes::ui::CleanupController;

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

    static qint64 insertFileWithDetails(const QSqlDatabase &db, qint64 rootId, const QString &path,
        const QString &contentHash, qint64 durationMs,
        const QString &codec = QStringLiteral("flac"), int sampleRate = 44100, int bitDepth = 16,
        int bitrate = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, content_hash, codec, sample_rate, "
            "bit_depth, bitrate, duration_ms, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, ?, ?, ?, ?, ?, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash);
        q.addBindValue(codec);
        q.addBindValue(sampleRate);
        q.addBindValue(bitDepth);
        q.addBindValue(bitrate);
        q.addBindValue(durationMs);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertWork(
        const QSqlDatabase &db, qint64 workId, const QString &groupingKey, const QString &title)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO works (id, grouping_key, title, created_at) VALUES (?, ?, ?, 1000);"));
        q.addBindValue(workId);
        q.addBindValue(groupingKey);
        q.addBindValue(title);
        return q.exec();
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrackWithWork(const QSqlDatabase &db, qint64 fileId, qint64 workId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, work_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, ?, 1000, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(workId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertTrackVersion(const QSqlDatabase &db, qint64 trackId, const QString &baseTitle,
        const QString &versionType = QStringLiteral("studio"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO track_versions (track_id, base_title, version_type, unresolved, "
            "updated_at) "
            "VALUES (?, ?, ?, 0, 1000);"));
        q.addBindValue(trackId);
        q.addBindValue(baseTitle);
        q.addBindValue(versionType);
        return q.exec();
    }

    static bool insertRawTag(const QSqlDatabase &db, qint64 trackId, const QString &key,
        const QString &value, const std::optional<QByteArray> &rawBytes = std::nullopt)
    {
        QSqlQuery q(db);
        if (rawBytes.has_value()) {
            q.prepare(QStringLiteral(
                "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value, "
                "raw_bytes, raw_encoding) "
                "VALUES (?, 'id3v2', 0, ?, 0, ?, ?, 'GBK');"));
            q.addBindValue(trackId);
            q.addBindValue(key);
            q.addBindValue(value);
            q.addBindValue(rawBytes.value());
        } else {
            q.prepare(QStringLiteral(
                "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
                "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
            q.addBindValue(trackId);
            q.addBindValue(key);
            q.addBindValue(value);
        }
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }
};

void populateTestLibrary(const QSqlDatabase &conn)
{
    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // 1. GBK mojibake album: 2 tracks in same folder
    const qint64 f1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/gbk_album/1.mp3"));
    const qint64 f2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/gbk_album/2.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"),
        QString::fromLatin1("\xC7\xE7\xCC\xEC"), QByteArray("\xC7\xE7\xCC\xEC"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"),
        QString::fromLatin1("\xD6\xDC\xBD\xDC\xC2\xD7"), QByteArray("\xD6\xDC\xBD\xDC\xC2\xD7"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"),
        QString::fromLatin1("\xD2\xB4\xBB\xDD"), QByteArray("\xD2\xB4\xBB\xDD"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"),
        QString::fromLatin1("\xB9\xEC\xBC\xA3"), QByteArray("\xB9\xEC\xBC\xA3"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"),
        QString::fromLatin1("\xD6\xDC\xBD\xDC\xC2\xD7"), QByteArray("\xD6\xDC\xBD\xDC\xC2\xD7"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"),
        QString::fromLatin1("\xD2\xB4\xBB\xDD"), QByteArray("\xD2\xB4\xBB\xDD"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    // 2. Multi-artist credit: A feat. B
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/feat/3.mp3"));
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Song 3"));
    TestDbHelper::insertRawTag(
        conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Artist A feat. Artist B"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ALBUM"), QStringLiteral("Album 1"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    // 3. Duplicate artists: Sora Amamiya / SORA AMAMIYA (same exact key)
    const qint64 f4
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/4.mp3"));
    const qint64 t4 = TestDbHelper::insertTrack(conn, f4);
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("TITLE"), QStringLiteral("Song 4"));
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("ARTIST"), QStringLiteral("Sora Amamiya"));
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("ALBUM"), QStringLiteral("Album 2"));
    TestDbHelper::updateTagsReadAt(conn, t4);

    const qint64 f5
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/5.mp3"));
    const qint64 t5 = TestDbHelper::insertTrack(conn, f5);
    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("TITLE"), QStringLiteral("Song 5"));
    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("ARTIST"), QStringLiteral("SORA AMAMIYA"));
    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("ALBUM"), QStringLiteral("Album 2"));
    TestDbHelper::updateTagsReadAt(conn, t5);

    EntityLinker linker(conn);
    Q_UNUSED(linker.linkTrack(t1));
    Q_UNUSED(linker.linkTrack(t2));
    Q_UNUSED(linker.linkTrack(t3));
    Q_UNUSED(linker.linkTrack(t4));
    Q_UNUSED(linker.linkTrack(t5));
}

class TstCleanupController : public QObject {
    Q_OBJECT

private slots:
    void healthCountsMatchLibrary();
    void runExecutesStepsInOrder();
    void autoAcceptAppliesThreshold();
    void albumInfoStepRequiresLlm();
    void duplicatesStepRuns();
    void automaticRunsRuleStepsOnly();
};

void TstCleanupController::healthCountsMatchLibrary()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_health.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();
    populateTestLibrary(conn);

    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    ManualClock clock(1000);
    AiConfig aiConfig(settings);
    MemorySecretStore secrets;
    QNetworkAccessManager network;
    LlmClient client(network);
    LlmCache cache(db, clock);
    UsageStore usage(db);
    PrivacyGuard privacy(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    LlmDebugLog debugLog(settings);
    LlmService llm(aiConfig, secrets, client, cache, usage, privacy, debugLog, clock);

    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<MojibakeJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistCreditJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistMergeJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<AlbumInfoJobHandler>(db, llm, prompts, clock));

    CleanupController cleanup(db, clock, jobs, prompts, aiConfig, settings);
    QCOMPARE(cleanup.isLlmConfigured(), false);
    QCOMPARE(cleanup.isHealthReady(), false);

    // 设置里新增服务后立即可用，无需重启
    linernotes::ai::ServiceProfile profile;
    profile.name = QStringLiteral("Test");
    profile.defaultModel = QStringLiteral("test-model");
    Q_UNUSED(aiConfig.saveService(profile));
    QCOMPARE(cleanup.isLlmConfigured(), true);

    cleanup.checkHealth();
    QTRY_VERIFY_WITH_TIMEOUT(cleanup.isHealthReady(), 5000);

    QVERIFY(cleanup.mojibakeGroups() >= 1);
    QVERIFY(cleanup.creditValues() >= 1);
    QVERIFY(cleanup.mergeClusters() >= 1);
    QCOMPARE(cleanup.albumInfoAlbums(), 3);
    QVERIFY(cleanup.albumInfoTokens() > 0);
}

void TstCleanupController::runExecutesStepsInOrder()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_run.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();
    populateTestLibrary(conn);

    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    ManualClock clock(1000);
    AiConfig aiConfig(settings);
    MemorySecretStore secrets;
    QNetworkAccessManager network;
    LlmClient client(network);
    LlmCache cache(db, clock);
    UsageStore usage(db);
    PrivacyGuard privacy(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    LlmDebugLog debugLog(settings);
    LlmService llm(aiConfig, secrets, client, cache, usage, privacy, debugLog, clock);

    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<MojibakeJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistCreditJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistMergeJobHandler>(db, llm, prompts, clock));

    const auto promptRes = prompts.load(QStringLiteral("cleanup/artist_credit"));
    QVERIFY(promptRes.ok());
    const int promptVersion = promptRes.value().version;

    // 没有 LLM 服务：库中每个艺人值都预存解析结果，署名解析步骤只走缓存
    ArtistCreditStore creditStore(db, clock);
    QHash<QString, ArtistCredit> savedCredits;
    QSqlQuery valuesQuery(conn);
    QVERIFY(valuesQuery.exec(QStringLiteral(
        "SELECT artist FROM effective_metadata WHERE artist != '' "
        "UNION SELECT album_artist FROM effective_metadata WHERE album_artist != ''")));
    while (valuesQuery.next()) {
        const QString value = valuesQuery.value(0).toString().trimmed();
        savedCredits.insert(value,
            ArtistCredit {
                .performers = { CreditPerformer { .name = value, .aka = { } } },
                .roles = { },
                .confidence = 0.9,
                .reason = QStringLiteral("Single artist"),
            });
    }
    savedCredits.insert(QStringLiteral("Artist A feat. Artist B"),
        ArtistCredit {
            .performers = {
                CreditPerformer { .name = QStringLiteral("Artist A"), .aka = { } },
                CreditPerformer { .name = QStringLiteral("Artist B"), .aka = { } },
            },
            .roles = { },
            .confidence = 0.95,
            .reason = QStringLiteral("Multi-artist split"),
        });
    QVERIFY(creditStore.save(savedCredits, QStringLiteral("test-model"), promptVersion).ok());

    CleanupController cleanup(db, clock, jobs, prompts, aiConfig, settings);

    cleanup.run(true, true, true, false, false, false, false);
    QCOMPARE(cleanup.isRunning(), true);

    QTRY_COMPARE_WITH_TIMEOUT(cleanup.isRunning(), false, 10000);

    CorrectionStore store(db, clock);
    const auto batchesRes = store.batches();
    QVERIFY(batchesRes.ok());
    const auto &batches = batchesRes.value();
    QCOMPARE(batches.size(), 3);

    // batches are ordered by created_at DESC
    QCOMPARE(batches.at(2).kind, CorrectionKind::Mojibake);
    QCOMPARE(batches.at(1).kind, CorrectionKind::ArtistCredit);
    QCOMPARE(batches.at(0).kind, CorrectionKind::ArtistMerge);

    QVERIFY(batches.at(2).pending > 0 || batches.at(2).accepted > 0);
    QVERIFY(batches.at(1).pending > 0 || batches.at(1).accepted > 0);
    QVERIFY(batches.at(0).pending > 0 || batches.at(0).accepted > 0);
}

void TstCleanupController::autoAcceptAppliesThreshold()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_auto_accept.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // Sora Amamiya / Amamiya Sora share no identity key (no parsed aka) -> not grouped
    const qint64 f1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/1.mp3"));
    const qint64 f2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/2.mp3"));
    const qint64 f3
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/3.mp3"));
    const qint64 f4
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/4.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    const qint64 t4 = TestDbHelper::insertTrack(conn, f4);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Track 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Sora Amamiya"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Track 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Sora Amamiya"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Track 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Sora Amamiya"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("TITLE"), QStringLiteral("Track 4"));
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("ARTIST"), QStringLiteral("Amamiya Sora"));
    TestDbHelper::updateTagsReadAt(conn, t4);

    // Cluster 2: Spelling difference cluster -> MYTH & ROID & MYTH&ROID -> 0.95
    const qint64 f5
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/5.mp3"));
    const qint64 f6
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/artists/6.mp3"));

    const qint64 t5 = TestDbHelper::insertTrack(conn, f5);
    const qint64 t6 = TestDbHelper::insertTrack(conn, f6);

    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("TITLE"), QStringLiteral("Track 5"));
    TestDbHelper::insertRawTag(conn, t5, QStringLiteral("ARTIST"), QStringLiteral("MYTH & ROID"));
    TestDbHelper::updateTagsReadAt(conn, t5);

    TestDbHelper::insertRawTag(conn, t6, QStringLiteral("TITLE"), QStringLiteral("Track 6"));
    TestDbHelper::insertRawTag(conn, t6, QStringLiteral("ARTIST"), QStringLiteral("MYTH&ROID"));
    TestDbHelper::updateTagsReadAt(conn, t6);

    EntityLinker linker(conn);
    Q_UNUSED(linker.linkTrack(t1));
    Q_UNUSED(linker.linkTrack(t2));
    Q_UNUSED(linker.linkTrack(t3));
    Q_UNUSED(linker.linkTrack(t4));
    Q_UNUSED(linker.linkTrack(t5));
    Q_UNUSED(linker.linkTrack(t6));

    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    ManualClock clock(1000);
    AiConfig aiConfig(settings);
    MemorySecretStore secrets;
    QNetworkAccessManager network;
    LlmClient client(network);
    LlmCache cache(db, clock);
    UsageStore usage(db);
    PrivacyGuard privacy(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    LlmDebugLog debugLog(settings);
    LlmService llm(aiConfig, secrets, client, cache, usage, privacy, debugLog, clock);

    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<MojibakeJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistCreditJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistMergeJobHandler>(db, llm, prompts, clock));

    CleanupController cleanup(db, clock, jobs, prompts, aiConfig, settings);
    cleanup.setAutoAcceptThreshold(0.9);

    cleanup.run(false, false, true, false, false, false, false);
    QTRY_COMPARE_WITH_TIMEOUT(cleanup.isRunning(), false, 10000);

    CorrectionStore store(db, clock);
    const auto batchesRes = store.batches();
    QVERIFY(batchesRes.ok());
    const auto &batches = batchesRes.value();
    QCOMPARE(batches.size(), 1);

    const qint64 batchId = batches.first().id;
    const auto correctionsRes = store.artistAliasCorrections(batchId);
    QVERIFY(correctionsRes.ok());
    const auto &corrections = correctionsRes.value();
    QCOMPARE(corrections.size(), 1);
    QCOMPARE(corrections.first().status, CorrectionStatus::Accepted);
}

void TstCleanupController::albumInfoStepRequiresLlm()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_album_info_step.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();
    populateTestLibrary(conn);

    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    ManualClock clock(1000);
    AiConfig aiConfig(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    MemorySecretStore secrets;
    QNetworkAccessManager network;
    LlmClient client(network);
    LlmCache cache(db, clock);
    UsageStore usage(db);
    PrivacyGuard privacy(settings);
    LlmDebugLog debugLog(settings);
    LlmService llm(aiConfig, secrets, client, cache, usage, privacy, debugLog, clock);

    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<AlbumInfoJobHandler>(db, llm, prompts, clock));

    const AlbumInfoSource source(db);
    const auto pendingRes = source.pendingAlbums();
    QVERIFY(pendingRes.ok());
    const auto &pendingAlbums = pendingRes.value();
    QCOMPARE(pendingAlbums.size(), 3);

    CleanupController cleanup(db, clock, jobs, prompts, aiConfig, settings);
    QCOMPARE(cleanup.isLlmConfigured(), false);

    // 配置 LLM 服务后立即可用
    linernotes::ai::ServiceProfile profile;
    profile.name = QStringLiteral("Test");
    profile.defaultModel = QStringLiteral("test-model");
    Q_UNUSED(aiConfig.saveService(profile));
    QCOMPARE(cleanup.isLlmConfigured(), true);

    cleanup.run(false, false, false, true, false, false, false);
    QCOMPARE(cleanup.isRunning(), true);
    QCOMPARE(cleanup.currentStep(), CleanupController::Step::AlbumInfo);

    QTRY_COMPARE_WITH_TIMEOUT(cleanup.isRunning(), false, 10000);
}

void TstCleanupController::duplicatesStepRuns()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_dup_step.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    const qint64 f1 = TestDbHelper::insertFileWithDetails(
        conn, rootId, QStringLiteral("/music/dup/1.flac"), QStringLiteral("hash1"), 200000);
    const qint64 f2 = TestDbHelper::insertFileWithDetails(
        conn, rootId, QStringLiteral("/music/dup/2.flac"), QStringLiteral("hash2"), 200500);

    QVERIFY(f1 > 0);
    QVERIFY(f2 > 0);

    const qint64 workId = 1;
    QVERIFY(TestDbHelper::insertWork(
        conn, workId, QStringLiteral("work_song_a"), QStringLiteral("Song A")));

    const qint64 t1 = TestDbHelper::insertTrackWithWork(conn, f1, workId);
    const qint64 t2 = TestDbHelper::insertTrackWithWork(conn, f2, workId);

    QVERIFY(t1 > 0);
    QVERIFY(t2 > 0);

    QVERIFY(TestDbHelper::insertTrackVersion(
        conn, t1, QStringLiteral("Song A"), QStringLiteral("studio")));
    QVERIFY(TestDbHelper::insertTrackVersion(
        conn, t2, QStringLiteral("Song A"), QStringLiteral("studio")));

    const ManualClock clock(1000);
    FingerprintStore fpStore(db, clock);

    QList<quint32> fpItems;
    fpItems.reserve(50);
    for (quint32 i = 0; i < 50; ++i) {
        fpItems.append(0x12345678U ^ (i * 0x9e3779b9U));
    }

    QVERIFY(fpStore.save(f1, 1, fpItems).ok());
    QVERIFY(fpStore.save(f2, 1, fpItems).ok());

    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    AiConfig aiConfig(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });

    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<FingerprintJobHandler>(db, clock));
    jobs.registerHandler(std::make_unique<DuplicateJobHandler>(db, clock));

    CleanupController cleanup(db, clock, jobs, prompts, aiConfig, settings);

    cleanup.checkHealth();
    QTRY_VERIFY_WITH_TIMEOUT(cleanup.isHealthReady(), 5000);
    QCOMPARE(cleanup.duplicateCandidates(), 2);
    QCOMPARE(cleanup.fingerprintPending(), 0);

    cleanup.run(false, false, false, false, false, false, true);
    QCOMPARE(cleanup.isRunning(), true);

    QTRY_COMPARE_WITH_TIMEOUT(cleanup.isRunning(), false, 10000);

    const DuplicateSource source(db, clock);
    const auto countRes = source.countGroups();
    QVERIFY(countRes.ok());
    QCOMPARE(countRes.value().value(DuplicateKind::SameRecording), 1);
}

void TstCleanupController::automaticRunsRuleStepsOnly()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_auto_cleanup.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // 1. Mojibake track (solvable by rules, GBK encoded tags)
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/gbk/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/gbk/2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"),
        QString::fromLatin1("\xC7\xE7\xCC\xEC"), QByteArray("\xC7\xE7\xCC\xEC"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"),
        QString::fromLatin1("\xD6\xDC\xBD\xDC\xC2\xD7"), QByteArray("\xD6\xDC\xBD\xDC\xC2\xD7"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"),
        QString::fromLatin1("\xD2\xB4\xBB\xDD"), QByteArray("\xD2\xB4\xBB\xDD"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"),
        QString::fromLatin1("\xB9\xEC\xBC\xA3"), QByteArray("\xB9\xEC\xBC\xA3"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"),
        QString::fromLatin1("\xD6\xDC\xBD\xDC\xC2\xD7"), QByteArray("\xD6\xDC\xBD\xDC\xC2\xD7"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"),
        QString::fromLatin1("\xD2\xB4\xBB\xDD"), QByteArray("\xD2\xB4\xBB\xDD"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    // 2. Cached artist credit (will produce correction)
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/feat/3.mp3"));
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Song 3"));
    TestDbHelper::insertRawTag(
        conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Artist A feat. Artist B"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ALBUM"), QStringLiteral("Album 1"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    // 3. Uncached artist credit value
    const qint64 f4 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/other/4.mp3"));
    const qint64 t4 = TestDbHelper::insertTrack(conn, f4);
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("TITLE"), QStringLiteral("Song 4"));
    TestDbHelper::insertRawTag(
        conn, t4, QStringLiteral("ARTIST"), QStringLiteral("Uncached Artist X & Artist Y"));
    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("ALBUM"), QStringLiteral("Album 2"));
    TestDbHelper::updateTagsReadAt(conn, t4);

    {
        // 限定作用域：EntityLinker 的预编译查询活着时主线程连接会一直持有读快照，
        // 工作线程提交后主线程再写就会 SQLITE_BUSY_SNAPSHOT（database is locked）
        EntityLinker linker(conn);
        Q_UNUSED(linker.linkTrack(t1));
        Q_UNUSED(linker.linkTrack(t2));
        Q_UNUSED(linker.linkTrack(t3));
        Q_UNUSED(linker.linkTrack(t4));
    }

    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    ManualClock clock(1000);
    AiConfig aiConfig(settings); // Unconfigured LLM
    MemorySecretStore secrets;
    QNetworkAccessManager network;
    LlmClient client(network);
    LlmCache cache(db, clock);
    UsageStore usage(db);
    PrivacyGuard privacy(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    LlmDebugLog debugLog(settings);
    LlmService llm(aiConfig, secrets, client, cache, usage, privacy, debugLog, clock);

    const auto promptRes = prompts.load(QStringLiteral("cleanup/artist_credit"));
    QVERIFY(promptRes.ok());
    const int promptVersion = promptRes.value().version;

    ArtistCreditStore creditStore(db, clock);
    QHash<QString, ArtistCredit> savedCredits;
    savedCredits.insert(QStringLiteral("Artist A feat. Artist B"),
        ArtistCredit {
            .performers = {
                CreditPerformer { .name = QStringLiteral("Artist A"), .aka = { } },
                CreditPerformer { .name = QStringLiteral("Artist B"), .aka = { } },
            },
            .roles = { },
            .confidence = 0.95,
            .reason = QStringLiteral("Multi-artist split"),
        });
    QVERIFY(creditStore.save(savedCredits, QStringLiteral("test-model"), promptVersion).ok());

    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<MojibakeJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<ArtistCreditJobHandler>(db, llm, prompts, clock));
    jobs.registerHandler(std::make_unique<VersionLinkJobHandler>(db, prompts, clock));

    CleanupController cleanup(db, clock, jobs, prompts, aiConfig, settings);
    QCOMPARE(cleanup.isLlmConfigured(), false);

    cleanup.runAutomatic();
    QCOMPARE(cleanup.isRunning(), true);
    QCOMPARE(cleanup.isAutomatic(), true);

    QTRY_COMPARE_WITH_TIMEOUT(cleanup.isRunning(), false, 10000);
    QCOMPARE(cleanup.isAutomatic(), false);
    QCOMPARE(cleanup.stepFailed(), 0);

    CorrectionStore store(db, clock);
    const auto batchesRes = store.batches();
    QVERIFY(batchesRes.ok());
    const auto &batches = batchesRes.value();

    bool hasMojibakeBatch = false;
    bool hasCreditBatch = false;
    for (const auto &b : batches) {
        if (b.kind == CorrectionKind::Mojibake) {
            hasMojibakeBatch = true;
            QVERIFY(b.pending > 0 || b.accepted > 0);
            QCOMPARE(b.description, QStringLiteral("Automatic: Fix garbled tags"));
        } else if (b.kind == CorrectionKind::ArtistCredit) {
            hasCreditBatch = true;
            QVERIFY(b.pending > 0 || b.accepted > 0);
            QCOMPARE(b.description, QStringLiteral("Automatic: Normalize artist credits"));
            const auto corrsRes = store.corrections(b.id);
            QVERIFY(corrsRes.ok());
            for (const auto &corr : corrsRes.value()) {
                QCOMPARE(corr.oldValue, QStringLiteral("Artist A feat. Artist B"));
            }
        }
    }
    QVERIFY(hasMojibakeBatch);
    QVERIFY(hasCreditBatch);

    QSqlQuery uncachedCheckQ(conn);
    QVERIFY(uncachedCheckQ.exec(QStringLiteral("SELECT COUNT(*) FROM artist_credits WHERE "
                                               "value = 'Uncached Artist X & Artist Y';")));
    QVERIFY(uncachedCheckQ.next());
    QCOMPARE(uncachedCheckQ.value(0).toInt(), 0);

    QSqlQuery tvQ(conn);
    QVERIFY(tvQ.exec(QStringLiteral("SELECT COUNT(*) FROM track_versions;")));
    QVERIFY(tvQ.next());
    QVERIFY(tvQ.value(0).toInt() > 0);
}

} // namespace

QTEST_MAIN(TstCleanupController)
#include "tst_CleanupController.moc"
