// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QtGlobal>

#include <core/Result.h>

#include <optional>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {

struct Candidate {
    qint64 trackId = 0;
    std::optional<qint64> albumId;
    std::optional<qint64> workId;
    QList<qint64> artistIds; // track_artists 中 role='artist'
    std::optional<int> year; // track_sort.year
    bool favorite = false; // 曲目、所属专辑或任一艺人被收藏
    int playCount = 0;
    int skipCount = 0;
    std::optional<qint64> lastPlayedAtMs;
    bool operator==(const Candidate &) const = default;
};

// 全部可推荐曲目：track_sort.visible = 1，排除 duplicate_members.recommended = 0 的曲目。
// 用少量 SQL 一次取完（不要每首一条查询），按 trackId 升序。
[[nodiscard]] core::Result<QList<Candidate>> loadCandidates(library::Database &db);

} // namespace linernotes::rec
