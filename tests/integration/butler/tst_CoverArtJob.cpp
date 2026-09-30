// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <ai/JobQueue.h>
#include <butler/CoverArtJobHandler.h>
#include <butler/CoverArtSource.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::ai::JobInfo;
using linernotes::ai::JobQueue;
using linernotes::ai::JobState;
using linernotes::butler::CoverArtJobHandler;
using linernotes::butler::CoverArtSource;
using linernotes::library::CoverStore;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::fixturePath;
using linernotes::test::ManualClock;

struct TestDbHelper {
    static qint64 insertAlbum(const QSqlDatabase &db, const QString &title)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO albums (grouping_key, title, created_at) VALUES (?, ?, 1000);"));
        q.addBindValue(QString(QStringLiteral("key:") + title));
        q.addBindValue(title);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertMbAlbumMatch(const QSqlDatabase &db, qint64 albumId, const QString &status,
        const QString &releaseId, const QString &releaseGroupId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO mb_album_matches (album_id, status, release_id, release_group_id, score, "
            "matched_at) VALUES (?, ?, ?, ?, 1.0, 1000);"));
        q.addBindValue(albumId);
        q.addBindValue(status);
        q.addBindValue(releaseId);
        q.addBindValue(releaseGroupId);
        return q.exec();
    }
};

class TstCoverArtJob : public QObject {
    Q_OBJECT

private slots:
    void testCoverArtDownloadJob();
};

void TstCoverArtJob::testCoverArtDownloadJob()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_cover_art_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const QTemporaryDir coverCacheDir;
    QVERIFY(coverCacheDir.isValid());
    CoverStore coverStore(coverCacheDir.path());

    const ManualClock clock(1000000);

    // Album A: matched, release 11ade0fa-6831-4169-830b-baf5b827878e (has release fixture)
    const qint64 albumA = TestDbHelper::insertAlbum(conn, QStringLiteral("Album A"));
    QVERIFY(albumA > 0);
    QVERIFY(TestDbHelper::insertMbAlbumMatch(conn, albumA, QStringLiteral("matched"),
        QStringLiteral("11ade0fa-6831-4169-830b-baf5b827878e"), QStringLiteral("rg-a")));

    // Album B: matched, release 00000000-0000-0000-0000-00000000000b (no release fixture),
    // release group ffb76de5-227e-4acb-9b6f-9bfc292433f8 (has release-group fixture)
    const qint64 albumB = TestDbHelper::insertAlbum(conn, QStringLiteral("Album B"));
    QVERIFY(albumB > 0);
    QVERIFY(TestDbHelper::insertMbAlbumMatch(conn, albumB, QStringLiteral("matched"),
        QStringLiteral("00000000-0000-0000-0000-00000000000b"),
        QStringLiteral("ffb76de5-227e-4acb-9b6f-9bfc292433f8")));

    // Album C: matched, release and release group both have no fixture
    const qint64 albumC = TestDbHelper::insertAlbum(conn, QStringLiteral("Album C"));
    QVERIFY(albumC > 0);
    QVERIFY(TestDbHelper::insertMbAlbumMatch(conn, albumC, QStringLiteral("matched"),
        QStringLiteral("00000000-0000-0000-0000-00000000000c"),
        QStringLiteral("00000000-0000-0000-0000-00000000000d")));

    // Verify pending albums contains all 3
    const CoverArtSource source(db);
    const auto pendingBefore = source.pendingAlbums();
    QVERIFY(pendingBefore.ok());
    QCOMPARE(pendingBefore.value().size(), 3);
    QVERIFY(pendingBefore.value().contains(albumA));
    QVERIFY(pendingBefore.value().contains(albumB));
    QVERIFY(pendingBefore.value().contains(albumC));

    QNetworkAccessManager network;
    const QUrl baseUrl = QUrl::fromLocalFile(fixturePath(QStringLiteral("coverart")));

    JobQueue queue(db, clock);
    queue.registerHandler(
        std::make_unique<CoverArtJobHandler>(db, network, coverStore, clock, baseUrl));

    const auto enqueueRes
        = queue.enqueue(QStringLiteral("butler.cover_art"), QStringLiteral("Download Covers"),
            { QString::number(albumA), QString::number(albumB), QString::number(albumC) });
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state == JobState::Completed, 10000);

    const auto jobOpt = queue.job(jobId);
    QVERIFY(jobOpt.has_value());
    if (!jobOpt.has_value()) {
        return;
    }
    QCOMPARE(jobOpt->state, JobState::Completed);
    QCOMPARE(jobOpt->done, 3);
    QCOMPARE(jobOpt->failed, 0);

    // Verify Album A
    {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT a.cover_id, m.cover_checked_at FROM albums a JOIN "
                                 "mb_album_matches m ON m.album_id = a.id WHERE a.id = ?;"));
        q.addBindValue(albumA);
        QVERIFY(q.exec() && q.next());
        QVERIFY(!q.value(0).isNull());
        QVERIFY(!q.value(1).isNull());

        const qint64 coverId = q.value(0).toLongLong();
        QSqlQuery coverQ(conn);
        coverQ.prepare(QStringLiteral("SELECT source, source_path FROM covers WHERE id = ?;"));
        coverQ.addBindValue(coverId);
        QVERIFY(coverQ.exec() && coverQ.next());
        QCOMPARE(coverQ.value(0).toString(), QStringLiteral("online"));
        QVERIFY(coverQ.value(1).toString().endsWith(
            QStringLiteral("/release/11ade0fa-6831-4169-830b-baf5b827878e/front-500")));
    }

    // Verify Album B
    {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT a.cover_id, m.cover_checked_at FROM albums a JOIN "
                                 "mb_album_matches m ON m.album_id = a.id WHERE a.id = ?;"));
        q.addBindValue(albumB);
        QVERIFY(q.exec() && q.next());
        QVERIFY(!q.value(0).isNull());
        QVERIFY(!q.value(1).isNull());

        const qint64 coverId = q.value(0).toLongLong();
        QSqlQuery coverQ(conn);
        coverQ.prepare(QStringLiteral("SELECT source, source_path FROM covers WHERE id = ?;"));
        coverQ.addBindValue(coverId);
        QVERIFY(coverQ.exec() && coverQ.next());
        QCOMPARE(coverQ.value(0).toString(), QStringLiteral("online"));
        QVERIFY(coverQ.value(1).toString().contains(QStringLiteral("/release-group/")));
        QVERIFY(coverQ.value(1).toString().endsWith(
            QStringLiteral("/release-group/ffb76de5-227e-4acb-9b6f-9bfc292433f8/front-500")));
    }

    // Verify Album C
    {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT a.cover_id, m.cover_checked_at FROM albums a JOIN "
                                 "mb_album_matches m ON m.album_id = a.id WHERE a.id = ?;"));
        q.addBindValue(albumC);
        QVERIFY(q.exec() && q.next());
        QVERIFY(q.value(0).isNull());
        QVERIFY(!q.value(1).isNull());
    }

    // Verify pending albums is empty after all finished
    const auto pendingAfter = source.pendingAlbums();
    QVERIFY(pendingAfter.ok());
    QVERIFY(pendingAfter.value().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstCoverArtJob)

#include "tst_CoverArtJob.moc"
