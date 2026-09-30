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
    void upgradesTo0007AddsTrackPlayStats();
    void upgradesTo0009AddsLlmUsage();
    void upgradesTo0010AddsJobs();
    void upgradesTo0011AddsButlerTables();
    void upgradesTo0012AddsCorrectionLocale();
    void upgradesTo0015AddsFingerprintsTable();
    void upgradesTo0016AddsMbMatches();
    void upgradesTo0017AddsCoverChecked();
    void upgradesTo0024AddsAlbumInfoChecks();
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

void TstMigrator::upgradesTo0007AddsTrackPlayStats()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 7);

    // Write migrations 1..6
    for (int i = 0; i < 6; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 6);

    // Insert track row
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                      "VALUES (1, '/music', 1, 100);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                      "mtime, first_seen_at, scanned_at) VALUES "
                                      "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1);")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                      "VALUES (1, 1, 1, 1);")));
    }

    // Add migration 7
    const auto &m7 = allMigrations.at(6);
    const QString fileName7
        = QStringLiteral("%1_%2.sql").arg(m7.version, 4, 10, QLatin1Char('0')).arg(m7.name);
    writeSqlFile(migDir.path(), fileName7, m7.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 7);

    // Verify track_play_stats table and album_play_completion view exist and can be queried
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM track_play_stats;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM album_play_completion;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstMigrator::upgradesTo0009AddsLlmUsage()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 9);

    // Write migrations 1..8
    for (int i = 0; i < 8; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 8);

    // Add migration 9
    const auto &m9 = allMigrations.at(8);
    const QString fileName9
        = QStringLiteral("%1_%2.sql").arg(m9.version, 4, 10, QLatin1Char('0')).arg(m9.name);
    writeSqlFile(migDir.path(), fileName9, m9.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 9);

    // Verify llm_usage table exists and can be inserted/queried
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO llm_usage (id, created_at, purpose, service_id, model, prompt_tokens, "
        "completion_tokens, cached, ok, error_code, elapsed_ms) "
        "VALUES (1, 1000, 'query', 'svc1', 'gpt-4o', 10, 5, 0, 1, NULL, 120);")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM llm_usage;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstMigrator::upgradesTo0010AddsJobs()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 10);

    // Write migrations 1..9
    for (int i = 0; i < 9; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 9);

    // Add migration 10
    const auto &m10 = allMigrations.at(9);
    const QString fileName10
        = QStringLiteral("%1_%2.sql").arg(m10.version, 4, 10, QLatin1Char('0')).arg(m10.name);
    writeSqlFile(migDir.path(), fileName10, m10.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 10);

    // Verify jobs and job_items tables exist and can be inserted/queried
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO jobs (id, kind, title, params, state, last_error, created_at, updated_at) "
        "VALUES (1, 'cleanup.normalize', 'Normalize Artist', '{}', 'running', NULL, 1000, "
        "1000);")));
    QVERIFY(
        q.exec(QStringLiteral("INSERT INTO job_items (job_id, seq, item_key, state, error_code) "
                              "VALUES (1, 0, 'artist:1', 'pending', NULL);")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM jobs;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM job_items;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstMigrator::upgradesTo0011AddsButlerTables()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 11);

    // Write migrations 1..10
    for (int i = 0; i < 10; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 10);

    // Insert correction_batches row in v10 schema
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO correction_batches (id, source, description, created_at) "
                           "VALUES (1, 'rule', 'Old Batch', 1000);")));
    }

    // Add migration 11
    const auto &m11 = allMigrations.at(10);
    const QString fileName11
        = QStringLiteral("%1_%2.sql").arg(m11.version, 4, 10, QLatin1Char('0')).arg(m11.name);
    writeSqlFile(migDir.path(), fileName11, m11.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 11);

    // Verify existing correction_batches row has default kind 'manual'
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT kind FROM correction_batches WHERE id = 1;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("manual"));

    // Verify track_issues and mb_cache tables exist and can be inserted/queried
    QVERIFY(q.exec(QStringLiteral("INSERT INTO library_roots (id, path, enabled, added_at) "
                                  "VALUES (1, '/music', 1, 100);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO files (id, root_id, path, duration_ms, size, "
                                  "mtime, first_seen_at, scanned_at) VALUES "
                                  "(1, 1, '/music/1.mp3', 60000, 1, 1, 1, 1);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                  "VALUES (1, 1, 1, 1);")));
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO track_issues (track_id, kind, field, detail, created_at) "
                       "VALUES (1, 'needs_online', 'title', 'damaged', 1000);")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM track_issues;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO mb_cache (url, body, fetched_at) "
        "VALUES ('https://musicbrainz.org/ws/2/recording/123', '{\"test\": 1}', 1000);")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM mb_cache;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstMigrator::upgradesTo0012AddsCorrectionLocale()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 12);

    // Write migrations 1..11
    for (int i = 0; i < 11; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 11);

    // Insert correction row in v11 schema
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO corrections (id, entity_type, entity_id, field, old_value, new_value, "
            "source, confidence, status, created_at) "
            "VALUES (1, 'artist', 10, 'alias', NULL, 'Jay', 'rule', 0.9, 'pending', 1000);")));
    }

    // Add migration 12
    const auto &m12 = allMigrations.at(11);
    const QString fileName12
        = QStringLiteral("%1_%2.sql").arg(m12.version, 4, 10, QLatin1Char('0')).arg(m12.name);
    writeSqlFile(migDir.path(), fileName12, m12.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 12);

    // Verify existing corrections row has NULL locale
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT locale FROM corrections WHERE id = 1;")));
    QVERIFY(q.next());
    QVERIFY(q.value(0).isNull());

    // Verify inserting new correction row with locale works
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO corrections (id, entity_type, entity_id, field, old_value, new_value, "
        "locale, source, confidence, status, created_at) "
        "VALUES (2, 'artist', 10, 'alias', NULL, 'Jay Chou', 'en', 'musicbrainz', 1.0, 'pending', "
        "2000);")));
    QVERIFY(q.exec(QStringLiteral("SELECT locale FROM corrections WHERE id = 2;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("en"));
}

void TstMigrator::upgradesTo0015AddsFingerprintsTable()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 15);

    // Write migrations 1..14
    for (int i = 0; i < 14; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 14);

    // Add migration 15
    const auto &m15 = allMigrations.at(14);
    const QString fileName15
        = QStringLiteral("%1_%2.sql").arg(m15.version, 4, 10, QLatin1Char('0')).arg(m15.name);
    writeSqlFile(migDir.path(), fileName15, m15.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 15);

    // Verify fingerprints table exists and can be queried
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM fingerprints;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstMigrator::upgradesTo0016AddsMbMatches()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 16);

    // Write migrations 1..15
    for (int i = 0; i < 15; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 15);

    // Add migration 16
    const auto &m16 = allMigrations.at(15);
    const QString fileName16
        = QStringLiteral("%1_%2.sql").arg(m16.version, 4, 10, QLatin1Char('0')).arg(m16.name);
    writeSqlFile(migDir.path(), fileName16, m16.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 16);

    // Verify mb_album_matches and mb_track_matches tables exist and can be queried
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM mb_album_matches;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM mb_track_matches;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstMigrator::upgradesTo0017AddsCoverChecked()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 17);

    // Write migrations 1..16
    for (int i = 0; i < 16; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 16);

    // Insert an mb_album_matches row in v16 schema
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO albums (id, grouping_key, title, created_at) "
                                      "VALUES (1, 'g1', 'Album 1', 100);")));
        QVERIFY(
            q.exec(QStringLiteral("INSERT INTO mb_album_matches (album_id, status, release_id, "
                                  "release_group_id, score, label, matched_at) "
                                  "VALUES (1, 'matched', 'rel-1', 'rg-1', 1.0, 'Label', 1000);")));
    }

    // Add migration 17
    const auto &m17 = allMigrations.at(16);
    const QString fileName17
        = QStringLiteral("%1_%2.sql").arg(m17.version, 4, 10, QLatin1Char('0')).arg(m17.name);
    writeSqlFile(migDir.path(), fileName17, m17.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 17);

    // Verify existing row has NULL cover_checked_at
    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("SELECT cover_checked_at FROM mb_album_matches WHERE album_id = 1;")));
    QVERIFY(q.next());
    QVERIFY(q.value(0).isNull());

    // Verify update cover_checked_at works
    QVERIFY(q.exec(
        QStringLiteral("UPDATE mb_album_matches SET cover_checked_at = 2000 WHERE album_id = 1;")));
    QVERIFY(q.exec(
        QStringLiteral("SELECT cover_checked_at FROM mb_album_matches WHERE album_id = 1;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), 2000LL);
}

void TstMigrator::upgradesTo0024AddsAlbumInfoChecks()
{
    const QTemporaryDir migDir;
    QVERIFY(migDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const Migrator defaultMigrator;
    const auto res = defaultMigrator.migrations();
    QVERIFY(res.ok());
    const auto &allMigrations = res.value();
    QVERIFY(allMigrations.size() >= 24);

    // Write migrations 1..23
    for (int i = 0; i < 23; ++i) {
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
    QCOMPARE(Migrator::currentVersion(conn).value(), 23);

    // Insert an album in v23 schema
    {
        QSqlQuery q(conn);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO albums (id, grouping_key, title, created_at) "
                                      "VALUES (1, 'g1', 'Album 1', 100);")));
    }

    // Add migration 24
    const auto &m24 = allMigrations.at(23);
    const QString fileName24
        = QStringLiteral("%1_%2.sql").arg(m24.version, 4, 10, QLatin1Char('0')).arg(m24.name);
    writeSqlFile(migDir.path(), fileName24, m24.sql);

    const Migrator migrator2(migDir.path());
    QVERIFY(migrator2.migrate(conn).ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 24);

    // Verify album_info_checks table exists and supports insert/delete cascade
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO album_info_checks (album_id, checked_at, model, prompt_version) "
        "VALUES (1, 1000, 'gpt-4o', 1);")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM album_info_checks;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    // Cascade delete on albums
    QVERIFY(q.exec(QStringLiteral("DELETE FROM albums WHERE id = 1;")));
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM album_info_checks;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstMigrator)

#include "tst_Migrator.moc"
