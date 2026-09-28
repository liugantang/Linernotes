// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <butler/ArtistGroup.h>
#include <butler/MusicBrainz.h>
#include <core/Result.h>
#include <library/ArtistAliasCorrections.h>

#include <cstdint>
#include <optional>

namespace linernotes::butler {

struct ArtistMergeMember {
    ArtistEntry entry;
    QStringList albums; // sampleAlbums，最多 3 个
    QStringList aka; // altNames 中该艺人的其他写法
    std::optional<MbArtist> mb;
    bool operator==(const ArtistMergeMember &) const = default;
};

struct ArtistMergeGroup {
    int id = 0; // 本次请求内的序号
    QList<ArtistMergeMember> members;
    bool operator==(const ArtistMergeGroup &) const = default;
};

/// 模板变量 groups：每组 id 与成员（artistId、名字、曲目数、示例专辑、aka）。
QHash<QString, QString> artistMergePromptVars(const QList<ArtistMergeGroup> &groups);

QJsonObject artistMergeSchema();

/// 结构化结果：{"groups":[{"id":0,"subsets":[{"members":[artistId,...],"confidence":0.9,"reason":"..."}]}]}
/// 每个 subset 是模型认定的同一实体。
/// - 顶层结构错误 → 错误；
/// - 组 id 不存在或重复、subset 中的 artistId 不属于该组或在该组的多个 subset 中重复出现 →
/// 跳过这个组（不报错）；
/// - 成员少于 2 个或 confidence < 0.5 的 subset 忽略；
/// - 其余 subset：pickCanonical 选规范艺人，其余成员作为它的别名，source Llm，confidence 夹到
/// [0,1]，reason 用原文。 没有出现在任何 subset 中的成员视为各自独立。
core::Result<QList<library::ArtistAliasProposal>> parseArtistMergeResult(
    const QJsonValue &value, const QList<ArtistMergeGroup> &groups);

} // namespace linernotes::butler
