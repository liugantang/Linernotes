// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <audio/TrackEmbedding.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>
#include <library/Migrator.h>
#include <rec/SimilarTracks.h>
#include <rec/SoundIndex.h>

#include <cmath>

namespace {

using linernotes::audio::kEmbeddingModelId;
using linernotes::library::Database;
using linernotes::library::EmbeddingStore;
using linernotes::library::Migrator;
using linernotes::rec::SimilarTracks;
using linernotes::rec::SoundIndex;
using linernotes::test::ManualClock;

constexpr int kDim = 1024;

QList<float> makeVector(float v0, float v1 = 0.0F, float v2 = 0.0F)
{
    QList<float> vec(kDim, 0.0F);
    vec.replace(0, v0);
    vec.replace(1, v1);
    vec.replace(2, v2);
    return vec;
}

struct DbHelper {
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

    static qint64 insertWork(
        const QSqlDatabase &db, const QString &title = QStringLiteral("Work 1"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO works (title, created_at) VALUES (?, 1000);"));
        q.addBindValue(title);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(
        const QSqlDatabase &db, qint64 fileId, const QVariant &workId = QVariant())
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, work_id, tags_read_at, created_at) "
                                 "VALUES (?, ?, 1000, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(workId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }
};

class TstSimilarTracks : public QObject {
    Q_OBJECT

private slots:
    void similarTracksFilteringAndOrdering();
    void rebuildsWhenCountChanges();
    void returnsEmptyForTrackWithoutEmbedding();
};

void TstSimilarTracks::similarTracksFilteringAndOrdering()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_similar.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 work1 = DbHelper::insertWork(conn, QStringLiteral("Work A"));

    // Track 1 (seed): work1, vector (1.0, 0, 0)
    const qint64 f1
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 t1 = DbHelper::insertTrack(conn, f1, work1);

    // Track 2 (similar, score ~0.8): work null, vector (0.8, 0.6, 0)
    const qint64 f2
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    const qint64 t2 = DbHelper::insertTrack(conn, f2);

    // Track 3 (similar, score ~0.5): work null, vector (0.5, 0.866, 0)
    const qint64 f3
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.flac"), QStringLiteral("h3"));
    const qint64 t3 = DbHelper::insertTrack(conn, f3);

    // Track 4 (nearly identical, score 0.99 > 0.98 -> excluded): work null, vector (0.99, 0.141, 0)
    const qint64 f4
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/4.flac"), QStringLiteral("h4"));
    const qint64 t4 = DbHelper::insertTrack(conn, f4);

    // Track 5 (same work_id as Track 1, score 0.7 -> excluded): work1, vector (0.7, 0.714, 0)
    const qint64 f5
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/5.flac"), QStringLiteral("h5"));
    const qint64 t5 = DbHelper::insertTrack(conn, f5, work1);

    ManualClock clock(1000);
    EmbeddingStore store(db, clock);
    const QString model = QString(kEmbeddingModelId);

    QVERIFY(store.save(t1, model, makeVector(1.0F, 0.0F, 0.0F)).ok());
    QVERIFY(store.save(t2, model, makeVector(0.8F, 0.6F, 0.0F)).ok());
    QVERIFY(store.save(t3, model, makeVector(0.5F, 0.866F, 0.0F)).ok());
    QVERIFY(store.save(t4, model, makeVector(0.99F, 0.141F, 0.0F)).ok());
    QVERIFY(store.save(t5, model, makeVector(0.7F, 0.714F, 0.0F)).ok());

    SoundIndex soundIndex(db, clock);
    SimilarTracks similar(db, soundIndex);

    // Query similarTo for t1:
    // - t1 is excluded (self)
    // - t4 is excluded (score > 0.98F)
    // - t5 is excluded (same work_id)
    // - t2 (score ~0.8) and t3 (score ~0.5) remain, ordered descending
    const auto res = similar.similarTo(t1, 10);
    QVERIFY(res.ok());
    const auto &neighbors = res.value();
    QCOMPARE(neighbors.size(), 2);
    QCOMPARE(neighbors.at(0).id, t2);
    QVERIFY(std::abs(neighbors.at(0).score - 0.8F) < 1e-3F);
    QCOMPARE(neighbors.at(1).id, t3);
    QVERIFY(std::abs(neighbors.at(1).score - 0.5F) < 1e-3F);
}

void TstSimilarTracks::rebuildsWhenCountChanges()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_similar_rebuild.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 t1 = DbHelper::insertTrack(conn, f1);
    const qint64 f2
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    const qint64 t2 = DbHelper::insertTrack(conn, f2);

    ManualClock clock(1000);
    EmbeddingStore store(db, clock);
    const QString model = QString(kEmbeddingModelId);

    QVERIFY(store.save(t1, model, makeVector(1.0F, 0.0F, 0.0F)).ok());
    QVERIFY(store.save(t2, model, makeVector(0.5F, 0.866F, 0.0F)).ok());

    SoundIndex soundIndex(db, clock);
    SimilarTracks similar(db, soundIndex);
    auto res1 = similar.similarTo(t1, 10);
    QVERIFY(res1.ok());
    QCOMPARE(res1.value().size(), 1);
    QCOMPARE(res1.value().at(0).id, t2);

    // Add track 3 (score 0.9 with t1)
    const qint64 f3
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.flac"), QStringLiteral("h3"));
    const qint64 t3 = DbHelper::insertTrack(conn, f3);
    QVERIFY(store.save(t3, model, makeVector(0.9F, 0.435F, 0.0F)).ok());

    // Second call to similarTo should automatically rebuild index and return t3 first
    auto res2 = similar.similarTo(t1, 10);
    QVERIFY(res2.ok());
    const auto &neighbors = res2.value();
    QCOMPARE(neighbors.size(), 2);
    QCOMPARE(neighbors.at(0).id, t3);
    QVERIFY(std::abs(neighbors.at(0).score - 0.9F) < 1e-3F);
    QCOMPARE(neighbors.at(1).id, t2);
}

void TstSimilarTracks::returnsEmptyForTrackWithoutEmbedding()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_similar_empty.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = DbHelper::insertRoot(conn);
    const qint64 f1
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 t1 = DbHelper::insertTrack(conn, f1);
    const qint64 f2
        = DbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    const qint64 tUnanalyzed = DbHelper::insertTrack(conn, f2);

    ManualClock clock(1000);
    EmbeddingStore store(db, clock);
    const QString model = QString(kEmbeddingModelId);
    QVERIFY(store.save(t1, model, makeVector(1.0F, 0.0F, 0.0F)).ok());

    SoundIndex soundIndex(db, clock);
    SimilarTracks similar(db, soundIndex);

    QVERIFY(similar.hasEmbedding(t1));
    QVERIFY(!similar.hasEmbedding(tUnanalyzed));
    QVERIFY(!similar.hasEmbedding(99999));

    // Track without embedding returns empty list
    auto resUnanalyzed = similar.similarTo(tUnanalyzed, 10);
    QVERIFY(resUnanalyzed.ok());
    QVERIFY(resUnanalyzed.value().isEmpty());

    // Nonexistent track returns empty list
    auto resMissing = similar.similarTo(99999, 10);
    QVERIFY(resMissing.ok());
    QVERIFY(resMissing.value().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstSimilarTracks)

#include "tst_SimilarTracks.moc"
