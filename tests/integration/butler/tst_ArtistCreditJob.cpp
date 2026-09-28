// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
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
#include <butler/ArtistCredit.h>
#include <butler/ArtistCreditJobHandler.h>
#include <butler/ArtistCreditSource.h>
#include <butler/ArtistCreditStore.h>
#include <common/ManualClock.h>
#include <core/Settings.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>

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
using linernotes::butler::ArtistCredit;
using linernotes::butler::ArtistCreditJobHandler;
using linernotes::butler::ArtistCreditSource;
using linernotes::butler::ArtistCreditStore;
using linernotes::butler::CreditPerformer;
using linernotes::core::Settings;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::library::TagField;
using linernotes::test::ManualClock;

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

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertRawTag(
        const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(key);
        q.addBindValue(value);
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

class TstArtistCreditJob : public QObject {
    Q_OBJECT

private slots:
    void cachedItemProducesCorrection();
    void oldPromptVersionIsUnparsed();
    void rejectedCorrectionExcludedFromTargets();
};

void TstArtistCreditJob::cachedItemProducesCorrection()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_credit_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1"));
    TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ARTIST"), QStringLiteral("高垣彩陽（as 雪音クリス）"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Jay Chou"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Song 3"));
    TestDbHelper::insertRawTag(
        conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Uncached Artist"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());
    QVERIFY(linker.linkTrack(t3).ok());

    ManualClock clock(1000);
    ArtistCreditStore creditStore(db, clock);

    // Save credit for "高垣彩陽（as 雪音クリス）" with normalized != original
    const ArtistCredit roleCredit {
        .performers = { CreditPerformer { .name = QStringLiteral("高垣彩陽"), .aka = { } } },
        .roles = { QStringLiteral("雪音クリス") },
        .confidence = 0.95,
        .reason = QStringLiteral("Remove character role"),
    };
    QVERIFY(creditStore
            .save({ { QStringLiteral("高垣彩陽（as 雪音クリス）"), roleCredit } },
                QStringLiteral("test-model"), 1)
            .ok());

    // Save credit for "Jay Chou" with normalized == original
    const ArtistCredit normalCredit {
        .performers = { CreditPerformer { .name = QStringLiteral("Jay Chou"), .aka = { } } },
        .roles = { },
        .confidence = 1.0,
        .reason = QStringLiteral("Single artist"),
    };
    QVERIFY(creditStore
            .save({ { QStringLiteral("Jay Chou"), normalCredit } }, QStringLiteral("test-model"), 1)
            .ok());

    const ArtistCreditSource source(db);
    const auto itemsRes = source.findItems(1);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();

    // Check items:
    // Should have 1 cached item with "高垣彩陽（as 雪音クリス）", and 1 parse item with "Uncached
    // Artist"
    QString cachedKey;
    QString parseKey;
    for (const auto &key : items) {
        const auto doc = QJsonDocument::fromJson(key.toUtf8());
        QVERIFY(doc.isObject());
        const QString type = doc.object().value(QStringLiteral("type")).toString();
        if (type == QLatin1StringView("cached")) {
            cachedKey = key;
        } else if (type == QLatin1StringView("parse")) {
            parseKey = key;
        }
    }

    QVERIFY(!cachedKey.isEmpty());
    QVERIFY(!parseKey.isEmpty());

    const auto cachedDoc = QJsonDocument::fromJson(cachedKey.toUtf8());
    const auto cachedVals = cachedDoc.object().value(QStringLiteral("values")).toArray();
    QCOMPARE(cachedVals.size(), 1);
    QCOMPARE(cachedVals.at(0).toString(), QStringLiteral("高垣彩陽（as 雪音クリス）"));

    const auto parseDoc = QJsonDocument::fromJson(parseKey.toUtf8());
    const auto parseVals = parseDoc.object().value(QStringLiteral("values")).toArray();
    QCOMPARE(parseVals.size(), 1);
    QCOMPARE(parseVals.at(0).toString(), QStringLiteral("Uncached Artist"));

    // Set up JobHandler to process cachedKey
    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
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

    ArtistCreditJobHandler handler(db, llm, prompts, clock);
    CorrectionStore corrStore(db, clock);
    const auto batchIdRes
        = corrStore.createBatch(CorrectionKind::ArtistCredit, QStringLiteral("Credit batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    QJsonObject params;
    params.insert(QStringLiteral("batchId"), batchId);

    bool callbackInvoked = false;
    linernotes::core::Result<void> processResult;
    auto task = handler.process(cachedKey, params, [&](const linernotes::core::Result<void> &res) {
        callbackInvoked = true;
        processResult = res;
    });

    QVERIFY(task == nullptr); // cached processing is synchronous
    QVERIFY(callbackInvoked);
    QVERIFY(processResult.ok());

    const auto corrsRes = corrStore.corrections(batchId);
    QVERIFY(corrsRes.ok());
    const auto &corrs = corrsRes.value();
    QCOMPARE(corrs.size(), 1);
    QCOMPARE(corrs.first().trackId, t1);
    QCOMPARE(corrs.first().field, TagField::Artist);
    QCOMPARE(corrs.first().oldValue, QStringLiteral("高垣彩陽（as 雪音クリス）"));
    QCOMPARE(corrs.first().newValue, QStringLiteral("高垣彩陽"));
}

void TstArtistCreditJob::oldPromptVersionIsUnparsed()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_old_version.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);

    TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist With Old Cache"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());

    ManualClock clock(1000);
    ArtistCreditStore creditStore(db, clock);

    const ArtistCredit credit {
        .performers
        = { CreditPerformer { .name = QStringLiteral("Artist With Old Cache"), .aka = { } } },
        .roles = { },
        .confidence = 0.9,
        .reason = QStringLiteral("Old cache"),
    };
    // Save with promptVersion = 0
    QVERIFY(creditStore
            .save({ { QStringLiteral("Artist With Old Cache"), credit } },
                QStringLiteral("test-model"), 0)
            .ok());

    const ArtistCreditSource source(db);
    const auto itemsRes = source.findItems(1);
    QVERIFY(itemsRes.ok());

    // Because promptVersion in cache (0) < current promptVersion (1), it must be in "parse"
    QCOMPARE(itemsRes.value().size(), 1);
    const auto doc = QJsonDocument::fromJson(itemsRes.value().first().toUtf8());
    QCOMPARE(doc.object().value(QStringLiteral("type")).toString(), QStringLiteral("parse"));
}

void TstArtistCreditJob::rejectedCorrectionExcludedFromTargets()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_rejected.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);

    TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist To Reject"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());

    const ArtistCreditSource source(db);
    auto initialTargets = source.targetsFor(QStringLiteral("Artist To Reject")).value();
    QCOMPARE(initialTargets.size(), 1);
    QCOMPARE(initialTargets.first().trackId, t1);

    // Create an artist_credit batch and add a proposal for this track
    ManualClock clock(1000);
    CorrectionStore store(db, clock);
    const auto batchIdRes
        = store.createBatch(CorrectionKind::ArtistCredit, QStringLiteral("Credit batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    const CorrectionProposal proposal {
        .trackId = t1,
        .field = TagField::Artist,
        .oldValue = QStringLiteral("Artist To Reject"),
        .newValue = QStringLiteral("Normalized Artist"),
        .source = linernotes::library::CorrectionSource::Llm,
        .confidence = 0.9,
        .reason = QStringLiteral("Parsed credit"),
    };
    QVERIFY(store.addProposals(batchId, { proposal }).ok());

    const auto corrs = store.corrections(batchId).value();
    QCOMPARE(corrs.size(), 1);
    const qint64 corrId = corrs.first().id;

    // Reject the correction via public API
    QVERIFY(store.reject({ corrId }).ok());

    // targetsFor should now exclude this track field
    auto updatedTargets = source.targetsFor(QStringLiteral("Artist To Reject")).value();
    QCOMPARE(updatedTargets.size(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistCreditJob)

#include "tst_ArtistCreditJob.moc"
