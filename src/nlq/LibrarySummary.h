// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>

#include <core/Result.h>
#include <library/Database.h>
#include <library/LibraryEnums.h>

#include <optional>

namespace linernotes::nlq {

struct LibrarySummary {
    QDate today;
    QString timeZoneId; // QTimeZone::systemTimeZoneId()
    int trackCount = 0;
    int albumCount = 0;
    int artistCount = 0;
    std::optional<int> minYear, maxYear;
    std::optional<QDate> firstPlayed, lastPlayed; // play_events 的最早/最晚 started_at（本地日期）
    int playEventCount = 0;
    QList<QPair<QString, int>> topGenres; // 按曲目数降序，最多 20 个，忽略空流派
    QList<QPair<library::TrackLanguage, int>> languages; // 用 linernotes_lang() 统计，降序
    QList<QPair<library::VersionType, int>> versionTypes; // track_versions，无记录计为 studio，降序
    bool operator==(const LibrarySummary &) const = default;
};

/// 从数据库统计（只统计 visible 曲目，参照 track_sort.visible）。today /
/// 时区由调用方传入（不要在内部取当前时间）。
[[nodiscard]] core::Result<LibrarySummary> buildLibrarySummary(
    library::Database &db, QDate today, const QString &timeZoneId);

/// 渲染为给 LLM 的中文纯文本（提示词变量 {{library_summary}} 的值）。
[[nodiscard]] QString renderLibrarySummary(const LibrarySummary &summary);

} // namespace linernotes::nlq
