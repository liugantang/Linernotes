// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Database.h"

#include "Errors.h"
#include "LibraryLogging.h"
#include "Migrator.h"

#include <QByteArrayView>
#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <library/EnumNames.h>
#include <library/TrackLanguage.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace linernotes::library {

namespace {

struct ThreadLocalConnectionCleaner {
    std::vector<QString> connectionNames;

    ThreadLocalConnectionCleaner() = default;
    ~ThreadLocalConnectionCleaner()
    {
        for (const QString &name : connectionNames) {
            if (QSqlDatabase::contains(name)) {
                {
                    QSqlDatabase db = QSqlDatabase::database(name, false);
                    if (db.isOpen()) {
                        db.close();
                    }
                }
                QSqlDatabase::removeDatabase(name);
            }
        }
    }

    ThreadLocalConnectionCleaner(const ThreadLocalConnectionCleaner &) = delete;
    ThreadLocalConnectionCleaner &operator=(const ThreadLocalConnectionCleaner &) = delete;
    ThreadLocalConnectionCleaner(ThreadLocalConnectionCleaner &&) = delete;
    ThreadLocalConnectionCleaner &operator=(ThreadLocalConnectionCleaner &&) = delete;

    void add(const QString &name)
    {
        if (std::ranges::find(connectionNames, name) == connectionNames.end()) {
            connectionNames.push_back(name);
        }
    }

    void remove(const QString &name) { std::erase(connectionNames, name); }
};

ThreadLocalConnectionCleaner &threadConnectionCleaner()
{
    static thread_local ThreadLocalConnectionCleaner s_cleaner;
    return s_cleaner;
}

QString currentThreadHexId()
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
}

core::Result<void> ensureParentDirectory(const QString &filePath)
{
    if (filePath.isEmpty() || filePath == QStringLiteral(":memory:")) {
        return { };
    }

    const QFileInfo fileInfo(filePath);
    if (fileInfo.isDir()) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("Database path is a directory"),
            .detail = QStringLiteral("%1: Path is a directory").arg(filePath),
        };
    }

    const QDir dir = fileInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("Failed to create database directory"),
            .detail
            = QStringLiteral("%1: Failed to create directory %2").arg(filePath, dir.absolutePath()),
        };
    }

    return { };
}

core::Result<void> checkFts5Support(const QSqlDatabase &db, const QString &filePath)
{
    if (sqlite3_compileoption_used("ENABLE_FTS5") != 0
        || sqlite3_compileoption_used("SQLITE_ENABLE_FTS5") != 0) {
        return { };
    }

    QSqlQuery probeQuery(db);
    if (probeQuery.exec(QStringLiteral("CREATE VIRTUAL TABLE temp._fts5_probe USING fts5(a);"))) {
        probeQuery.exec(QStringLiteral("DROP TABLE temp._fts5_probe;"));
        return { };
    }

    return core::Error {
        .code = errc::kDbFts5,
        .message = QStringLiteral("SQLite FTS5 extension is not available"),
        .detail = filePath,
    };
}

struct PragmaCommand {
    const char *sql;
    const char *name;
};

