// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/DatabaseBackup.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::DatabaseBackup;
using linernotes::library::Migrator;

class TstDatabaseBackup : public QObject {
    Q_OBJECT

private slots:
    void backupCreatesValidDatabaseWithoutTmpResiduals();
    void keepRetentionCleansOldestBackupsAndPreservesOtherFiles();
    void isDueReportsCorrectly();
};

void TstDatabaseBackup::backupCreatesValidDatabaseWithoutTmpResiduals()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("source.db"));
    Database db(dbPath);
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    // Insert some test data
    {
        const auto connRes = db.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO library_roots (path, added_at) VALUES ('/music/test', 12345);")));
    }

    const QString backupDir = tempDir.filePath(QStringLiteral("backups"));
    DatabaseBackup backup(db, { .backupDir = backupDir, .keep = 7 });

    const QDateTime now(QDate(2026, 9, 27), QTime(9, 30, 0));
    const auto res = backup.backupNow(now);
    QVERIFY(res.ok());

    const QString &backupFilePath = res.value();
    QCOMPARE(QFileInfo(backupFilePath).fileName(), QStringLiteral("library-20260927-093000.db"));
    QVERIFY(QFile::exists(backupFilePath));

    // Verify backup database can be opened and contains data
    {
        Database backupDb(backupFilePath);
        const auto connRes = backupDb.connection();
        QVERIFY(connRes.ok());
        QSqlQuery q(connRes.value());
        QVERIFY(q.exec(QStringLiteral("SELECT path, added_at FROM library_roots;")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("/music/test"));
        QCOMPARE(q.value(1).toLongLong(), 12345LL);
    }

    // Verify no .tmp files exist in backup directory
    const QDir dir(backupDir);
    const QStringList tmpFiles
        = dir.entryList(QStringList { QStringLiteral("*.tmp") }, QDir::Files);
    QVERIFY(tmpFiles.isEmpty());
}

void TstDatabaseBackup::keepRetentionCleansOldestBackupsAndPreservesOtherFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("source.db"));
    Database db(dbPath);
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString backupDir = tempDir.filePath(QStringLiteral("backups"));
    QVERIFY(QDir().mkpath(backupDir));

    // Create an unrelated file
    const QString notesPath = QDir(backupDir).filePath(QStringLiteral("notes.txt"));
    {
        QFile notesFile(notesPath);
        QVERIFY(notesFile.open(QIODevice::WriteOnly | QIODevice::Text));
        notesFile.write("important notes");
        notesFile.close();
    }

    DatabaseBackup backup(db, { .backupDir = backupDir, .keep = 3 });

    const QDateTime baseTime(QDate(2026, 9, 27), QTime(10, 0, 0));
    for (int i = 1; i <= 5; ++i) {
        const QDateTime now = baseTime.addSecs(i * 3600LL); // 11:00, 12:00, 13:00, 14:00, 15:00
        const auto res = backup.backupNow(now);
        QVERIFY(res.ok());
    }

    // Check backups in directory
    const QDir dir(backupDir);
    const QStringList dbFiles
        = dir.entryList(QStringList { QStringLiteral("library-*.db") }, QDir::Files, QDir::Name);
    QCOMPARE(dbFiles.size(), 3);
    QCOMPARE(dbFiles.at(0), QStringLiteral("library-20260927-130000.db"));
    QCOMPARE(dbFiles.at(1), QStringLiteral("library-20260927-140000.db"));
    QCOMPARE(dbFiles.at(2), QStringLiteral("library-20260927-150000.db"));

    // Verify oldest backups are deleted
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("library-20260927-110000.db"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("library-20260927-120000.db"))));

    // Verify unrelated file is preserved
    QVERIFY(QFile::exists(notesPath));
}

void TstDatabaseBackup::isDueReportsCorrectly()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("source.db"));
    Database db(dbPath);
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    const QString backupDir = tempDir.filePath(QStringLiteral("backups"));
    DatabaseBackup backup(db, { .backupDir = backupDir, .keep = 7 });

    const QDateTime now(QDate(2026, 9, 27), QTime(12, 0, 0));

    // No backups yet -> isDue is true
    QVERIFY(backup.isDue(now));

    // Perform backup
    const auto res = backup.backupNow(now);
    QVERIFY(res.ok());

    // Same now -> isDue is false
    QVERIFY(!backup.isDue(now));

    // now + 25 hours -> isDue is true
    const QDateTime futureTime = now.addSecs(25LL * 3600);
    QVERIFY(backup.isDue(futureTime));
}

} // namespace

QTEST_GUILESS_MAIN(TstDatabaseBackup)
#include "tst_DatabaseBackup.moc"
