// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

class QSqlQuery;

namespace linernotes::library {

struct PlayCountRule {
    int minPercent = 50; // 1..100
    int minSeconds = 240; // 0 表示不启用时长条件；上限 3600

    /// 规则：playedMs ≥ minSeconds*1000（minSeconds > 0 时），
    ///       或 durationMs 已知且 > 0 时 playedMs*100 ≥ minPercent*durationMs。
    [[nodiscard]] bool counts(qint64 playedMs, std::optional<qint64> durationMs) const;

    /// 同一规则的 SQL 条件，列名固定为 play_events 的 played_ms / track_duration_ms，
    /// 使用命名参数 :count_min_ms 与 :count_min_percent；配合 bindSql() 绑定。
    /// 4.3 的统计查询复用它，保证 C++ 与 SQL 两处判定一致。
    /// 注意：SmartRuleSql.cpp 中播放窗口统计实现了位置参数版本的相同逻辑，两处需保持一致。
    [[nodiscard]] static QString sqlCondition();
    void bindSql(QSqlQuery &query) const;

    /// 夹到合法范围后的副本。
    [[nodiscard]] PlayCountRule normalized() const;

    bool operator==(const PlayCountRule &) const = default;
};

} // namespace linernotes::library
