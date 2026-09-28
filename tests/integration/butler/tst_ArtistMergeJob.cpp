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
#include <butler/ArtistCreditStore.h>
#include <butler/ArtistGroup.h>
#include <butler/ArtistMerge.h>
#include <butler/ArtistMergeJobHandler.h>
#include <butler/ArtistMergeSource.h>
#include <butler/MusicBrainz.h>
#include <butler/MusicBrainzClient.h>
#include <common/ManualClock.h>
#include <core/Result.h>
#include <core/Settings.h>
#include <library/ArtistAliasCorrections.h>
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
using linernotes::butler::ArtistCreditStore;
using linernotes::butler::ArtistEntry;
using linernotes::butler::ArtistGroup;
using linernotes::butler::ArtistMergeJobHandler;
using linernotes::butler::ArtistMergeSource;
using linernotes::butler::artistSearchUrl;
using linernotes::butler::CreditPerformer;
using linernotes::butler::groupProposals;
using linernotes::butler::MusicBrainzClient;
using linernotes::core::Result;
using linernotes::core::Settings;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionStatus;
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

    static qint64 getArtistId(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?;"));
        q.addBindValue(name);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    }

    static QList<qint64> getTrackArtistIds(const QSqlDatabase &db, qint64 trackId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT artist_id FROM track_artists WHERE track_id = ? ORDER BY position;"));
        q.addBindValue(trackId);
        QList<qint64> ids;
        if (q.exec()) {
            while (q.next()) {
                ids.append(q.value(0).toLongLong());
            }
        }
        return ids;
    }

    static bool insertMbCache(const QSqlDatabase &db, const QString &name, const QString &bodyJson,
        qint64 fetchedAt = 1000)
    {
        const QString url = artistSearchUrl(name).toString();
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO mb_cache (url, body, fetched_at) VALUES (?, ?, ?);"));
        q.addBindValue(url);
        q.addBindValue(bodyJson);
        q.addBindValue(fetchedAt);
        return q.exec();
    }
};

class TstArtistMergeJob : public QObject {
    Q_OBJECT

private slots:
    void groupMergeAndAutoAccept();
    void findItemsWithAkaProducesConfirmItem();
    void loadArtistsAndFindItemsExcludePendingArtistCredit();
    void mbCacheEnablesConfirmGrouping();
    void findLookupItemsExcludesCachedArtists();
    void mbAliasItemCreatesLocalizedAliasCorrections();
};

void TstArtistMergeJob::groupMergeAndAutoAccept()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_merge_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);

    // Jay Chou: 2 tracks (t1, t2)
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Jay Chou"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Jay Chou"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    // jay chou: 1 track (t3)
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Song 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ARTIST"), QStringLiteral("jay chou"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());
    QVERIFY(linker.linkTrack(t3).ok());

    const qint64 jayChouId = TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou"));
    const qint64 lowerJayChouId = TestDbHelper::getArtistId(conn, QStringLiteral("jay chou"));

    QVERIFY(jayChouId > 0);
    QVERIFY(lowerJayChouId > 0);

    ManualClock clock(1000);

    // Find items without MusicBrainz
    const ArtistMergeSource source(db, clock);
    const auto itemsRes = source.findItems(false);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();

    int groupItemCount = 0;
    QJsonObject groupObj;

    for (const auto &itemStr : items) {
        const auto doc = QJsonDocument::fromJson(itemStr.toUtf8());
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() == QLatin1StringView("group")) {
            ++groupItemCount;
            groupObj = obj;
        }
    }

    // 1 group item found (Jay Chou / jay chou)
    QCOMPARE(groupItemCount, 1);

    const auto idsArr = groupObj.value(QStringLiteral("ids")).toArray();
    QCOMPARE(idsArr.size(), 2);
    const QList<qint64> groupIds = { idsArr.at(0).toInteger(), idsArr.at(1).toInteger() };
    QVERIFY(groupIds.contains(jayChouId));
    QVERIFY(groupIds.contains(lowerJayChouId));

    const ArtistGroup group {
        .members = {
            ArtistEntry { .artistId = jayChouId, .name = QStringLiteral("Jay Chou"), .trackCount = 2 },
            ArtistEntry {
                .artistId = lowerJayChouId, .name = QStringLiteral("jay chou"), .trackCount = 1
            },
        },
        .exactOnly = true,
    };

    const auto proposals = groupProposals(group);
    QCOMPARE(proposals.size(), 1);

    CorrectionStore store(db, clock);

    const auto batchIdRes
        = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("Auto-merge exact group"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    // Auto-accept proposals with threshold 0.9 (since confidence is 0.95)
    const auto addRes = store.addArtistAliasProposals(batchId, proposals, 0.9);
    QVERIFY(addRes.ok());

    // Track 3 is now linked to Jay Chou
    const auto t3Artists = TestDbHelper::getTrackArtistIds(conn, t3);
    QCOMPARE(t3Artists.size(), 1);
    QCOMPARE(t3Artists.first(), jayChouId);

    // jay chou entity is merged and no longer exists
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("jay chou")), -1);
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou")), jayChouId);
}

