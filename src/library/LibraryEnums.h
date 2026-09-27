// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <cstdint>

namespace linernotes::library {

Q_NAMESPACE
// 多个枚举有同名值（Title、Year…），QML 中只允许带作用域的写法 Library.SmartField.Title
Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")

/// 这是 library 命名空间唯一的 Q_NAMESPACE，需要反射/暴露给 QML 的 library 枚举都放在这里。

enum class FavoriteKind : std::uint8_t { Track, Album, Artist };
Q_ENUM_NS(FavoriteKind)

enum class TagField : std::uint8_t {
    Title,
    Artist,
    Album,
    AlbumArtist,
    Genre,
    Composer,
    Year,
    TrackNumber,
    TrackTotal,
    DiscNumber,
    DiscTotal
};
Q_ENUM_NS(TagField)

enum class TrackSortKey : std::uint8_t {
    Default,
    Title,
    Artist,
    Album,
    Year,
    Duration,
    DateAdded,
    PlaylistOrder,
};
Q_ENUM_NS(TrackSortKey)

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
Q_ENUM_NS(SmartField)

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
Q_ENUM_NS(SmartOp)

enum class SmartMatch : std::uint8_t { All, Any };
Q_ENUM_NS(SmartMatch)

enum class SmartFieldKind : std::uint8_t { Text, Number, Bool, Date };
Q_ENUM_NS(SmartFieldKind)

} // namespace linernotes::library
