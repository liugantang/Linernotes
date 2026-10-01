// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDate>
#include <QList>

#include <core/Result.h>
#include <library/Database.h>
#include <nlq/NlqQuery.h>
#include <nlq/QueryRunner.h>

#include <cstdint>
#include <optional>

namespace linernotes::nlq {

enum class RelaxKind : std::uint8_t { RemoveCondition, RemovePlayWindow };

struct Relaxation {
    RelaxKind kind = RelaxKind::RemoveCondition;
    int conditionIndex = -1; // RemoveCondition 时有效
    int resultCount = 0; // 放宽后的结果数（最多数到 query.limit）
    bool operator==(const Relaxation &) const = default;
};

struct EmptyResultAnalysis {
    QList<Relaxation> relaxations; // 只列出放宽后结果不为空的，按 resultCount 降序
    std::optional<QDate> firstPlayed; // play_events 最早日期（本地）；无记录为空
    bool windowBeforeHistory = false; // 有播放窗口且窗口结束早于最早播放记录
    bool operator==(const EmptyResultAnalysis &) const = default;
};

[[nodiscard]] core::Result<EmptyResultAnalysis> analyzeEmptyResult(
    library::Database &db, const QueryRunner &runner, const Query &query);

} // namespace linernotes::nlq
