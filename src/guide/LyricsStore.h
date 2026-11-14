// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Clock.h>
#include <core/Result.h>
#include <library/Database.h>

#include <cstdint>
#include <optional>

namespace linernotes::guide {

struct StoredLyrics {
    enum class Kind : std::uint8_t { Manual, Lrclib, NotFound };
    Kind kind = Kind::NotFound;
    QString text { };
    bool synced = false;
    bool instrumental = false;
    std::optional<qint64> lrclibId;
    qint64 fetchedAtMs = 0;
    bool operator==(const StoredLyrics &) const = default;
};

class LyricsStore {
public:
    explicit LyricsStore(library::Database &db, const core::Clock &clock);

    core::Result<std::optional<StoredLyrics>> get(qint64 trackId) const;
    core::Result<void> saveManual(
        qint64 trackId, const QString &text) const; // text 为空 = 删除手动歌词（删整行）
    core::Result<void> saveLrclib(
        qint64 trackId, const QString &text, bool synced, bool instrumental, qint64 lrclibId) const;
    core::Result<void> saveNotFound(qint64 trackId) const;
    core::Result<void> removeOnline(qint64 trackId) const; // 删除 lrclib / none 行，保留 manual

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::guide