void TstArtistMergeJob::findItemsWithAkaProducesConfirmItem()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_aka_confirm.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Good Day"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("아이유"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Celebrity"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("IU"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    const qint64 iuKoreanId = TestDbHelper::getArtistId(conn, QStringLiteral("아이유"));
    const qint64 iuEnglishId = TestDbHelper::getArtistId(conn, QStringLiteral("IU"));
    QVERIFY(iuKoreanId > 0);
    QVERIFY(iuEnglishId > 0);

    // Pre-populate artist_credits with aka "IU" for "아이유"
    ManualClock clock(1000);
    ArtistCreditStore creditStore(db, clock);

    ArtistCredit credit;
    credit.performers.append(CreditPerformer {
        .name = QStringLiteral("아이유"),
        .aka = { QStringLiteral("IU") },
    });
    credit.confidence = 0.95;
    credit.reason = QStringLiteral("Known artist alias");

    QHash<QString, ArtistCredit> credits;
    credits.insert(QStringLiteral("아이유"), credit);
    QVERIFY(creditStore.save(credits, QStringLiteral("test-model"), 1).ok());

    // findItems should produce a "confirm" item grouping both entities
    const ArtistMergeSource source(db, clock);
    const auto itemsRes = source.findItems(false);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();

    int confirmCount = 0;
    QJsonObject confirmObj;

    for (const auto &itemStr : items) {
        const auto doc = QJsonDocument::fromJson(itemStr.toUtf8());
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() == QLatin1StringView("confirm")) {
            ++confirmCount;
            confirmObj = obj;
        }
    }

    QCOMPARE(confirmCount, 1);
    const auto groupsArr = confirmObj.value(QStringLiteral("groups")).toArray();
    QCOMPARE(groupsArr.size(), 1);
    const auto firstGroupArr = groupsArr.at(0).toArray();
    QCOMPARE(firstGroupArr.size(), 2);

    const QList<qint64> memberIds = {
        firstGroupArr.at(0).toInteger(),
        firstGroupArr.at(1).toInteger(),
    };
    QVERIFY(memberIds.contains(iuKoreanId));
    QVERIFY(memberIds.contains(iuEnglishId));
}

