// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <core/Result.h>

namespace linernotes::library {

struct Migration {
    int version { 0 }; // 文件名中的编号 NNNN
    QString name; // 文件名中的描述部分
    QString sql; // 脚本全文

    bool operator==(const Migration &other) const = default;
};

class Migrator {
public:
    /// 从目录（默认 ":/migrations"，即 Qt 资源）加载 NNNN_<描述>.sql
    explicit Migrator(QString migrationsDir = QStringLiteral(":/migrations"));

    /// 按编号排序；校验：文件名格式、编号从 1 开始连续且不重复；不合法返回
    /// Error{"db.migration.invalid", ...}
    core::Result<QList<Migration>> migrations() const;

    /// 目标版本 = 最后一个迁移的编号（无迁移时为 0）
    core::Result<int> latestVersion() const;

    /// 读取数据库当前版本（schema_version 表不存在视为 0）
    static core::Result<int> currentVersion(const QSqlDatabase &db);

    /// 把数据库升级到最新版本。
    core::Result<void> migrate(const QSqlDatabase &db) const;

    QString migrationsDir() const;

private:
    QString m_migrationsDir;
};

} // namespace linernotes::library
