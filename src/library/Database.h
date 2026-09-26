// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QSqlDatabase>
#include <QString>

#include <core/Result.h>

#include <atomic>
#include <cstdint>

namespace linernotes::library {

class Migrator;

/// 一个 SQLite 数据库文件。每个线程通过 connection() 拿到自己独立的连接（QSqlDatabase
/// 不能跨线程使用）。线程安全：connection() 可在任意线程调用；返回的连接只能在调用线程使用。
/// 约定：调用方应在 Database 析构前结束使用它的后台线程。Database 析构时仅处理本线程的连接；
/// 其他线程的连接在其线程退出时由 thread_local 清理。
class Database {
public:
    explicit Database(QString filePath);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&) = delete;
    Database &operator=(Database &&) = delete;

    QString filePath() const;

    /// 返回当前线程的连接，首次调用时打开并应用 PRAGMA；失败返回 Error{"db.open", ...}。
    core::Result<QSqlDatabase> connection();

    /// 打开（本线程连接）+ 执行迁移。应用启动时在主线程调用一次。
    core::Result<void> open(const Migrator &migrator);

private:
    core::Result<QSqlDatabase> createConnection(const QString &connName);

    QString m_filePath;
    quint64 m_instanceId { 0 };
    static std::atomic<quint64> s_instanceCounter;

    std::atomic_flag m_loggedFirstOpen = ATOMIC_FLAG_INIT;
};

/// RAII 事务：构造时 BEGIN（写事务用 BEGIN IMMEDIATE），析构时若未 commit() 则 ROLLBACK。
class Transaction {
public:
    enum class Mode : std::uint8_t { Deferred, Immediate };
    explicit Transaction(const QSqlDatabase &db, Mode mode = Mode::Immediate);
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    Transaction(Transaction &&) = delete;
    Transaction &operator=(Transaction &&) = delete;

    bool isActive() const;
    core::Result<void> commit();
    void rollback();

private:
    QSqlDatabase m_db;
    bool m_active { false };
};

} // namespace linernotes::library
