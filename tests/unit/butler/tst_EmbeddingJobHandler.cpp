// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <audio/TrackEmbedding.h>
#include <butler/EmbeddingJobHandler.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::EmbeddingJobHandler;
using linernotes::library::Database;
using linernotes::library::EmbeddingStore;
using linernotes::library::Migrator;
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
        const QString &contentHash = QStringLiteral("hash1"), qint64 durationMs = 60000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                 "duration_ms, first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1024, 2000, ?, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash);
        q.addBindValue(durationMs);
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

class TstEmbeddingJobHandler : public QObject {
    Q_OBJECT

private slots:
    void propertiesAndEstimate();
    void processFailsGracefullyWhenModelNotFound();
};

void TstEmbeddingJobHandler::propertiesAndEstimate()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_handler_prop.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000);
    EmbeddingJobHandler handler(db, clock, QStringLiteral("/nonexistent/model.onnx"));

    QCOMPARE(handler.kind(), QStringLiteral("audio.embed"));
    QCOMPARE(handler.maxInFlight(), 1);

    const auto usage = handler.estimate(QStringLiteral("1"), { });
    QCOMPARE(usage.promptTokens, 0);
    QCOMPARE(usage.completionTokens, 0);
}

void TstEmbeddingJobHandler::processFailsGracefullyWhenModelNotFound()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_handler_process.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/song.flac"), QStringLiteral("h1"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);

    ManualClock clock(1000);
    EmbeddingJobHandler handler(db, clock, QStringLiteral("/nonexistent/model.onnx"));

    bool callbackCalled = false;
    linernotes::core::Result<void> processResult;

    auto worker
        = handler.process(QString::number(t1), { }, [&](const linernotes::core::Result<void> &res) {
              callbackCalled = true;
              processResult = res;
          });

    QVERIFY(worker != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(callbackCalled, 5000);
    QVERIFY(!processResult.ok());

    // Verify that the failure was not written to audio_embeddings table (because it was model load
    // failure)
    EmbeddingStore store(db, clock);
    const auto pendingRes = store.pendingTrackIds(QString::fromLatin1(
        linernotes::audio::kEmbeddingModelId.data(), linernotes::audio::kEmbeddingModelId.size()));
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { t1 }));
}

} // namespace

QTEST_GUILESS_MAIN(TstEmbeddingJobHandler)

#include "tst_EmbeddingJobHandler.moc"
