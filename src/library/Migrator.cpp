// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Migrator.h"

#include "Errors.h"
#include "LibraryLogging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <sqlite3.h>

#include <algorithm>
#include <vector>

namespace linernotes::library {

namespace {

core::Result<void> ensureSchemaVersionTable(const QSqlDatabase &db)
{
    QSqlQuery createTableQuery(db);
    if (!createTableQuery.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version ("
                                              "version INTEGER PRIMARY KEY, "
                                              "name TEXT NOT NULL, "
                                              "applied_at TEXT NOT NULL"
                                              ");"))) {
        return core::Error {
            .code = errc::kDbMigration,
            .message = QStringLiteral("Failed to create schema_version table"),
            .detail = createTableQuery.lastError().text(),
        };
    }
    return { };
}

core::Result<void> backupDatabase(const QSqlDatabase &db, int curVer)
{
    const QString dbPath = db.databaseName();
    if (dbPath.isEmpty() || dbPath == QStringLiteral(":memory:")) {
        return core::Error {
            .code = errc::kDbBackup,
            .message = QStringLiteral("Cannot backup in-memory database"),
            .detail = dbPath,
        };
    }

    const QFileInfo dbFileInfo(dbPath);
    const QDir dbDir = dbFileInfo.dir();
    const QString timestamp
        = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backupFileName
        = QStringLiteral("%1.bak-v%2-%3").arg(dbFileInfo.fileName()).arg(curVer).arg(timestamp);
    const QString backupFilePath = dbDir.filePath(backupFileName);

    QString escapedBackupPath = backupFilePath;
    escapedBackupPath.replace(QLatin1Char('\''), QLatin1String("''"));

    QSqlQuery vacuumQuery(db);
    if (!vacuumQuery.exec(QStringLiteral("VACUUM INTO '%1';").arg(escapedBackupPath))) {
        return core::Error {
            .code = errc::kDbBackup,
            .message = QStringLiteral("Database backup failed before migration"),
            .detail = vacuumQuery.lastError().text(),
        };
    }

    // Keep only latest 3 backups for this database
    struct BackupEntry {
        QFileInfo fileInfo;
        QString timestamp;
        int version { 0 };
    };

    const QString escapedDbFileName = QRegularExpression::escape(dbFileInfo.fileName());
    const QRegularExpression backupRegex(
        QStringLiteral(R"(^%1\.bak-v(\d+)-(\d{8}-\d{6})$)").arg(escapedDbFileName));

    const QFileInfoList allFiles = dbDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    std::vector<BackupEntry> validBackups;

    for (const auto &fileInfo : allFiles) {
        const auto match = backupRegex.match(fileInfo.fileName());
        if (match.hasMatch()) {
            validBackups.push_back(BackupEntry {
                .fileInfo = fileInfo,
                .timestamp = match.captured(2),
                .version = match.captured(1).toInt(),
            });
        }
    }

    std::ranges::sort(validBackups, [](const BackupEntry &a, const BackupEntry &b) {
        if (a.timestamp != b.timestamp) {
            return a.timestamp < b.timestamp;
        }
        return a.version < b.version;
    });

    if (validBackups.size() > 3) {
        const auto toDeleteCount = validBackups.size() - 3;
        for (std::size_t i = 0; i < toDeleteCount; ++i) {
            QFile::remove(validBackups.at(i).fileInfo.absoluteFilePath());
        }
    }

    return { };
}

core::Result<sqlite3 *> getRawSqliteHandle(const QSqlDatabase &db)
{
    const QSqlDriver *driver = db.driver();
    if (driver == nullptr) {
        return core::Error {
            .code = errc::kDbDriver,
            .message = QStringLiteral("Database driver is null"),
            .detail = QString(),
        };
    }

    const QVariant handle = driver->handle();
    if (handle.typeName() == nullptr || std::strcmp(handle.typeName(), "sqlite3*") != 0) {
        return core::Error {
            .code = errc::kDbHandle,
            .message = QStringLiteral("Database driver handle is not sqlite3*"),
            .detail = handle.typeName() != nullptr ? QString::fromUtf8(handle.typeName())
                                                   : QStringLiteral("null"),
        };
    }

    auto *rawDb = *static_cast<sqlite3 *const *>(handle.constData());
    if (rawDb == nullptr) {
        return core::Error {
            .code = errc::kDbHandle,
            .message = QStringLiteral("sqlite3 handle pointer is null"),
            .detail = QString(),
        };
    }

    return rawDb;
}

core::Result<void> applySingleMigration(sqlite3 *rawDb, const Migration &m)
{
    char *errmsg = nullptr;
    int rc = sqlite3_exec(rawDb, "BEGIN IMMEDIATE;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const QString errStr = (errmsg != nullptr) ? QString::fromUtf8(errmsg)
                                                   : QStringLiteral("Failed to begin transaction");
        sqlite3_free(errmsg);
        return core::Error {
            .code = errc::kDbMigration,
            .message = QStringLiteral("Failed to begin transaction for migration %1_%2")
                .arg(m.version, 4, 10, QLatin1Char('0'))
                .arg(m.name),
            .detail = errStr,
        };
    }

    rc = sqlite3_exec(rawDb, m.sql.toUtf8().constData(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const QString errStr = (errmsg != nullptr) ? QString::fromUtf8(errmsg)
                                                   : QStringLiteral("Unknown sqlite error");
        sqlite3_free(errmsg);
        sqlite3_exec(rawDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return core::Error {
            .code = errc::kDbMigration,
            .message = QStringLiteral("Migration failed"),
            .detail = QStringLiteral("迁移 %1_%2: %3")
                .arg(m.version, 4, 10, QLatin1Char('0'))
                .arg(m.name)
                .arg(errStr),
        };
    }

    // Insert into schema_version table
    sqlite3_stmt *stmt = nullptr;
    const char *insertSql
        = "INSERT INTO schema_version (version, name, applied_at) VALUES (?, ?, ?);";
    rc = sqlite3_prepare_v2(rawDb, insertSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        const QString prepErr = QString::fromUtf8(sqlite3_errmsg(rawDb));
        sqlite3_exec(rawDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return core::Error {
            .code = errc::kDbMigration,
            .message = QStringLiteral("Failed to prepare schema_version insert"),
            .detail = prepErr,
        };
    }

    const QString appliedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QByteArray nameUtf8 = m.name.toUtf8();
    const QByteArray appliedAtUtf8 = appliedAt.toUtf8();

    sqlite3_bind_int(stmt, 1, m.version);
    sqlite3_bind_text(
        stmt, 2, nameUtf8.constData(), static_cast<int>(nameUtf8.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, appliedAtUtf8.constData(), static_cast<int>(appliedAtUtf8.size()),
        SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        const QString stepErr = QString::fromUtf8(sqlite3_errmsg(rawDb));
        sqlite3_exec(rawDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return core::Error {
            .code = errc::kDbMigration,
            .message = QStringLiteral("Failed to record migration version into schema_version"),
            .detail = stepErr,
        };
    }

    rc = sqlite3_exec(rawDb, "COMMIT;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const QString commitErr
            = (errmsg != nullptr) ? QString::fromUtf8(errmsg) : QStringLiteral("Commit failed");
        sqlite3_free(errmsg);
        sqlite3_exec(rawDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return core::Error {
            .code = errc::kDbMigration,
            .message = QStringLiteral("Failed to commit migration transaction"),
            .detail = commitErr,
        };
    }

    qCInfo(lcDb) << "Applied migration"
                 << QStringLiteral("%1_%2").arg(m.version, 4, 10, QLatin1Char('0')).arg(m.name);
    return { };
}

core::Result<void> verifyForeignKeys(const QSqlDatabase &db)
{
    QSqlQuery fkQuery(db);
    if (fkQuery.exec(QStringLiteral("PRAGMA foreign_key_check;")) && fkQuery.next()) {
        const QString table = fkQuery.value(0).toString();
        const QString rowid = fkQuery.value(1).toString();
        const QString parent = fkQuery.value(2).toString();
        const QString fkid = fkQuery.value(3).toString();
        const QString detail = QStringLiteral("Violation in table %1, rowid %2, parent %3, fkid %4")
                                   .arg(table, rowid, parent, fkid);
        return core::Error {
            .code = errc::kDbFk,
            .message = QStringLiteral("Foreign key constraint violation detected after migration"),
            .detail = detail,
        };
    }
    return { };
}

} // namespace

Migrator::Migrator(QString migrationsDir)
    : m_migrationsDir(std::move(migrationsDir))
{
}

QString Migrator::migrationsDir() const
{
    return m_migrationsDir;
}

core::Result<QList<Migration>> Migrator::migrations() const
{
    const QDir dir(m_migrationsDir);
    if (!dir.exists()) {
        return QList<Migration> { };
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    const QRegularExpression fileRegex(QStringLiteral(R"(^(\d{4})_(.+)\.sql$)"));

    QList<Migration> list;

    for (const auto &entry : entries) {
        const QString fileName = entry.fileName();
        if (!fileName.endsWith(QStringLiteral(".sql"), Qt::CaseInsensitive)) {
            continue;
        }

        const auto match = fileRegex.match(fileName);
        if (!match.hasMatch()) {
            return core::Error {
                .code = errc::kDbMigrationInvalid,
                .message = QStringLiteral("Invalid migration filename format"),
                .detail = fileName,
            };
        }

        const int version = match.captured(1).toInt();
        const QString name = match.captured(2);

        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return core::Error {
                .code = errc::kDbMigrationInvalid,
                .message = QStringLiteral("Failed to read migration file"),
                .detail = fileName,
            };
        }

        const QString sql = QString::fromUtf8(file.readAll());
        list.append(Migration {
            .version = version,
            .name = name,
            .sql = sql,
        });
    }

    std::ranges::sort(
        list, [](const Migration &a, const Migration &b) { return a.version < b.version; });

    if (list.isEmpty()) {
        return list;
    }

    if (list.first().version != 1) {
        return core::Error {
            .code = errc::kDbMigrationInvalid,
            .message = QStringLiteral("Migrations do not start from version 1"),
            .detail = QStringLiteral("First version is %1").arg(list.first().version),
        };
    }

    for (qsizetype i = 1; i < list.size(); ++i) {
        if (list.at(i).version == list.at(i - 1).version) {
            return core::Error {
                .code = errc::kDbMigrationInvalid,
                .message = QStringLiteral("Duplicate migration version"),
                .detail = QStringLiteral("Version %1 duplicated").arg(list.at(i).version),
            };
        }
        if (list.at(i).version != list.at(i - 1).version + 1) {
            return core::Error {
                .code = errc::kDbMigrationInvalid,
                .message = QStringLiteral("Non-consecutive migration versions"),
                .detail = QStringLiteral("Expected %1, got %2")
                    .arg(list.at(i - 1).version + 1)
                    .arg(list.at(i).version),
            };
        }
    }

    return list;
}

core::Result<int> Migrator::latestVersion() const
{
    const auto migsRes = migrations();
    if (!migsRes) {
        return migsRes.error();
    }
    const auto &migs = migsRes.value();
    if (migs.isEmpty()) {
        return 0;
    }
    return migs.last().version;
}

core::Result<int> Migrator::currentVersion(const QSqlDatabase &db)
{
    if (!db.isOpen()) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    QSqlQuery checkQuery(db);
    if (!checkQuery.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version';"))) {
        return core::Error {
            .code = errc::kDbQuery,
            .message = QStringLiteral("Failed to query schema_version table existence"),
            .detail = checkQuery.lastError().text(),
        };
    }

    if (!checkQuery.next()) {
        return 0;
    }

    QSqlQuery verQuery(db);
    if (!verQuery.exec(QStringLiteral("SELECT MAX(version) FROM schema_version;"))) {
        return core::Error {
            .code = errc::kDbQuery,
            .message = QStringLiteral("Failed to query max version from schema_version"),
            .detail = verQuery.lastError().text(),
        };
    }

    if (!verQuery.next() || verQuery.value(0).isNull()) {
        return 0;
    }

    return verQuery.value(0).toInt();
}

core::Result<void> Migrator::migrate(const QSqlDatabase &db) const
{
    if (!db.isOpen()) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("Database is not open"),
            .detail = QStringLiteral("%1: Database is not open").arg(db.databaseName()),
        };
    }

    const auto migsRes = migrations();
    if (!migsRes) {
        return migsRes.error();
    }
    const auto &migs = migsRes.value();
    const int targetVersion = migs.isEmpty() ? 0 : migs.last().version;

    const auto schemaRes = ensureSchemaVersionTable(db);
    if (!schemaRes) {
        return schemaRes.error();
    }

    const auto curVerRes = currentVersion(db);
    if (!curVerRes) {
        return curVerRes.error();
    }
    const int curVer = curVerRes.value();

    if (curVer > targetVersion) {
        return core::Error {
            .code = errc::kDbTooNew,
            .message = QStringLiteral("Database schema version is newer than available migrations"),
            .detail = QStringLiteral("Database is version %1, but latest migration is %2")
                .arg(curVer)
                .arg(targetVersion),
        };
    }

    if (curVer == targetVersion) {
        return { };
    }

    if (curVer > 0) {
        const auto backupRes = backupDatabase(db, curVer);
        if (!backupRes) {
            return backupRes.error();
        }
    }

    const auto rawDbRes = getRawSqliteHandle(db);
    if (!rawDbRes) {
        return rawDbRes.error();
    }
    sqlite3 *rawDb = rawDbRes.value();

    for (const auto &m : migs) {
        if (m.version <= curVer) {
            continue;
        }

        const auto applyRes = applySingleMigration(rawDb, m);
        if (!applyRes) {
            return applyRes.error();
        }
    }

    return verifyForeignKeys(db);
}

} // namespace linernotes::library
