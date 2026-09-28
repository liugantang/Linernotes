// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <butler/MojibakeAnalysis.h>
#include <butler/MojibakeSource.h>
#include <core/Result.h>
#include <library/CorrectionStore.h>

namespace linernotes::butler {

/// 模板变量：directory、context（组内可读字段，最多 20
/// 行）、items（每个疑难项：id、字段、原值、候选编码/文本/得分）
QHash<QString, QString> mojibakePromptVars(
    const MojibakeGroup &group, const QList<AmbiguousItem> &items);

QJsonObject mojibakeSchema(); // 从资源 ":/schemas/cleanup/mojibake.json" 读取

/// 把校验通过的结构化结果转成提议。id 不存在或重复 → 错误；text 为 null 或与原值相同 → 跳过；
/// confidence 截断到 [0,1]；source Llm。
core::Result<QList<library::CorrectionProposal>> parseMojibakeResult(
    const QJsonValue &value, const QList<AmbiguousItem> &items);

/// 不使用 LLM 时的退路：取每项第一候选，confidence = min(候选得分, 0.5)，reason 注明“未经 AI
/// 确认”。
QList<library::CorrectionProposal> fallbackProposals(const QList<AmbiguousItem> &items);

} // namespace linernotes::butler