core::Result<void> applyPragmas(const QSqlDatabase &db, const QString &filePath)
{
    // PRAGMA journal_mode = WAL;
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("PRAGMA journal_mode = WAL;")) || !q.next()) {
            return core::Error {
                .code = errc::kDbOpen,
                .message = QStringLiteral("Failed to set PRAGMA journal_mode = WAL"),
                .detail = QStringLiteral("%1: %2").arg(filePath, q.lastError().text()),
            };
        }
        const QString mode = q.value(0).toString().trimmed();
        if (mode.compare(QStringLiteral("wal"), Qt::CaseInsensitive) != 0) {
            return core::Error {
                .code = errc::kDbOpen,
                .message = QStringLiteral("Failed to set journal_mode to WAL"),
                .detail = QStringLiteral("%1: Result was: %2").arg(filePath, mode),
            };
        }
    }

    static constexpr std::array<PragmaCommand, 6> kPragmas = { {
        { .sql = "PRAGMA synchronous = NORMAL;", .name = "synchronous" },
        { .sql = "PRAGMA foreign_keys = ON;", .name = "foreign_keys" },
        { .sql = "PRAGMA busy_timeout = 5000;", .name = "busy_timeout" },
        { .sql = "PRAGMA temp_store = MEMORY;", .name = "temp_store" },
        { .sql = "PRAGMA cache_size = -20000;", .name = "cache_size" },
        // 不用 mmap：每个连接各映射一份库文件，读过的页全部计入进程内存；走系统页缓存即可
        { .sql = "PRAGMA mmap_size = 0;", .name = "mmap_size" },
    } };

    for (const auto &pragma : kPragmas) {
        QSqlQuery q(db);
        if (!q.exec(QLatin1String(pragma.sql))) {
            return core::Error {
                .code = errc::kDbOpen,
                .message
                = QStringLiteral("Failed to set PRAGMA %1").arg(QLatin1String(pragma.name)),
                .detail = QStringLiteral("%1: %2").arg(filePath, q.lastError().text()),
            };
        }
    }

    return { };
}

QString extractSqliteValueString(sqlite3_value *val)
{
    if (val == nullptr) {
        return { };
    }
    const auto *ptr = sqlite3_value_text(val);
    if (ptr == nullptr) {
        return { };
    }
    return QString::fromUtf8(QByteArrayView(ptr, sqlite3_value_bytes(val)));
}

void linernotesLangScalarFunc(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) -- C API boundary: wrapping
    // argv in span
    const std::span<sqlite3_value *> args(argv, static_cast<std::size_t>(argc));
    auto it = args.begin();
    const QString title = extractSqliteValueString(*it);
    ++it;
    const QString album = extractSqliteValueString(*it);
    ++it;
    const QString artist = extractSqliteValueString(*it);

    const TrackLanguage lang = inferTrackLanguage(title, album, artist);
    const QString langStr = trackLanguageToString(lang);
    const QByteArray utf8 = langStr.toUtf8();
    sqlite3_result_text(ctx, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
}

core::Result<void> registerCustomFunctions(const QSqlDatabase &db, const QString &filePath)
{
    const QVariant handleVariant = db.driver()->handle();
    if (!handleVariant.isValid() || qstrcmp(handleVariant.typeName(), "sqlite3*") != 0) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("Failed to retrieve sqlite3 handle from QSqlDriver"),
            .detail = filePath,
        };
    }

    auto *sqliteHandle = *static_cast<sqlite3 *const *>(handleVariant.constData());
    if (sqliteHandle == nullptr) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("sqlite3 handle is null"),
            .detail = filePath,
        };
    }

    const int rc = sqlite3_create_function(sqliteHandle, "linernotes_lang", 3,
        SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, &linernotesLangScalarFunc, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        return core::Error {
            .code = errc::kDbOpen,
            .message = QStringLiteral("Failed to register SQLite function linernotes_lang"),
            .detail
            = QStringLiteral("%1: sqlite3_create_function returned %2").arg(filePath).arg(rc),
        };
    }

    return { };
}

} // namespace

std::atomic<quint64> Database::s_instanceCounter { 0 };

Database::Database(QString filePath)
    : m_filePath(std::move(filePath))
    , m_instanceId(++s_instanceCounter)
{
}