void TstArtistMergeJob::loadArtistsAndFindItemsExcludePendingArtistCredit()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_exclude_pending.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);

    TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ARTIST"), QStringLiteral("40mP feat. 初音ミク"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());

    ManualClock clock(1000);
    const ArtistMergeSource source(db, clock);
    auto initialArtists = source.loadArtists().value();
    QCOMPARE(initialArtists.size(), 1);
    QCOMPARE(initialArtists.first().name, QStringLiteral("40mP feat. 初音ミク"));

    // Add a pending artist_credit correction with old_value = "40mP feat. 初音ミク"
    CorrectionStore store(db, clock);
    const auto batchIdRes
        = store.createBatch(CorrectionKind::ArtistCredit, QStringLiteral("Credit batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal proposal;
    proposal.trackId = t1;
    proposal.field = TagField::Artist;
    proposal.oldValue = QStringLiteral("40mP feat. 初音ミク");
    proposal.newValue = QStringLiteral("40mP / 初音ミク");
    proposal.confidence = 0.9;
    proposal.reason = QStringLiteral("Artist credit resolution");

    QVERIFY(store.addProposals(batchId, { proposal }).ok());

    // loadArtists() and findItems() must exclude this artist
    auto filteredArtists = source.loadArtists().value();
    QCOMPARE(filteredArtists.size(), 0);

    auto items = source.findItems(false).value();
    QVERIFY(items.isEmpty());
}

void TstArtistMergeJob::mbCacheEnablesConfirmGrouping()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_mb_confirm.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Good Day"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("아이유"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Celebrity"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("IU"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    const qint64 iuKoreanId = TestDbHelper::getArtistId(conn, QStringLiteral("아이유"));
    const qint64 iuEnglishId = TestDbHelper::getArtistId(conn, QStringLiteral("IU"));
    QVERIFY(iuKoreanId > 0);
    QVERIFY(iuEnglishId > 0);

    // Pre-populate mb_cache for "아이유" with alias "IU"
    const QString mbJson = QStringLiteral(R"({
        "artists": [
            {
                "id": "mbid-iu",
                "name": "아이유",
                "score": 100,
                "type": "Person",
                "country": "KR",
                "disambiguation": "",
                "aliases": [
                    {
                        "name": "IU",
                        "sort-name": "IU",
                        "locale": "en",
                        "primary": true
                    }
                ]
            }
        ]
    })");
    QVERIFY(TestDbHelper::insertMbCache(conn, QStringLiteral("아이유"), mbJson, 1000));

    ManualClock clock(1000);
    const ArtistMergeSource source(db, clock);

    // findItems(false) should NOT produce a confirm item (no alias link without MB)
    const auto noMbRes = source.findItems(false);
    QVERIFY(noMbRes.ok());
    for (const auto &itemStr : noMbRes.value()) {
        const auto doc = QJsonDocument::fromJson(itemStr.toUtf8());
        QVERIFY(doc.isObject());
        QVERIFY(
            doc.object().value(QStringLiteral("type")).toString() != QLatin1StringView("confirm"));
    }

    // findItems(true) SHOULD produce a confirm item grouping 아이유 and IU
    const auto mbRes = source.findItems(true);
    QVERIFY(mbRes.ok());
    int confirmCount = 0;
    QJsonObject confirmObj;

    for (const auto &itemStr : mbRes.value()) {
        const auto doc = QJsonDocument::fromJson(itemStr.toUtf8());
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() == QLatin1StringView("confirm")) {
            ++confirmCount;
            confirmObj = obj;
        }
    }

    QCOMPARE(confirmCount, 1);
    const auto groupsArr = confirmObj.value(QStringLiteral("groups")).toArray();
    QCOMPARE(groupsArr.size(), 1);
    const auto firstGroupArr = groupsArr.at(0).toArray();
    QCOMPARE(firstGroupArr.size(), 2);

    const QList<qint64> memberIds = {
        firstGroupArr.at(0).toInteger(),
        firstGroupArr.at(1).toInteger(),
    };
    QVERIFY(memberIds.contains(iuKoreanId));
    QVERIFY(memberIds.contains(iuEnglishId));
}

void TstArtistMergeJob::findLookupItemsExcludesCachedArtists()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_lookup_cache.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist1"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Artist2"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    // Pre-populate mb_cache only for "Artist1"
    const QString mbJson = QStringLiteral(R"({"artists":[]})");
    QVERIFY(TestDbHelper::insertMbCache(conn, QStringLiteral("Artist1"), mbJson, 1000));

    ManualClock clock(1000);
    const ArtistMergeSource source(db, clock);
    const auto itemsRes = source.findLookupItems();
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();

    // Should only have Artist2
    QCOMPARE(items.size(), 1);
    const auto doc = QJsonDocument::fromJson(items.first().toUtf8());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("name")).toString(), QStringLiteral("Artist2"));
}

