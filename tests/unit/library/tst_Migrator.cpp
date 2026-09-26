// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
namespace errc = linernotes::library::errc;

void writeSqlFile(const QString &dirPath, const QString &fileName, const QString &content)
{
    QFile file(QDir(dirPath).filePath(fileName));
    Q_ASSERT(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
    file.close();
}

class TstMigrator : public QObject {
    Q_OBJECT

private slots:
    void emptyMigrationSetIsVersionZero();
    void appliesMigrationsInOrder();
    void incrementalMigrationOnlyRunsNew();
    void failedMigrationRollsBackAndKeepsPrevious();
    void rejectsInvalidMigrationSets_data();
    void rejectsInvalidMigrationSets();
    void refusesNewerDatabase();
    void backsUpBeforeUpgrade();
    void keepsOnlyThreeBackups();
    void defaultResourceMigrationsLoad();
};

void TstMigrator::emptyMigrationSetIsVersionZero()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const Migrator migrator(migDir.path());
    const auto res = migrator.migrate(conn);
    QVERIFY(res.ok());

    const auto verRes = Migrator::currentVersion(conn);
    QVERIFY(verRes.ok());
    QCOMPARE(verRes.value(), 0);

    // Verify schema_version table exists
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version';")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("schema_version"));
}

void TstMigrator::appliesMigrationsInOrder()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE songs (id INTEGER PRIMARY KEY, title TEXT);\n"));

    const QString sql0002 = QStringLiteral(
        "-- Add column and trigger with semicolons in string\n"
        "ALTER TABLE songs ADD COLUMN updated_at TEXT;\n"
        "CREATE TRIGGER update_song_time AFTER UPDATE ON songs\n"
        "BEGIN\n"
        "    UPDATE songs SET updated_at = '2026-01-01;edited' WHERE id = NEW.id;\n"
        "END;\n");
    writeSqlFile(migDir.path(), QStringLiteral("0002_trigger.sql"), sql0002);

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const Migrator migrator(migDir.path());
    const auto res = migrator.migrate(conn);
    QVERIFY(res.ok());

    const auto verRes = Migrator::currentVersion(conn);
    QVERIFY(verRes.ok());
    QCOMPARE(verRes.value(), 2);

    // Verify table, column, and trigger work
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO songs (id, title) VALUES (1, 'Song 1');")));
    QVERIFY(q.exec(QStringLiteral("UPDATE songs SET title = 'Song 1 Edited' WHERE id = 1;")));
    QVERIFY(q.exec(QStringLiteral("SELECT updated_at FROM songs WHERE id = 1;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("2026-01-01;edited"));

    // Verify schema_version has two rows
    QVERIFY(q.exec(QStringLiteral("SELECT version, name FROM schema_version ORDER BY version;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QCOMPARE(q.value(1).toString(), QStringLiteral("init"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
    QCOMPARE(q.value(1).toString(), QStringLiteral("trigger"));
    QVERIFY(!q.next());
}

void TstMigrator::incrementalMigrationOnlyRunsNew()
{
    const QTemporaryDir migDir1;
    QVERIFY(migDir1.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir1.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE counter (val INT);\n"
                       "INSERT INTO counter VALUES (1);\n"));

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // First migration with {0001}
    const Migrator migrator1(migDir1.path());
    const auto res1 = migrator1.migrate(conn);
    QVERIFY(res1.ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    // Second migration with {0001, 0002}
    const QTemporaryDir migDir2;
    QVERIFY(migDir2.isValid());
    writeSqlFile(migDir2.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE counter (val INT);\n"
                       "INSERT INTO counter VALUES (1);\n"));
    writeSqlFile(migDir2.path(), QStringLiteral("0002_extra.sql"),
        QStringLiteral("CREATE TABLE extra (id INT);\n"));

    const Migrator migrator2(migDir2.path());
    const auto res2 = migrator2.migrate(conn);
    QVERIFY(res2.ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 2);

    // Counter table still has exactly 1 row (0001 was not re-run)
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM counter;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    // Extra table exists
    QVERIFY(q.exec(
        QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='extra';")));
    QVERIFY(q.next());
}

void TstMigrator::failedMigrationRollsBackAndKeepsPrevious()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE t1 (id INTEGER PRIMARY KEY);\n"));

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const Migrator migrator1(migDir.path());
    const auto res1 = migrator1.migrate(conn);
    QVERIFY(res1.ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    // Add failing migration 0002
    writeSqlFile(migDir.path(), QStringLiteral("0002_bad.sql"),
        QStringLiteral("CREATE TABLE t2 (id INTEGER PRIMARY KEY);\n"
                       "THIS IS INVALID SYNTAX;\n"));

    const Migrator migrator2(migDir.path());
    const auto res2 = migrator2.migrate(conn);
    QVERIFY(!res2.ok());
    QCOMPARE(res2.error().code, errc::kDbMigration);
    QVERIFY(res2.error().detail.contains(QStringLiteral("0002")));

    // Version remains 1
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    // Table t1 still exists
    QSqlQuery q(conn);
    QVERIFY(
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='t1';")));
    QVERIFY(q.next());

    // Table t2 was rolled back and does not exist
    QVERIFY(
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='t2';")));
    QVERIFY(!q.next());
}

void TstMigrator::rejectsInvalidMigrationSets_data()
{
    QTest::addColumn<QStringList>("fileNames");

    QTest::newRow("non-consecutive")
        << QStringList { QStringLiteral("0001_a.sql"), QStringLiteral("0003_b.sql") };

    QTest::newRow("duplicate") << QStringList { QStringLiteral("0001_a.sql"),
        QStringLiteral("0001_b.sql") };

    QTest::newRow("not-starting-at-1") << QStringList { QStringLiteral("0002_a.sql") };

    QTest::newRow("invalid-format-short-digit") << QStringList { QStringLiteral("1_x.sql") };

    QTest::newRow("invalid-format-missing-name") << QStringList { QStringLiteral("0001.sql") };
}

void TstMigrator::rejectsInvalidMigrationSets()
{
    QFETCH(QStringList, fileNames);

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    for (const QString &fn : fileNames) {
        writeSqlFile(tempDir.path(), fn, QStringLiteral("-- dummy\nSELECT 1;\n"));
    }

    const Migrator migrator(tempDir.path());
    const auto res = migrator.migrations();
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, errc::kDbMigrationInvalid);
}

void TstMigrator::refusesNewerDatabase()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE songs (id INT);\n"));
    writeSqlFile(migDir.path(), QStringLiteral("0002_extra.sql"),
        QStringLiteral("CREATE TABLE extra (id INT);\n"));

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // Manually setup schema_version with version 3
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, "
                                      "name TEXT, applied_at TEXT);")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO schema_version VALUES (3, 'future', '2026-09-26T00:00:00Z');")));
    }

    const Migrator migrator(migDir.path());
    const auto res = migrator.migrate(conn);
    QVERIFY(!res.ok());
    QCOMPARE(res.error().code, errc::kDbTooNew);

    // DB version remains 3
    QCOMPARE(Migrator::currentVersion(conn).value(), 3);
}

void TstMigrator::backsUpBeforeUpgrade()
{
    const QTemporaryDir migDir1;
    QVERIFY(migDir1.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir1.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE songs (id INT);\n"));

    const QString dbPath = dbDir.filePath(QStringLiteral("music.db"));
    Database db(dbPath);
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // 1. Initial migration v0 -> v1 (no backup should be created)
    const Migrator migrator1(migDir1.path());
    const auto res1 = migrator1.migrate(conn);
    QVERIFY(res1.ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    const QDir dir(dbDir.path());
    const auto initialBackups
        = dir.entryInfoList({ QStringLiteral("music.db.bak-v*") }, QDir::Files);
    QCOMPARE(initialBackups.size(), 0);

    // 2. Upgrade v1 -> v2 (backup should be created with version 1)
    const QTemporaryDir migDir2;
    QVERIFY(migDir2.isValid());
    writeSqlFile(migDir2.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE songs (id INT);\n"));
    writeSqlFile(migDir2.path(), QStringLiteral("0002_extra.sql"),
        QStringLiteral("CREATE TABLE extra (id INT);\n"));

    const Migrator migrator2(migDir2.path());
    const auto res2 = migrator2.migrate(conn);
    QVERIFY(res2.ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 2);

    const auto upgradedBackups
        = dir.entryInfoList({ QStringLiteral("music.db.bak-v1-*") }, QDir::Files);
    QCOMPARE(upgradedBackups.size(), 1);

    // Verify the backup file itself is a valid database with version 1
    const QString backupPath = upgradedBackups.first().absoluteFilePath();
    Database backupDb(backupPath);
    const auto backupConnRes = backupDb.connection();
    QVERIFY(backupConnRes.ok());
    QCOMPARE(Migrator::currentVersion(backupConnRes.value()).value(), 1);
}

void TstMigrator::keepsOnlyThreeBackups()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const QString dbPath = dbDir.filePath(QStringLiteral("archive.db"));
    Database db(dbPath);
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // Migrate to v1 first
    writeSqlFile(migDir.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE t1 (id INT);\n"));
    const Migrator migrator1(migDir.path());
    QVERIFY(migrator1.migrate(conn).ok());

    // Pre-place older backup files with varying version digits and an invalidly formatted note file
    const QString b9 = QStringLiteral("archive.db.bak-v9-20200101-000001");
    const QString b10 = QStringLiteral("archive.db.bak-v10-20200101-000002");
    const QString b11 = QStringLiteral("archive.db.bak-v11-20200101-000003");
    const QString bNote = QStringLiteral("archive.db.bak-vX-note");

    const QDir dir(dbDir.path());
    for (const QString &bf : { b9, b10, b11, bNote }) {
        QFile f(dir.filePath(bf));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("backup");
        f.close();
    }

    // Now migrate v1 -> v2 (creates a 4th valid backup)
    writeSqlFile(migDir.path(), QStringLiteral("0002_extra.sql"),
        QStringLiteral("CREATE TABLE t2 (id INT);\n"));
    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());

    // Oldest backup (v9) must have been deleted
    QVERIFY(!QFile::exists(dir.filePath(b9)));

    // Newer backups (v10, v11), the new backup, and the unformatted note file must remain
    QVERIFY(QFile::exists(dir.filePath(b10)));
    QVERIFY(QFile::exists(dir.filePath(b11)));
    QVERIFY(QFile::exists(dir.filePath(bNote)));

    const auto remainingFiles
        = dir.entryInfoList({ QStringLiteral("archive.db.bak-*") }, QDir::Files);
    // 3 valid backups + 1 unformatted note = 4 files total
    QCOMPARE(remainingFiles.size(), 4);
}

void TstMigrator::defaultResourceMigrationsLoad()
{
    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    QVERIFY(res.value().isEmpty());
}

} // namespace

QTEST_GUILESS_MAIN(TstMigrator)

#include "tst_Migrator.moc"
