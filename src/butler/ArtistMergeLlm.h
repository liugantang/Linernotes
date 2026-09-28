// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <butler/ArtistCluster.h>
#include <core/Result.h>
#include <library/ArtistAliasCorrections.h>

#include <cstdint>

namespace linernotes::butler {

struct ArtistMergeCandidatePair {
    int id = 0;
    ArtistEntry artistA;
    QStringList albumsA;
    ArtistEntry artistB;
    QStringList albumsB;

    bool operator==(const ArtistMergeCandidatePair &) const = default;
};

/// 模板变量：pairs（每个候选对：id、两边的名字、曲目数、示例专辑）
QHash<QString, QString> artistMergePromptVars(const QList<ArtistMergeCandidatePair> &pairs);

QJsonObject artistMergeSchema(); // 从资源 ":/schemas/cleanup/artist_merge.json" 读取

/// 把校验通过的结构化结果转成提议。
/// id 越界或重复 → 错误；只对 same=true 且 confidence >= 0.5 的生成提议：
/// pickCanonical 两者，另一个作为别名，source Llm，confidence 取 LLM 值（夹到 [0,1]），reason 用
/// LLM 原文。
core::Result<QList<library::ArtistAliasProposal>> parseArtistMergeResult(const QJsonValue &value,
    const QList<ArtistMergeCandidatePair> &pairs, const QHash<qint64, ArtistEntry> &entriesById);

} // namespace linernotes::butler
