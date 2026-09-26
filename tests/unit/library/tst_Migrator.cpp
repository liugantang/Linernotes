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
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    Q_ASSERT(opened);
    Q_UNUSED(opened);
    file.write(content.toUtf8());
    file.close();
}

class TstMigrator : public QObject {
    Q_OBJECT

private slots:
    void appliesMigrationsInOrder();
    void failedMigrationRollsBackAndKeepsPrevious();
    void rejectsInvalidMigrationFiles();
    void refusesNewerDatabase();
    void backsUpBeforeUpgrade();
    void defaultResourceMigrationsLoad();
    void upgradesTo0006AddsPlayEventDetails();
};

void TstMigrator::appliesMigrationsInOrder()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE songs (id INTEGER PRIMARY KEY, title TEXT);\n"));

    writeSqlFile(migDir.path(), QStringLiteral("0002_trigger.sql"),
        QStringLiteral("ALTER TABLE songs ADD COLUMN updated_at TEXT;\n"
                       "CREATE TRIGGER update_song_time AFTER UPDATE ON songs\n"
                       "BEGIN\n"
                       "    UPDATE songs SET updated_at = '2026-01-01;edited' WHERE id = NEW.id;\n"
                       "END;\n"));

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const Migrator migrator(migDir.path());
    QVERIFY(migrator.migrate(conn).ok());

    const auto verRes = Migrator::currentVersion(conn);
    QVERIFY(verRes.ok());
    QCOMPARE(verRes.value(), 2);

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO songs (id, title) VALUES (1, 'Song 1');")));
    QVERIFY(q.exec(QStringLiteral("UPDATE songs SET title = 'Song 1 Edited' WHERE id = 1;")));
    QVERIFY(q.exec(QStringLiteral("SELECT updated_at FROM songs WHERE id = 1;")) && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("2026-01-01;edited"));
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
    QVERIFY(migrator1.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    // Add failing migration 0002
    writeSqlFile(migDir.path(), QStringLiteral("0002_bad.sql"),
        QStringLiteral("CREATE TABLE t2 (id INTEGER PRIMARY KEY);\n"
                       "THIS IS INVALID SYNTAX;\n"));

    const Migrator migrator2(migDir.path());
    const auto res2 = migrator2.migrate(conn);
    QVERIFY(!res2.ok());
    QCOMPARE(res2.error().code, errc::kDbMigration);
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    QSqlQuery q(conn);
    QVERIFY(
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='t1';")));
    QVERIFY(q.next());
    QVERIFY(
        q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='t2';")));
    QVERIFY(!q.next());
}

void TstMigrator::rejectsInvalidMigrationFiles()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    writeSqlFile(tempDir.path(), QStringLiteral("0001_a.sql"), QStringLiteral("SELECT 1;"));
    writeSqlFile(tempDir.path(), QStringLiteral("0003_c.sql"), QStringLiteral("SELECT 1;"));

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

    Database db(dbDir.filePath(QStringLiteral("test.db")));
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // Setup schema_version with future version 3
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 3);
}

void TstMigrator::backsUpBeforeUpgrade()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    writeSqlFile(migDir.path(), QStringLiteral("0001_init.sql"),
        QStringLiteral("CREATE TABLE songs (id INT);\n"));

    const QString dbPath = dbDir.filePath(QStringLiteral("music.db"));
    Database db(dbPath);
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // 1. Initial migration v0 -> v1 (no backup)
    const Migrator migrator1(migDir.path());
    QVERIFY(migrator1.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    const QDir dir(dbDir.path());
    QCOMPARE(dir.entryInfoList({ QStringLiteral("music.db.bak-v*") }, QDir::Files).size(), 0);

    // 2. Upgrade v1 -> v2 (backup created)
    writeSqlFile(migDir.path(), QStringLiteral("0002_extra.sql"),
        QStringLiteral("CREATE TABLE extra (id INT);\n"));

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 2);

    const auto upgradedBackups
        = dir.entryInfoList({ QStringLiteral("music.db.bak-v1-*") }, QDir::Files);
    QCOMPARE(upgradedBackups.size(), 1);

    Database backupDb(upgradedBackups.first().absoluteFilePath());
    const auto backupConnRes = backupDb.connection();
    QVERIFY(backupConnRes.ok());
    QCOMPARE(Migrator::currentVersion(backupConnRes.value()).value(), 1);
}

void TstMigrator::defaultResourceMigrationsLoad()
{
    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &migrations = res.value();
    QVERIFY(!migrations.isEmpty());
    QCOMPARE(migrations.first().version, 1);
    QCOMPARE(migrations.first().name, QStringLiteral("core_schema"));
    for (int i = 0; i < migrations.size(); ++i) {
        QCOMPARE(migrations.at(i).version, i + 1);
    }
}

void TstMigrator::upgradesTo0006AddsPlayEventDetails()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 6);

    // Write migrations 1..5
    for (int i = 0; i < 5; ++i) {
        const auto &m = allMigrations.at(i);
        const QString fileName
            = QStringLiteral("%1_%2.sql").arg(m.version, 4, 10, QLatin1Char('0')).arg(m.name);
        writeSqlFile(migDir.path(), fileName, m.sql);
    }

    const QString dbPath = dbDir.filePath(QStringLiteral("test.db"));
    Database db(dbPath);
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const Migrator migrator1(migDir.path());
    QVERIFY(migrator1.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 5);

    // Insert row in v5 play_events table
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO play_events (id, track_id, started_at, played_ms, track_duration_ms, "
            "completed, skipped, source) VALUES (1, NULL, 1000, 500, 1000, 0, 0, 'local');")));
    }

    // Add migration 6
    const auto &m6 = allMigrations.at(5);
    const QString fileName6
        = QStringLiteral("%1_%2.sql").arg(m6.version, 4, 10, QLatin1Char('0')).arg(m6.name);
    writeSqlFile(migDir.path(), fileName6, m6.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 6);

    // Verify default values on existing row
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT play_source, skip_position_ms, paused_ms, device "
                                  "FROM play_events WHERE id = 1;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("unknown"));
    QVERIFY(q.value(1).isNull());
    QCOMPARE(q.value(2).toLongLong(), 0LL);
    QVERIFY(q.value(3).isNull());
}

} // namespace

QTEST_GUILESS_MAIN(TstMigrator)

#include "tst_Migrator.moc"
