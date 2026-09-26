// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QVariant>

#include <library/SmartRule.h>

namespace linernotes::library::detail {

/// 将 SmartRule 的条件转化为适用于 track_sort WHERE 子句的 SQL 条件片段。
/// 返回以 " AND " 开头的条件字符串，并将参数绑定追加至 binds。
/// 若 rule.conditions 为空，返回空 QString。
QString buildSmartRuleWhereSql(const SmartRule &rule, QList<QVariant> &binds);

/// 转义 LIKE 模糊匹配中的特殊字符 (% _ \)
QString escapeLikePattern(const QString &input);

} // namespace linernotes::library::detail
