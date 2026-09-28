// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <butler/ArtistCredit.h>
#include <core/Result.h>

namespace linernotes::butler {

/// 变量 {{items}}：每行 "ID <序号>: <值>"，序号从 1 开始
QHash<QString, QString> artistCreditPromptVars(const QStringList &values);

QJsonObject artistCreditSchema(); // ":/schemas/cleanup/artist_credit.json"

/// 返回 值 → 解析结果。按 id 对回 values；id 越界、重复、performers 为空或某个 name trim
/// 后为空的项跳过； 有的 value 没有返回结果也不算错（下次运行再解析）。顶层结构不对才返回错误。
core::Result<QHash<QString, ArtistCredit>> parseArtistCreditResult(
    const QJsonValue &value, const QStringList &values);

} // namespace linernotes::butler
