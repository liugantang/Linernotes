// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <core/Result.h>

namespace linernotes::library {

class Database;

struct LibraryRoot {
    qint64 id = 0;
    QString path; // 规范化的绝对路径（QDir::cleanPath，无结尾 /）
    bool enabled = true;
    QStringList excludes; // glob 模式
};

class LibraryRoots {
public:
    explicit LibraryRoots(Database &db); // 在调用线程使用 db.connection()
    core::Result<LibraryRoot> add(const QString &path, const QStringList &excludes = { });
    core::Result<void> remove(qint64 id); // 级联删除该根下的 files（表上已有 ON DELETE CASCADE）
    core::Result<void> setEnabled(qint64 id, bool enabled);
    core::Result<void> setExcludes(qint64 id, const QStringList &excludes);
    core::Result<QList<LibraryRoot>> list() const; // 按 id 排序

private:
    Database &m_db;
};

} // namespace linernotes::library
