// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <library/Database.h>
#include <library/Errors.h>

#include <atomic>

namespace {

using linernotes::library::Database;
using linernotes::library::Transaction;
namespace errc = linernotes::library::errc;

class TstDatabase : public QObject {
    Q_OBJECT

private slots:
    void opensAndAppliesPragmas();
    void createsParentDirectory();
    void sameThreadReturnsSameConnection();
    void eachThreadGetsOwnConnection();
    void connectionsRemovedWhenThreadExits();
    void transactionRollsBackOnDestruction();
    void transactionCommits();
    void openFailsForUnwritablePath();
};

void TstDatabase::opensAndAppliesPragmas()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    Database db(dbPath);

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();
    QVERIFY(conn.isOpen());

    // 1. journal_mode == wal
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString().toLower(), QStringLiteral("wal"));
    }

    // 2. foreign_keys == 1
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // 3. busy_timeout == 5000
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("PRAGMA busy_timeout;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 5000);
    }

    // 4. synchronous == 1 (NORMAL)
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("PRAGMA synchronous;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }
}

void TstDatabase::createsParentDirectory()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString nestedPath = tempDir.filePath(QStringLiteral("nested/sub/dir/test.db"));
    QVERIFY(!QDir(tempDir.filePath(QStringLiteral("nested/sub/dir"))).exists());

    Database db(nestedPath);
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    QVERIFY(QDir(tempDir.filePath(QStringLiteral("nested/sub/dir"))).exists());
    QVERIFY(QFileInfo::exists(nestedPath));
}

void TstDatabase::sameThreadReturnsSameConnection()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const auto connRes1 = db.connection();
    QVERIFY(connRes1.ok());

    const auto connRes2 = db.connection();
    QVERIFY(connRes2.ok());

    QCOMPARE(connRes1.value().connectionName(), connRes2.value().connectionName());
}

void TstDatabase::eachThreadGetsOwnConnection()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("shared.db")));

    // Main thread creates table and writes row
    const auto mainConnRes = db.connection();
    QVERIFY(mainConnRes.ok());
    const auto &mainConn = mainConnRes.value();

    {
        QSqlQuery q(mainConn);
        QVERIFY(q.exec(
            QStringLiteral("CREATE TABLE shared_items (id INTEGER PRIMARY KEY, name TEXT);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO shared_items VALUES (1, 'song_one');")));
    }

    QString t1ConnName;
    QString t2ConnName;
    QString t1ReadName;
    QString t2ReadName;
    std::atomic<bool> t1Success { false };
    std::atomic<bool> t2Success { false };

    auto *thread1 = QThread::create([&]() {
        const auto res = db.connection();
        if (res.ok()) {
            const auto &conn = res.value();
            t1ConnName = conn.connectionName();
            QSqlQuery q(conn);
            if (q.exec(QStringLiteral("SELECT name FROM shared_items WHERE id = 1;")) && q.next()) {
                t1ReadName = q.value(0).toString();
                t1Success = true;
            }
        }
    });

    auto *thread2 = QThread::create([&]() {
        const auto res = db.connection();
        if (res.ok()) {
            const auto &conn = res.value();
            t2ConnName = conn.connectionName();
            QSqlQuery q(conn);
            if (q.exec(QStringLiteral("SELECT name FROM shared_items WHERE id = 1;")) && q.next()) {
                t2ReadName = q.value(0).toString();
                t2Success = true;
            }
        }
    });

    thread1->start();
    thread2->start();
    thread1->wait();
    thread2->wait();
    delete thread1;
    delete thread2;

    QVERIFY(t1Success);
    QVERIFY(t2Success);
    QCOMPARE(t1ReadName, QStringLiteral("song_one"));
    QCOMPARE(t2ReadName, QStringLiteral("song_one"));

    QVERIFY(t1ConnName != mainConn.connectionName());
    QVERIFY(t2ConnName != mainConn.connectionName());
    QVERIFY(t1ConnName != t2ConnName);
}

void TstDatabase::connectionsRemovedWhenThreadExits()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("cleanup.db")));

    QString workerConnName;

    auto *thread = QThread::create([&]() {
        const auto res = db.connection();
        if (res.ok()) {
            workerConnName = res.value().connectionName();
            QVERIFY(QSqlDatabase::contains(workerConnName));
        }
    });

    thread->start();
    thread->wait();
    delete thread;

    QVERIFY(!workerConnName.isEmpty());
    QVERIFY(!QSqlDatabase::contains(workerConnName));
}

void TstDatabase::transactionRollsBackOnDestruction()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("tx_rb.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INT);")));
    }

    {
        Transaction tx(conn, Transaction::Mode::Immediate);
        QVERIFY(tx.isActive());
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO items VALUES (10);")));
        // Do not call commit() -> roll back on destruction
    }

    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
}

void TstDatabase::transactionCommits()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("tx_cm.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INT);")));
    }

    {
        Transaction tx(conn, Transaction::Mode::Immediate);
        QVERIFY(tx.isActive());
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO items VALUES (20);")));
        const auto commitRes = tx.commit();
        QVERIFY(commitRes.ok());
        QVERIFY(!tx.isActive());
    }

    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }
}

void TstDatabase::openFailsForUnwritablePath()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral("still in use")));

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // 1. Point Database path directly to an existing directory
    {
        Database db(tempDir.path());
        const auto connRes = db.connection();
        QVERIFY(!connRes.ok());
        QCOMPARE(connRes.error().code, errc::kDbOpen);
        QVERIFY(connRes.error().detail.contains(tempDir.path()));
    }

    // 2. Point Database to an unwritable directory to test db.open() failure
    {
        const QString subDir = tempDir.filePath(QStringLiteral("readonly_dir"));
        QDir().mkdir(subDir);
        QFile::setPermissions(subDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const QString badPath = QDir(subDir).filePath(QStringLiteral("test.db"));
        Database db(badPath);
        const auto connRes = db.connection();
        QVERIFY(!connRes.ok());
        QCOMPARE(connRes.error().code, errc::kDbOpen);
        QVERIFY(connRes.error().detail.contains(badPath));
        QFile::setPermissions(
            subDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstDatabase)

#include "tst_Database.moc"
