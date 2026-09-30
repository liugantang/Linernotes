// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QByteArray>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <ai/JobQueue.h>
#include <butler/MbMatchJobHandler.h>
#include <butler/MbMatchSource.h>
#include <butler/MusicBrainz.h>
#include <butler/MusicBrainzClient.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>

namespace {

using linernotes::ai::JobInfo;
using linernotes::ai::JobQueue;
using linernotes::ai::JobState;
using linernotes::butler::MbMatchJobHandler;
using linernotes::butler::MbMatchSource;
using linernotes::butler::MusicBrainzClient;
using linernotes::butler::releaseSearchUrl;
using linernotes::butler::releaseUrl;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::TagField;
using linernotes::test::fixturePath;
using linernotes::test::ManualClock;

QByteArray readFixture(const QString &relativePath)
{
    const QString fullPath = fixturePath(relativePath);
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    return file.readAll();
}

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(
        const QSqlDatabase &db, qint64 rootId, const QString &path, qint64 durationMs = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1048576, 2000, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertAlbum(
        const QSqlDatabase &db, const QString &title, const QString &albumArtist)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO albums (grouping_key, title, album_artist, created_at) "
                           "VALUES (?, ?, ?, 1000);"));
        q.addBindValue(QString(QStringLiteral("key:") + title + QStringLiteral(":") + albumArtist));
        q.addBindValue(title);
        q.addBindValue(albumArtist);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId, qint64 albumId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, ?, 0, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(albumId);
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

    static bool writeCache(
        const QSqlDatabase &db, const QUrl &url, const QByteArray &body, qint64 nowMs)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO mb_cache (url, body, fetched_at) VALUES (?, ?, ?);"));
        q.addBindValue(url.toString());
        q.addBindValue(QString::fromUtf8(body));
        q.addBindValue(nowMs);
        return q.exec();
    }
};

class TstMbMatchJob : public QObject {
    Q_OBJECT

private slots:
    void testFlowerflowerFullMatchJob();
    void testUnrelatedAlbumNoMatchJob();
};

void TstMbMatchJob::testFlowerflowerFullMatchJob()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_mb_match_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const ManualClock clock(1000000);

    // Read fixtures
    const QByteArray searchJson
        = readFixture(QStringLiteral("musicbrainz/release_search_flowerflower.json"));
    QVERIFY(!searchJson.isEmpty());
    const QByteArray releaseJson
        = readFixture(QStringLiteral("musicbrainz/release_flowerflower_takaramono.json"));
    QVERIFY(!releaseJson.isEmpty());

    // Pre-fill mb_cache
    const QUrl searchUrl
        = releaseSearchUrl(QStringLiteral("宝物"), QStringLiteral("FLOWER FLOWER"), 10);
    QVERIFY(TestDbHelper::writeCache(conn, searchUrl, searchJson, clock.nowMs()));

    const QUrl releaseDetailUrl
        = releaseUrl(QStringLiteral("11ade0fa-6831-4169-830b-baf5b827878e"));
    QVERIFY(TestDbHelper::writeCache(conn, releaseDetailUrl, releaseJson, clock.nowMs()));

    // Other candidate detail
    QByteArray altReleaseJson = releaseJson;
    altReleaseJson.replace(
        "\"11ade0fa-6831-4169-830b-baf5b827878e\"", "\"967e16f4-a5e4-4b76-b904-f9fd72aaff4a\"");
    const QUrl altReleaseUrl = releaseUrl(QStringLiteral("967e16f4-a5e4-4b76-b904-f9fd72aaff4a"));
    QVERIFY(TestDbHelper::writeCache(conn, altReleaseUrl, altReleaseJson, clock.nowMs()));

    // Insert album & tracks into DB
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 albumId
        = TestDbHelper::insertAlbum(conn, QStringLiteral("宝物"), QStringLiteral("FLOWER FLOWER"));

    const QStringList titles = {
        QStringLiteral("宝物"),
        QStringLiteral("炎"),
        QStringLiteral("とうめいなうた (Live at Shibuya WWW)"),
        QStringLiteral("宝物 (Live at Shibuya WWW)"),
        QStringLiteral("スタートライン (Live at Shibuya WWW)"),
    };
    const QList<qint64> durations = { 292000, 309000, 328000, 298000, 340000 };

    for (int i = 0; i < 5; ++i) {
        const QString path = QStringLiteral("/music/flowerflower_%1.flac").arg(i + 1);
        const qint64 fileId = TestDbHelper::insertFile(conn, rootId, path, durations.at(i));
        const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, albumId);
        TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("TITLE"), titles.at(i));
        TestDbHelper::insertRawTag(
            conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("FLOWER FLOWER"));
        TestDbHelper::updateTagsReadAt(conn, trackId, 1000);
    }

    // Verify pending albums
    const MbMatchSource matchSource(db);
    const auto pendingBefore = matchSource.pendingAlbums();
    QVERIFY(pendingBefore.ok());
    QVERIFY(pendingBefore.value().contains(albumId));

    // Setup client and JobQueue
    QNetworkAccessManager network;
    MusicBrainzClient mbClient(network, db, clock);

    CorrectionStore corrStore(db, clock);
    const auto batchIdRes
        = corrStore.createBatch(CorrectionKind::MbMatch, QStringLiteral("MB Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    JobQueue queue(db, clock);
    queue.registerHandler(std::make_unique<MbMatchJobHandler>(db, mbClient, clock));

    QJsonObject params;
    params.insert(QStringLiteral("batchId"), batchId);

    const auto enqueueRes = queue.enqueue(QStringLiteral("butler.mb_match"),
        QStringLiteral("Match 宝物"), { QString::number(albumId) }, params);
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    // Wait for job completion
    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state == JobState::Completed, 10000);

    const auto jobOpt = queue.job(jobId);
    QVERIFY(jobOpt.has_value());
    if (!jobOpt.has_value()) {
        return;
    }
    QCOMPARE(jobOpt->state, JobState::Completed);
    QCOMPARE(jobOpt->done, 1);
    QCOMPARE(jobOpt->failed, 0);

    // Verify mb_album_matches
    QSqlQuery qAlbum(conn);
    qAlbum.prepare(QStringLiteral(
        "SELECT status, release_id, score FROM mb_album_matches WHERE album_id = ?;"));
    qAlbum.addBindValue(albumId);
    QVERIFY(qAlbum.exec() && qAlbum.next());
    QCOMPARE(qAlbum.value(0).toString(), QStringLiteral("matched"));
    QCOMPARE(qAlbum.value(1).toString(), QStringLiteral("11ade0fa-6831-4169-830b-baf5b827878e"));
    QVERIFY(qAlbum.value(2).toDouble() > 0.9);

    // Verify mb_track_matches has 5 rows
    QSqlQuery qTrack(conn);
    QVERIFY(qTrack.exec(QStringLiteral("SELECT COUNT(*) FROM mb_track_matches;")));
    QVERIFY(qTrack.next());
    QCOMPARE(qTrack.value(0).toInt(), 5);

    // Verify proposals in batch contain year proposals
    const auto corrsRes = corrStore.corrections(batchId);
    QVERIFY(corrsRes.ok());
    const auto &corrs = corrsRes.value();
    QVERIFY(!corrs.isEmpty());

    bool foundYearProposal = false;
    for (const auto &row : corrs) {
        if (row.field == TagField::Year) {
            foundYearProposal = true;
            QCOMPARE(row.newValue, QStringLiteral("2016"));
        }
    }
    QVERIFY(foundYearProposal);

    // Verify pending albums is now empty
    const auto pendingAfter = matchSource.pendingAlbums();
    QVERIFY(pendingAfter.ok());
    QVERIFY(!pendingAfter.value().contains(albumId));
}

