// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/FingerprintStore.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::FingerprintStore;
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
        const QString &contentHash = QStringLiteral("hash1"), bool missing = false)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                 "first_seen_at, scanned_at, missing_since) "
                                 "VALUES (?, ?, 1024, 2000, ?, 2000, 2000, ?);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(contentHash.isEmpty() ? QVariant() : QVariant(contentHash));
        q.addBindValue(missing ? QVariant(1000) : QVariant());
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

class TstFingerprintStore : public QObject {
    Q_OBJECT

private slots:
    void testsPendingFileIdsTransitions();
    void testsLoadAndLoadAll();
};

void TstFingerprintStore::testsPendingFileIdsTransitions()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_fp_store.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 f2 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    const qint64 f3 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/3.flac"), QStringLiteral("h3"), true); // missing
    TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/4.flac"), QStringLiteral("h4")); // no tracks

    TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertTrack(conn, f3);

    ManualClock clock(1000);
    FingerprintStore store(db, clock);

    // Initial pending: f1 and f2 (f3 is missing, f4 has no tracks)
    auto pendingRes = store.pendingFileIds();
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { f1, f2 }));

    // Save f1 -> f1 is no longer pending
    const QList<quint32> dummyItems { 0x10, 0x20, 0x30 };
    QVERIFY(store.save(f1, 1, dummyItems).ok());

    pendingRes = store.pendingFileIds();
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { f2 }));

    // Save failure for f2 -> f2 is no longer pending
    QVERIFY(store.saveFailure(f2, QStringLiteral("decode error")).ok());

    pendingRes = store.pendingFileIds();
    QVERIFY(pendingRes.ok());
    QVERIFY(pendingRes.value().isEmpty());

    // Modify files.content_hash for f1 -> f1 becomes pending again
    QSqlQuery updateQuery(conn);
    QVERIFY(
        updateQuery.exec(QStringLiteral("UPDATE files SET content_hash = 'h1_modified' WHERE id = ")
            + QString::number(f1)));

    pendingRes = store.pendingFileIds();
    QVERIFY(pendingRes.ok());
    QCOMPARE(pendingRes.value(), (QList<qint64> { f1 }));
}

void TstFingerprintStore::testsLoadAndLoadAll()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_fp_load.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/1.flac"), QStringLiteral("h1"));
    const qint64 f2 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/2.flac"), QStringLiteral("h2"));
    TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertTrack(conn, f2);

    ManualClock clock(1000);
    FingerprintStore store(db, clock);

    const QList<quint32> items1 { 0x100, 0x200, 0x300, 0x400 };
    QVERIFY(store.save(f1, 2, items1).ok());
    QVERIFY(store.saveFailure(f2, QStringLiteral("corrupt audio")).ok());

    // load f1
    auto load1Res = store.load(f1);
    QVERIFY(load1Res.ok());
    const auto &load1Opt = load1Res.value();
    QVERIFY(load1Opt.has_value());
    if (!load1Opt.has_value()) {
        return;
    }
    const auto &stored = *load1Opt;
    QCOMPARE(stored.fileId, f1);
    QCOMPARE(stored.algorithm, 2);
    QCOMPARE(stored.items, items1);

    // load f2 (failure) -> nullopt
    auto load2Res = store.load(f2);
    QVERIFY(load2Res.ok());
    const auto &load2Opt = load2Res.value();
    QVERIFY(!load2Opt.has_value());

    // loadAll -> only f1
    auto allRes = store.loadAll();
    QVERIFY(allRes.ok());
    QCOMPARE(allRes.value().size(), 1);
    QCOMPARE(allRes.value().first().fileId, f1);
    QCOMPARE(allRes.value().first().items, items1);

    // Expire f1 by changing files.content_hash
    QSqlQuery updateQuery(conn);
    QVERIFY(updateQuery.exec(QStringLiteral("UPDATE files SET content_hash = 'h1_new' WHERE id = ")
        + QString::number(f1)));

    // loadAll -> empty (f1 is now expired)
    allRes = store.loadAll();
    QVERIFY(allRes.ok());
    QVERIFY(allRes.value().isEmpty());

    // load(f1) still returns record
    load1Res = store.load(f1);
    QVERIFY(load1Res.ok());
    const auto &load1ExpiredOpt = load1Res.value();
    QVERIFY(load1ExpiredOpt.has_value());
}

} // namespace

QTEST_GUILESS_MAIN(TstFingerprintStore)

#include "tst_FingerprintStore.moc"
