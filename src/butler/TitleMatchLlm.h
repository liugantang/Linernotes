// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>

#include <butler/TitleMatchSource.h>
#include <butler/TitleMatchStore.h>
#include <core/Result.h>

namespace linernotes::butler {

/// 变量 {{items}}：每行 "ID <序号>: <a> ｜ <b> ｜ 艺人: <artist>"，序号从 1 开始
QHash<QString, QString> titleMatchPromptVars(const QList<TitlePair> &pairs);

QJsonObject titleMatchSchema(); // ":/schemas/cleanup/title_match.json"

/// 按 id 对回 pairs，返回 (keyA, keyB) → TitleMatchVerdict。
/// id 越界/重复、缺字段的项跳过。顶层结构不对才返回错误。
core::Result<QHash<QPair<QString, QString>, TitleMatchVerdict>> parseTitleMatchResult(
    const QJsonValue &value, const QList<TitlePair> &pairs);

} // namespace linernotes::butler