void TstMbMatchJob::testUnrelatedAlbumNoMatchJob()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_mb_unrelated_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const ManualClock clock(1000000);

    // Pre-fill empty search result in mb_cache
    const QByteArray emptySearchJson = "{\"created\":\"2026-09-30T00:00:00.000Z\",\"count\":0,"
                                       "\"offset\":0,\"releases\":[]}";
    const QUrl searchUrl
        = releaseSearchUrl(QStringLiteral("UnrelatedAlbum"), QStringLiteral("UnrelatedArtist"), 10);
    QVERIFY(TestDbHelper::writeCache(conn, searchUrl, emptySearchJson, clock.nowMs()));

    // Insert album & track
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 albumId = TestDbHelper::insertAlbum(
        conn, QStringLiteral("UnrelatedAlbum"), QStringLiteral("UnrelatedArtist"));
    const qint64 fileId
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/unrelated.mp3"), 180000);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, albumId);
    TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("UnrelatedSong"));
    TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("UnrelatedArtist"));
    TestDbHelper::updateTagsReadAt(conn, trackId, 1000);

    QNetworkAccessManager network;
    MusicBrainzClient mbClient(network, db, clock);

    CorrectionStore corrStore(db, clock);
    const auto batchIdRes
        = corrStore.createBatch(CorrectionKind::MbMatch, QStringLiteral("Unrelated Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    JobQueue queue(db, clock);
    queue.registerHandler(std::make_unique<MbMatchJobHandler>(db, mbClient, clock));

    QJsonObject params;
    params.insert(QStringLiteral("batchId"), batchId);

    const auto enqueueRes = queue.enqueue(QStringLiteral("butler.mb_match"),
        QStringLiteral("Match Unrelated"), { QString::number(albumId) }, params);
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state == JobState::Completed, 10000);

    // Verify mb_album_matches has status 'no_match'
    QSqlQuery qAlbum(conn);
    qAlbum.prepare(QStringLiteral("SELECT status FROM mb_album_matches WHERE album_id = ?;"));
    qAlbum.addBindValue(albumId);
    QVERIFY(qAlbum.exec() && qAlbum.next());
    QCOMPARE(qAlbum.value(0).toString(), QStringLiteral("no_match"));

    // Verify no proposals in batch
    const auto corrsRes = corrStore.corrections(batchId);
    QVERIFY(corrsRes.ok());
    QVERIFY(corrsRes.value().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstMbMatchJob)

#include "tst_MbMatchJob.moc"
