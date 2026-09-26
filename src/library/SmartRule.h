// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <Qt>

#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

enum class TrackSortKey : std::uint8_t;

enum class SmartField : std::uint8_t {
    Title,
    Artist,
    Album,
    AlbumArtist,
    Genre,
    Codec, // 文本
    Year,
    Rating,
    DurationSec, // 数值
    Favorite, // 布尔
    DateAdded // 日期（files.first_seen_at）
};

enum class SmartOp : std::uint8_t {
    Contains,
    NotContains,
    Is,
    IsNot,
    StartsWith, // 文本
    Equals,
    NotEquals,
    Greater,
    Less,
    Between, // 数值
    IsTrue,
    IsFalse, // 布尔
    InLastDays,
    NotInLastDays // 日期
};

struct SmartCondition {
    SmartField field = SmartField::Title;
    SmartOp op = SmartOp::Contains;
    QVariant value; // 文本：QString；数值/天数：数字；Between 的下界
    QVariant value2; // 仅 Between 的上界（含）
    bool operator==(const SmartCondition &) const = default;
};

struct SmartRule {
    enum class Match : std::uint8_t { All, Any };
    Match match = Match::All;
    QList<SmartCondition> conditions; // 为空表示匹配全部曲目
    TrackSortKey sortKey = static_cast<TrackSortKey>(0);
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    std::optional<int> limit; // 例：“最近添加的 50 首”= DateAdded 降序 + limit 50
    bool operator==(const SmartRule &) const = default;

    [[nodiscard]] QString toJson() const; // 存入 playlists.rule
    static core::Result<SmartRule> fromJson(const QString &json);
};

/// 该字段允许的运算符（UI 编辑器据此填下拉框；fromJson 据此校验）
QList<SmartOp> smartOpsFor(SmartField field);

} // namespace linernotes::library
