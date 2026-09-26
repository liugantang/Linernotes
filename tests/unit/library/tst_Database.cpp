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
    void threadConnectionAffinity();
    void transactionCommitAndRollback();
    void openFailsForUnwritablePath();
};

void TstDatabase::opensAndAppliesPragmas()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();
    QVERIFY(conn.isOpen());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode;")) && q.next());
    QCOMPARE(q.value(0).toString().toLower(), QStringLiteral("wal"));

    QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys;")) && q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    QVERIFY(q.exec(QStringLiteral("PRAGMA busy_timeout;")) && q.next());
    QCOMPARE(q.value(0).toInt(), 5000);

    QVERIFY(q.exec(QStringLiteral("PRAGMA synchronous;")) && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstDatabase::createsParentDirectory()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString nestedPath = tempDir.filePath(QStringLiteral("nested/sub/dir/test.db"));
    Database db(nestedPath);
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    QVERIFY(QDir(tempDir.filePath(QStringLiteral("nested/sub/dir"))).exists());
    QVERIFY(QFileInfo::exists(nestedPath));
}

void TstDatabase::threadConnectionAffinity()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("shared.db")));

    const auto mainConnRes1 = db.connection();
    const auto mainConnRes2 = db.connection();
    QVERIFY(mainConnRes1.ok() && mainConnRes2.ok());
    QCOMPARE(mainConnRes1.value().connectionName(), mainConnRes2.value().connectionName());

    QString workerConnName;
    std::atomic<bool> workerOk { false };

    auto *thread = QThread::create([&]() {
        const auto res = db.connection();
        if (res.ok()) {
            workerConnName = res.value().connectionName();
            workerOk = QSqlDatabase::contains(workerConnName);
        }
    });

    thread->start();
    thread->wait();
    delete thread;

    QVERIFY(workerOk);
    QVERIFY(workerConnName != mainConnRes1.value().connectionName());
    QVERIFY(!QSqlDatabase::contains(workerConnName));
}

void TstDatabase::transactionCommitAndRollback()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("tx.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("CREATE TABLE items (id INT);")));

    // Rollback on destruction
    {
        Transaction tx(conn, Transaction::Mode::Immediate);
        QVERIFY(tx.isActive());
        QVERIFY(q.exec(QStringLiteral("INSERT INTO items VALUES (10);")));
    }
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items;")) && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // Commit
    {
        Transaction tx(conn, Transaction::Mode::Immediate);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO items VALUES (20);")));
        QVERIFY(tx.commit().ok());
        QVERIFY(!tx.isActive());
    }
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM items;")) && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
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

    // 2. 父路径是一个普通文件：数据库目录无法创建，任何用户（包括 root）都一样。
    {
        const QString blocker = tempDir.filePath(QStringLiteral("not_a_dir"));
        QFile blockerFile(blocker);
        QVERIFY(blockerFile.open(QIODevice::WriteOnly));
        blockerFile.close();
        const QString badPath = QDir(blocker).filePath(QStringLiteral("sub/test.db"));
        Database db(badPath);
        const auto connRes = db.connection();
        QVERIFY(!connRes.ok());
        QCOMPARE(connRes.error().code, errc::kDbOpen);
        QVERIFY2(connRes.error().detail.contains(blocker), qPrintable(connRes.error().detail));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstDatabase)

#include "tst_Database.moc"
