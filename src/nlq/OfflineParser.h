// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

#include <nlq/NlqQuery.h>

#include <optional>

namespace linernotes::nlq {

struct OfflineParse {
    Query query;
    QString leftover; // 去掉已识别的关键词与虚词后剩下的文字（trim 后）
    bool matchedRule = false; // 至少识别出一条规则
    QStringList matchedTags;
    bool operator==(const OfflineParse &) const = default;
};

/// base
/// 不为空时在其基础上修改（追问）：新识别的条件追加（同字段同运算符的替换），实体/排序/数量/窗口如识别到则覆盖。
[[nodiscard]] OfflineParse parseOffline(
    const QString &text, QDate today, const std::optional<Query> &base = std::nullopt);

} // namespace linernotes::nlq
