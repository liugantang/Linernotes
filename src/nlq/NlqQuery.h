// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <Qt>

#include <core/Result.h>
#include <library/SmartRule.h>

#include <cstdint>
#include <optional>

namespace linernotes::nlq {

Q_NAMESPACE
Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")

enum class Entity : std::uint8_t { Track, Album, Artist };
Q_ENUM_NS(Entity)

enum class SortKey : std::uint8_t {
    Default,
    PlayCount,
    LastPlayed,
    Rating,
    Year,
    DateAdded,
    Duration,
    Random
};
Q_ENUM_NS(SortKey)

[[nodiscard]] QString entityToString(Entity entity);
[[nodiscard]] std::optional<Entity> entityFromString(QStringView name);

[[nodiscard]] QString sortKeyToString(SortKey sortKey);
[[nodiscard]] std::optional<SortKey> sortKeyFromString(QStringView name);

struct SimilarTo {
    bool current = false; // true：以“正在播放的歌”为种子
    QStringList titles; // 种子曲名的多种写法（同名称多写法规则）
    QStringList artists; // 可选：种子艺人的多种写法，用于消歧
    bool operator==(const SimilarTo &) const = default;
};

struct Query {
    Entity entity = Entity::Track;
    std::optional<SimilarTo> similarTo;
    library::SmartRule
        rule; // 只用 match / conditions / playedFrom / playedTo；rule.sortKey / rule.limit 忽略
    SortKey sortKey = SortKey::Default;
    Qt::SortOrder sortOrder = Qt::DescendingOrder;
    int limit = 50; // 1..500
    bool operator==(const Query &) const = default;

    [[nodiscard]] QJsonObject toJson() const;
    static core::Result<Query> fromJson(const QJsonObject &obj);
    [[nodiscard]] core::Result<void> validate() const;
};

} // namespace linernotes::nlq