Database::~Database()
{
    const QString myConnName
        = QStringLiteral("linernotes-db-%1-%2").arg(m_instanceId).arg(currentThreadHexId());

    if (QSqlDatabase::contains(myConnName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(myConnName, false);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(myConnName);
        threadConnectionCleaner().remove(myConnName);
    }
}

QString Database::filePath() const
{
    return m_filePath;
}

bool Database::isOpen() const
{
    return m_open.load(std::memory_order_acquire);
}

core::Result<QSqlDatabase> Database::connection()
{
    const QString connName
        = QStringLiteral("linernotes-db-%1-%2").arg(m_instanceId).arg(currentThreadHexId());

    if (QSqlDatabase::contains(connName)) {
        bool isValidAndOpen = false;
        {
            const QSqlDatabase db = QSqlDatabase::database(connName, false);
            isValidAndOpen = (db.isValid() && db.isOpen());
        }
        if (isValidAndOpen) {
            return QSqlDatabase::database(connName, false);
        }
        // Connection is closed/invalid; ensure local db was destroyed before removeDatabase
        QSqlDatabase::removeDatabase(connName);
    }

    return createConnection(connName);
}

core::Result<QSqlDatabase> Database::createConnection(const QString &connName)
{
    const auto dirRes = ensureParentDirectory(m_filePath);
    if (!dirRes) {
        return dirRes.error();
    }

    auto setupConn = [this, &connName]() -> core::Result<void> {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(m_filePath);

        if (!db.open()) {
            const QString err = db.lastError().text();
            return core::Error {
                .code = errc::kDbOpen,
                .message = QStringLiteral("Failed to open SQLite database"),
                .detail = QStringLiteral("%1: %2").arg(m_filePath, err),
            };
        }

        if (!m_loggedFirstOpen.test_and_set()) {
            qCInfo(lcDb) << "SQLite version:" << sqlite3_libversion()
                         << "Database file:" << m_filePath;
        }

        const auto ftsRes = checkFts5Support(db, m_filePath);
        if (!ftsRes) {
            db.close();
            return ftsRes.error();
        }

        const auto pragmaRes = applyPragmas(db, m_filePath);
        if (!pragmaRes) {
            db.close();
            return pragmaRes.error();
        }

        const auto customFuncRes = registerCustomFunctions(db, m_filePath);
        if (!customFuncRes) {
            db.close();
            return customFuncRes.error();
        }

        return { };
    };

    const auto setupRes = setupConn();
    if (!setupRes) {
        if (QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
        return setupRes.error();
    }

    threadConnectionCleaner().add(connName);
    return QSqlDatabase::database(connName, false);
}

core::Result<void> Database::open(const Migrator &migrator)
{
    const auto connRes = connection();
    if (!connRes) {
        return connRes.error();
    }
    const auto migrateRes = migrator.migrate(connRes.value());
    if (!migrateRes) {
        return migrateRes.error();
    }
    m_open.store(true, std::memory_order_release);
    return { };
}

Transaction::Transaction(const QSqlDatabase &db, Mode mode)
    : m_db(db)
{
    if (!m_db.isOpen()) {
        qCWarning(lcDb) << "Transaction failed to begin: database is not open";
        return;
    }

    QSqlQuery q(m_db);
    const QString sql = (mode == Mode::Immediate) ? QStringLiteral("BEGIN IMMEDIATE;")
                                                  : QStringLiteral("BEGIN DEFERRED;");
    if (q.exec(sql)) {
        m_active = true;
    } else {
        qCWarning(lcDb) << "Transaction failed to begin:" << q.lastError().text();
    }
}

Transaction::~Transaction()
{
    if (m_active) {
        rollback();
    }
}

bool Transaction::isActive() const
{
    return m_active;
}

core::Result<void> Transaction::commit()
{
    if (!m_active) {
        return core::Error {
            .code = errc::kDbTransaction,
            .message = QStringLiteral("No active transaction to commit"),
            .detail = QString(),
        };
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("COMMIT;"))) {
        const QString err = q.lastError().text();
        return core::Error {
            .code = errc::kDbTransaction,
            .message = QStringLiteral("Failed to commit transaction"),
            .detail = err,
        };
    }
    m_active = false;
    return { };
}

void Transaction::rollback()
{
    if (m_active) {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("ROLLBACK;"));
        m_active = false;
    }
}

} // namespace linernotes::library
