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

/// 返回 值 → 解析结果。按 id 对回 values；未在返回中出现的项视为“原样”（performers 为原值、aka 与
/// roles 为空、置信度 1.0、理由为空）； id 越界、重复、performers 为空或 name
/// 为空的非法项跳过、不记结果（下次重试）。顶层结构不对才返回错误。
core::Result<QHash<QString, ArtistCredit>> parseArtistCreditResult(
    const QJsonValue &value, const QStringList &values);

} // namespace linernotes::butler
