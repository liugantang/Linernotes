// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <ai/JobQueue.h>
#include <butler/FingerprintJobHandler.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/FingerprintStore.h>
#include <library/Migrator.h>

namespace {

using linernotes::ai::JobQueue;
using linernotes::ai::JobState;
using linernotes::butler::FingerprintJobHandler;
using linernotes::library::Database;
using linernotes::library::FingerprintStore;
using linernotes::library::Migrator;
using linernotes::test::fixturePath;
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

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path,
        const QString &contentHash = QStringLiteral("hash1"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                 "first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1024, 2000, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 1000, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }
};

class TstFingerprintJob : public QObject {
    Q_OBJECT

private slots:
    void processesAudioAndCorruptFiles();
};

void TstFingerprintJob::processesAudioAndCorruptFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_fp_job.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const QString melodyPath = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString corruptPath = fixturePath(QStringLiteral("audio/corrupt.flac"));

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1
        = TestDbHelper::insertFile(conn, rootId, melodyPath, QStringLiteral("h_melody"));
    const qint64 f2
        = TestDbHelper::insertFile(conn, rootId, corruptPath, QStringLiteral("h_corrupt"));

    TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertTrack(conn, f2);

    ManualClock clock(1000);
    FingerprintStore store(db, clock);

    // Both files should initially be pending
    const auto pendingBefore = store.pendingFileIds();
    QVERIFY(pendingBefore.ok());
    QCOMPARE(pendingBefore.value(), (QList<qint64> { f1, f2 }));

    JobQueue queue(db, clock);
    queue.registerHandler(std::make_unique<FingerprintJobHandler>(db, clock));

    const auto enqueueRes = queue.enqueue(QStringLiteral("butler.fingerprint"),
        QStringLiteral("Test Fingerprint Job"), { QString::number(f1), QString::number(f2) });
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    // Wait for the job queue to finish both items
    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(jobId).value_or(linernotes::ai::JobInfo { }).state == JobState::Completed, 10000);
    const auto jobOpt = queue.job(jobId);
    QVERIFY(jobOpt.has_value());
    if (!jobOpt.has_value()) {
        return;
    }
    QCOMPARE(jobOpt->state, JobState::Completed);
    QCOMPARE(jobOpt->done, 2);
    QCOMPARE(jobOpt->failed, 0);

    // Check store state: neither file should be pending anymore
    const auto pendingAfter = store.pendingFileIds();
    QVERIFY(pendingAfter.ok());
    QVERIFY(pendingAfter.value().isEmpty());

    // Melody file should have successful fingerprint
    const auto load1 = store.load(f1);
    QVERIFY(load1.ok());
    const auto &load1Opt = load1.value();
    QVERIFY(load1Opt.has_value());
    if (!load1Opt.has_value()) {
        return;
    }
    QVERIFY(!load1Opt->items.isEmpty());

    // Corrupt file should not have successful fingerprint in load()
    const auto load2 = store.load(f2);
    QVERIFY(load2.ok());
    const auto &load2Opt = load2.value();
    QVERIFY(!load2Opt.has_value());

    // Corrupt file should have a failure error record in database
    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("SELECT error FROM fingerprints WHERE file_id = ") + QString::number(f2)));
    QVERIFY(q.next());
    QVERIFY(!q.value(0).isNull());
    QVERIFY(!q.value(0).toString().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstFingerprintJob)

#include "tst_FingerprintJob.moc"
