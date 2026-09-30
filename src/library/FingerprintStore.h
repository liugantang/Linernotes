// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <core/Result.h>

#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

struct StoredFingerprint {
    qint64 fileId = 0;
    int algorithm = 0;
    QList<quint32> items;
    bool operator==(const StoredFingerprint &) const = default;
};

class FingerprintStore {
public:
    FingerprintStore(Database &db, const core::Clock &clock);

    /// 需要计算的文件：未缺失（missing_since IS NULL）、无 scan_error、至少有一条 tracks 行，
    /// 且 fingerprints 中没有记录或记录的 content_hash 与 files.content_hash 不同（NULL
    /// 视为不同）。按 file_id 升序。
    [[nodiscard]] core::Result<QList<qint64>> pendingFileIds() const;

    /// 文件不存在返回错误
    [[nodiscard]] core::Result<QString> filePath(qint64 fileId) const;

    /// 同时写入当前 content_hash，INSERT OR REPLACE
    core::Result<void> save(qint64 fileId, int algorithm, const QList<quint32> &items);

    /// 失败也写记录（避免反复重试），content_hash 同上
    core::Result<void> saveFailure(qint64 fileId, const QString &error);

    /// 只返回成功的记录
    [[nodiscard]] core::Result<std::optional<StoredFingerprint>> load(qint64 fileId) const;

    /// 只返回成功且未过期的记录（7.8 用）
    [[nodiscard]] core::Result<QList<StoredFingerprint>> loadAll() const;

private:
    Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::library
