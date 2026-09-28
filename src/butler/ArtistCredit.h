// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace linernotes::butler {

struct CreditPerformer {
    QString name { }; // 标签原文写法
    QStringList aka; // 其他写法，可为空
    bool operator==(const CreditPerformer &) const = default;
};

struct ArtistCredit {
    QList<CreditPerformer> performers; // 至少 1 项
    QStringList roles; // 角色/标注，只记录
    double confidence = 0.0;
    QString reason { };
    bool operator==(const ArtistCredit &) const = default;
};

QJsonObject toJson(const ArtistCredit &credit);
std::optional<ArtistCredit> artistCreditFromJson(
    const QJsonObject &obj); // 字段缺失/类型错/performers 为空 → nullopt

/// performers 的 name 用 " / " 连接。
QString normalizedValue(const ArtistCredit &credit);

} // namespace linernotes::butler