void TstArtistMergeJob::mbAliasItemCreatesLocalizedAliasCorrections()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_mb_alias.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Sparkle"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("RADWIMPS"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());

    const qint64 radwimpsId = TestDbHelper::getArtistId(conn, QStringLiteral("RADWIMPS"));
    QVERIFY(radwimpsId > 0);

    const QString mbJson = QStringLiteral(R"({
        "artists": [
            {
                "id": "mbid-radwimps",
                "name": "RADWIMPS",
                "score": 100,
                "type": "Group",
                "country": "JP",
                "aliases": [
                    {
                        "name": "ラッドウィンプス",
                        "sort-name": "ラッドウィンプス",
                        "locale": "ja",
                        "primary": true
                    }
                ]
            }
        ]
    })");
    QVERIFY(TestDbHelper::insertMbCache(conn, QStringLiteral("RADWIMPS"), mbJson, 1000));

    ManualClock clock(1000);
    const ArtistMergeSource source(db, clock);
    const auto itemsRes = source.findItems(true);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();

    // Must contain mb_alias item for RADWIMPS
    int mbAliasCount = 0;
    QString mbAliasItem;
    for (const auto &itemStr : items) {
        const auto doc = QJsonDocument::fromJson(itemStr.toUtf8());
        if (doc.isObject()
            && doc.object().value(QStringLiteral("type")).toString()
                == QLatin1StringView("mb_alias")) {
            ++mbAliasCount;
            mbAliasItem = itemStr;
        }
    }
    QCOMPARE(mbAliasCount, 1);

    // Process the mb_alias item using ArtistMergeJobHandler
    const QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    AiConfig aiConfig(settings);
    MemorySecretStore secretStore;
    QNetworkAccessManager network;
    LlmClient llmClient(network);
    LlmCache llmCache(db, clock);
    UsageStore usageStore(db);
    PrivacyGuard privacyGuard(settings);
    PromptLibrary prompts({ QStringLiteral(":/prompts") });
    LlmDebugLog debugLog(settings);
    LlmService llmService(
        aiConfig, secretStore, llmClient, llmCache, usageStore, privacyGuard, debugLog, clock);
    MusicBrainzClient mbClient(network, db, clock);
    ArtistMergeJobHandler handler(db, llmService, prompts, mbClient, clock);

    CorrectionStore store(db, clock);
    const auto batchIdRes
        = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("MB alias batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    QJsonObject params;
    params.insert(QStringLiteral("batchId"), batchId);
    params.insert(QStringLiteral("autoAccept"), 0.8);

    bool calledDone = false;
    Result<void> doneResult;
    auto task = handler.process(mbAliasItem, params, [&](const Result<void> &res) {
        calledDone = true;
        doneResult = res;
    });

    QTRY_VERIFY(calledDone);
    QVERIFY(doneResult.ok());
    QVERIFY(task == nullptr);

    // Verify alias correction was created and auto-accepted
    const auto aliasRowsRes = store.artistAliasCorrections(batchId);
    QVERIFY(aliasRowsRes.ok());
    const auto &aliasRows = aliasRowsRes.value();
    QCOMPARE(aliasRows.size(), 1);
    QCOMPARE(aliasRows.first().artistId, radwimpsId);
    QCOMPARE(aliasRows.first().alias, QStringLiteral("ラッドウィンプス"));
    QCOMPARE(aliasRows.first().locale, std::optional<QString>(QStringLiteral("ja")));
    QCOMPARE(aliasRows.first().status, CorrectionStatus::Accepted);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistMergeJob)

#include "tst_ArtistMergeJob.moc"
