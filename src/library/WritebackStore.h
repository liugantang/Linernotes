// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

struct PlannedFile {
    qint64 fileId = 0;
    QString path;
    QHash<TagField, QString> fields;
    WritebackFileStatus status = WritebackFileStatus::Pending;
    QString error;
    bool operator==(const PlannedFile &) const = default;
};

struct WritebackPlan {
    QList<PlannedFile> files;
    QList<PlannedFile> skippedFiles;
    int skippedCue = 0;
    int skippedUnsupported = 0;
    bool operator==(const WritebackPlan &) const = default;
};

struct WritebackCounts {
    int pending = 0;
    int written = 0;
    int failed = 0;
    int reverted = 0;
    int revertFailed = 0;
    int skipped = 0;
    int total = 0;
    bool operator==(const WritebackCounts &) const = default;
};

struct WritebackFileRecord {
    qint64 writebackId = 0;
    qint64 fileId = 0;
    QString path;
    QHash<TagField, QString> fields;
    QString snapshot;
    WritebackFileStatus status = WritebackFileStatus::Pending;
    QString error;
    qint64 writtenSize = 0;
    qint64 writtenMtime = 0;
    bool operator==(const WritebackFileRecord &) const = default;
};

class WritebackStore {
public:
    WritebackStore(Database &db, const core::Clock &clock);

    [[nodiscard]] core::Result<WritebackPlan> plan(qint64 batchId) const;
    core::Result<qint64> create(qint64 batchId, const WritebackPlan &plan, qint64 now);
    [[nodiscard]] std::optional<qint64> activeWriteback(qint64 batchId) const;

    core::Result<void> saveSnapshot(qint64 wbId, qint64 fileId, const QString &snapshotJson);
    core::Result<void> markWritten(qint64 wbId, qint64 fileId, qint64 size, qint64 mtime);
    core::Result<void> markFailed(
        qint64 wbId, qint64 fileId, WritebackFileStatus status, const QString &error);
    core::Result<void> markReverted(qint64 wbId, qint64 fileId);
    core::Result<void> finishRevert(qint64 wbId, qint64 now);

    [[nodiscard]] core::Result<WritebackCounts> counts(qint64 wbId) const;
    [[nodiscard]] core::Result<WritebackFileRecord> fileRecord(qint64 wbId, qint64 fileId) const;
    [[nodiscard]] core::Result<QList<qint64>> writtenFileIds(qint64 wbId) const;
    [[nodiscard]] core::Result<QStringList> affectedDirectories(qint64 wbId) const;

private:
    Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::library
