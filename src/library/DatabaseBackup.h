// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDateTime>
#include <QString>

#include <core/Result.h>

#include <optional>
#include <vector>

namespace linernotes::library {

class Database;

class DatabaseBackup {
public:
    struct Options {
        QString backupDir; // <dataDir>/backups
        int keep = 7; // 1..60
        qint64 intervalMs = 24LL * 3600 * 1000;
    };

    DatabaseBackup(Database &db, Options options);

    /// 最新备份的时间（按文件名解析，没有备份返回 nullopt）
    [[nodiscard]] std::optional<QDateTime> latestBackupTime() const;
    /// 距最新备份已超过 intervalMs（或没有备份）时返回 true
    [[nodiscard]] bool isDue(const QDateTime &now) const;
    /// 立即备份：在调用线程上用该线程自己的连接执行 `VACUUM INTO :path`，
    /// 先写到 `library-<yyyyMMdd-HHmmss>.db.tmp`，成功后改名为 `.db`（失败时删除 .tmp），
    /// 然后删除超出 keep 的最旧备份。返回新备份的路径。
    core::Result<QString> backupNow(const QDateTime &now);

private:
    struct BackupEntry {
        QString filePath;
        QString fileName;
        QDateTime time;
    };

    [[nodiscard]] std::vector<BackupEntry> listBackups() const;

    Database &m_db;
    Options m_options;
};

} // namespace linernotes::library
