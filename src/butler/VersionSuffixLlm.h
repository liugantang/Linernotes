// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <butler/VersionSuffixSource.h>
#include <butler/VersionSuffixStore.h>
#include <core/Result.h>

namespace linernotes::butler {

/// 变量 {{items}}：每行 "ID <序号>: <后缀> ｜ 完整标题: <标题>"，序号从 1 开始
QHash<QString, QString> versionSuffixPromptVars(const QList<SuffixSample> &samples);

QJsonObject versionSuffixSchema(); // ":/schemas/cleanup/version_suffix.json"

/// 按 id 对回 samples，返回 suffix_key → SuffixVerdict。
/// id 越界/重复、role 非法、role=version 但 type 缺失或非法
/// 的项跳过（下次重试）。顶层结构不对才返回错误。
core::Result<QHash<QString, SuffixVerdict>> parseVersionSuffixResult(
    const QJsonValue &value, const QList<SuffixSample> &samples);

} // namespace linernotes::butler
