// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>
#include <library/Migrator.h>

namespace {

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
        const QString &contentHash = QStringLiteral("hash1"), qint64 durationMs = 60000,
        bool missing = false, const QString &scanError = QString())
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                           "duration_ms, scan_error, first_seen_at, scanned_at, missing_since) "
                           "VALUES (?, ?, 1024, 2000, ?, ?, ?, 2000, 2000, ?);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash.isEmpty() ? QVariant() : QVariant(contentHash));
        q.addBindValue(durationMs);
        q.addBindValue(scanError.isEmpty() ? QVariant() : QVariant(scanError));
        q.addBindValue(missing ? QVariant(1000) : QVariant());
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId,
        const QVariant &cueIndex = QVariant(), const QVariant &startMs = QVariant(),
        const QVariant &endMs = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, cue_index, start_ms, end_ms, "
                                 "album_id, tags_read_at, created_at) "
                                 "VALUES (?, ?, ?, ?, NULL, 1000, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(cueIndex);
        q.addBindValue(startMs);
        q.addBindValue(endMs);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }
};

class TstEmbeddingStore : public QObject {
    Q_OBJECT

private slots:
    void pendingTrackIdsTransitions();
    void saveAndLoadAllRoundtrip();
    void sourceCueAndNormalTracks();
};

void TstEmbeddingStore::pendingTrackIdsTransitions()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_embed_pending.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 f2 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.flac"),
        QStringLiteral("h3"), 60000, true); // missing
    const qint64 f4 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/4.flac"),
        QStringLiteral("h4"), 60000, false,
        QStringLiteral("corrupted")); // scan_error

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertTrack(conn, f3);
    TestDbHelper::insertTrack(conn, f4);

    ManualClock clock(1000);
    EmbeddingStore store(db, clock);

    const QString model1 = QStringLiteral("msclap2023-3x7s");
    const QString model2 = QStringLiteral("other-model");

    // Initial pending for model1: t1 and t2 (f3 missing, f4 scan_error)
    auto pendingRes = store.pendingTrackIds(model1);
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { t1, t2 }));
    auto analyzedRes = store.analyzedCount(model1);
    QVERIFY(analyzedRes.ok());
    QCOMPARE(analyzedRes.value(), 0);

    // Save t1 with dummy 4-dim vector
    const QList<float> dummyVector { 0.5F, -0.5F, 0.25F, -0.25F };
    QVERIFY(store.save(t1, model1, dummyVector).ok());

    // t1 is no longer pending for model1, analyzedCount is 1
    pendingRes = store.pendingTrackIds(model1);
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { t2 }));
    analyzedRes = store.analyzedCount(model1);
    QVERIFY(analyzedRes.ok());
    QCOMPARE(analyzedRes.value(), 1);

    // t1 IS pending for model2 (different model), analyzedCount for model2 is 0
    auto pendingModel2 = store.pendingTrackIds(model2);
    QVERIFY(pendingModel2.ok());
    QCOMPARE(pendingModel2.value(), (QList<qint64> { t1, t2 }));
    auto analyzedModel2 = store.analyzedCount(model2);
    QVERIFY(analyzedModel2.ok());
    QCOMPARE(analyzedModel2.value(), 0);

    // Save failure for t2 -> t2 is no longer pending for model1, analyzedCount remains 1
    QVERIFY(store.saveFailure(t2, model1, QStringLiteral("decode error")).ok());

    pendingRes = store.pendingTrackIds(model1);
    QVERIFY(pendingRes.ok());
    QVERIFY(pendingRes.value().isEmpty());
    analyzedRes = store.analyzedCount(model1);
    QVERIFY(analyzedRes.ok());
    QCOMPARE(analyzedRes.value(), 1);

    // Modify files.content_hash for f1 -> t1 becomes pending again, analyzedCount drops to 0
    QSqlQuery updateQuery(conn);
    QVERIFY(
        updateQuery.exec(QStringLiteral("UPDATE files SET content_hash = 'h1_modified' WHERE id = ")
            + QString::number(f1)));

    pendingRes = store.pendingTrackIds(model1);
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { t1 }));
    analyzedRes = store.analyzedCount(model1);
    QVERIFY(analyzedRes.ok());
    QCOMPARE(analyzedRes.value(), 0);
}

