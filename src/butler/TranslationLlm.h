// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <core/Result.h>

namespace linernotes::butler {

/// 变量 {{items}}：每行 "ID <序号>: <文本>"，序号从 1 开始
QHash<QString, QString> translationPromptVars(const QStringList &texts);

QJsonObject translationSchema(); // ":/schemas/cleanup/translate_titles.json"

/// 按 id 对回 texts，返回 原文 → 译文。
/// id 越界/重复跳过。顶层结构不对才返回错误。
core::Result<QHash<QString, QString>> parseTranslationResult(
    const QJsonValue &value, const QStringList &texts);

} // namespace linernotes::butler
