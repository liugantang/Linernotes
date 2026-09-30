// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

/// 把文件移入回收站的接口：生产用 SystemFileTrash（QFile::moveToTrash）。
class FileTrash {
public:
    FileTrash() = default;
    virtual ~FileTrash() = default;
    Q_DISABLE_COPY_MOVE(FileTrash)

    virtual core::Result<void> moveToTrash(const QString &path) = 0;
};

class SystemFileTrash final : public FileTrash {
public:
    SystemFileTrash() = default;
    ~SystemFileTrash() override = default;
    Q_DISABLE_COPY_MOVE(SystemFileTrash)

    core::Result<void> moveToTrash(const QString &path) override;
};

struct ResolveOutcome {
    QList<qint64> removedTrackIds;
    QStringList failedPaths; // 移入回收站失败的文件（这些曲目保持原样，不迁移、不删除）
    bool operator==(const ResolveOutcome &) const = default;
};

/// 重复曲目处理：
/// 保留指定的 keepTrackId，组内其余成员的文件移入系统回收站，用户数据合并到 keepTrackId。
/// 注意：本操作不提供软件内撤销功能。被删除的文件移入了系统回收站，用户可自行从回收站还原；
/// 还原后若重新扫描音乐库，将作为新曲目入库。
class DuplicateResolver {
public:
    DuplicateResolver(library::Database &db, FileTrash &trash, const core::Clock &clock);

    /// 保留 keepTrackId，处理组内其余成员：对每个成员先移动文件到回收站，成功后
    /// mergeTrackInto(成员, keep)。
    /// 一个成员失败不影响其他成员。成员全部处理成功后删除该组（duplicate_groups
    /// 行）；有失败则保留组。 cue 分轨（同一文件的多条
    /// tracks）不会出现在组里，不用处理。keepTrackId 不在组内 → 错误。
    /// 可在工作线程调用（使用本线程的 connection()）。
    core::Result<ResolveOutcome> resolve(qint64 groupId, qint64 keepTrackId) const;

    /// 组内所有两两组合写入 duplicate_dismissals，然后删除该组。
    core::Result<void> dismiss(qint64 groupId) const;

private:
    library::Database &m_db;
    FileTrash &m_trash;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