void TstEmbeddingStore::saveAndLoadAllRoundtrip()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_embed_roundtrip.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 f2 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    ManualClock clock(1000);
    EmbeddingStore store(db, clock);

    const QString model = QStringLiteral("msclap2023-3x7s");
    const QList<float> vec1 { 0.123F, -0.456F, 0.789F, -0.012F };
    QVERIFY(store.save(t1, model, vec1).ok());
    QVERIFY(store.saveFailure(t2, model, QStringLiteral("audio corrupt")).ok());

    // loadAll should only return t1 (not t2)
    auto loadRes = store.loadAll(model);
    QVERIFY(loadRes.ok());
    const auto &storedList = loadRes.value();
    QCOMPARE(storedList.size(), 1);
    QCOMPARE(storedList.first().trackId, t1);

    auto countRes = store.analyzedCount(model);
    QVERIFY(countRes.ok());
    QCOMPARE(countRes.value(), 1);

    const auto &loadedVec = storedList.first().vector;
    QCOMPARE(loadedVec.size(), vec1.size());
    for (qsizetype i = 0; i < vec1.size(); ++i) {
        QVERIFY(std::abs(loadedVec.at(i) - vec1.at(i)) < 1e-3F);
    }

    // loadAll and analyzedCount for different model return empty / 0
    auto otherLoad = store.loadAll(QStringLiteral("nonexistent-model"));
    QVERIFY(otherLoad.ok());
    QVERIFY(otherLoad.value().isEmpty());
    auto otherCount = store.analyzedCount(QStringLiteral("nonexistent-model"));
    QVERIFY(otherCount.ok());
    QCOMPARE(otherCount.value(), 0);

    // Expire t1 by changing content_hash
    QSqlQuery updateQuery(conn);
    QVERIFY(updateQuery.exec(QStringLiteral("UPDATE files SET content_hash = 'h1_new' WHERE id = ")
        + QString::number(f1)));

    loadRes = store.loadAll(model);
    QVERIFY(loadRes.ok());
    QVERIFY(loadRes.value().isEmpty());
    countRes = store.analyzedCount(model);
    QVERIFY(countRes.ok());
    QCOMPARE(countRes.value(), 0);
}

void TstEmbeddingStore::sourceCueAndNormalTracks()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_embed_source.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    // Normal file: 120s
    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/album.flac"), QStringLiteral("h1"), 120000);
    // Normal track
    const qint64 tNormal = TestDbHelper::insertTrack(conn, f1);

    // CUE track with end_ms: start 10s, end 40s (duration 30s)
    const qint64 tCue1 = TestDbHelper::insertTrack(conn, f1, 1, 10000, 40000);

    // CUE track without end_ms (last track): start 90s (duration 120s - 90s = 30s)
    const qint64 tCueLast = TestDbHelper::insertTrack(conn, f1, 2, 90000, QVariant());

    ManualClock clock(1000);
    EmbeddingStore store(db, clock);

    // Normal track source
    auto srcNormal = store.source(tNormal);
    QVERIFY(srcNormal.ok());
    QCOMPARE(srcNormal.value().trackId, tNormal);
    QCOMPARE(srcNormal.value().path, QStringLiteral("/music/album.flac"));
    QCOMPARE(srcNormal.value().startMs, 0);
    QCOMPARE(srcNormal.value().durationMs, 120000);

    // CUE track 1 source
    auto srcCue1 = store.source(tCue1);
    QVERIFY(srcCue1.ok());
    QCOMPARE(srcCue1.value().trackId, tCue1);
    QCOMPARE(srcCue1.value().path, QStringLiteral("/music/album.flac"));
    QCOMPARE(srcCue1.value().startMs, 10000);
    QCOMPARE(srcCue1.value().durationMs, 30000);

    // CUE last track source
    auto srcCueLast = store.source(tCueLast);
    QVERIFY(srcCueLast.ok());
    QCOMPARE(srcCueLast.value().trackId, tCueLast);
    QCOMPARE(srcCueLast.value().path, QStringLiteral("/music/album.flac"));
    QCOMPARE(srcCueLast.value().startMs, 90000);
    QCOMPARE(srcCueLast.value().durationMs, 30000);

    // Nonexistent track
    auto srcMissing = store.source(99999);
    QVERIFY(!srcMissing.ok());
}

} // namespace

QTEST_GUILESS_MAIN(TstEmbeddingStore)

#include "tst_EmbeddingStore.moc"
