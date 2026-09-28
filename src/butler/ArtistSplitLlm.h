// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <butler/ArtistSplitSource.h>
#include <core/Result.h>
#include <library/CorrectionStore.h>

namespace linernotes::butler {

/// 模板变量：items（每个疑难项：id、原值、上下文专辑、规则建议拆分）
QHash<QString, QString> artistSplitPromptVars(const ArtistSplitGroup &group);

QJsonObject artistSplitSchema(); // 从资源 ":/schemas/cleanup/artist_split.json" 读取

/// 把校验通过的结构化结果转成提议。
/// id 不存在或重复 → 错误；parts 为空或任一部分不是原串子串 → 错误；
/// parts 只有 1 个元素 → 跳过；confidence 截断到 [0,1]；source Llm。
core::Result<QList<library::CorrectionProposal>> parseArtistSplitResult(
    const QJsonValue &value, const ArtistSplitGroup &group);

} // namespace linernotes::butler
