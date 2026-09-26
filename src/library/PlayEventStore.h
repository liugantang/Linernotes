// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <core/PlaySource.h>
#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

class Database;

struct PlayEvent {
    qint64 id = -1;
    std::optional<qint64> trackId;
    qint64 startedAtMs = 0;
    std::optional<qint64> endedAtMs; // 进行中为空
    qint64 playedMs = 0;
    qint64 pausedMs = 0;
    std::optional<qint64> durationMs;
    bool completed = false;
    bool skipped = false;
    std::optional<qint64> skipPositionMs;
    core::PlaySource playSource = core::PlaySource::Unknown;
    QString device;
    QString snapshot; // JSON 文本：{"path","title","artist","album"}

    bool operator==(const PlayEvent &other) const = default;
};

class PlayEventStore {
public:
    explicit PlayEventStore(Database &db);

    core::Result<qint64> insert(const PlayEvent &event); // 返回新 id
    core::Result<void> update(const PlayEvent &event); // 按 event.id 更新全部可变字段
    core::Result<QList<PlayEvent>> eventsBetween(
        qint64 fromMs, qint64 toMs) const; // started_at ∈ [from, to)，按 started_at 升序

private:
    Database &m_db;
};

} // namespace linernotes::library
