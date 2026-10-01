// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace linernotes::butler {

struct ArtistEntry {
    qint64 artistId = 0;
    QString name { };
    int trackCount = 0;

    bool operator==(const ArtistEntry &) const = default;
};

struct ArtistGroup {
    QList<ArtistEntry> members; // 至少 2 个；按 trackCount 降序、再按名字
    /// true：组只由“名字 exactKey 相同”连起来，且所有成员 exactKey 长度 > 3。
    /// 这种组（繁简、大小写、标点差异）不经 LLM，规则直接提议。
    bool exactOnly = false;

    bool operator==(const ArtistGroup &) const = default;
};

struct ArtistGrouping {
    QList<ArtistGroup> groups;
    QList<QList<ArtistEntry>> oversized; // 成员数超过上限的组，调用方记日志后跳过
};

/// altNames：exactKey(艺人名) → 该名字的其他写法（来自署名解析的 aka 等）。
/// 每个实体的身份键 = exactKey(自身名字) ∪ { exactKey(n) | n ∈ altNames[exactKey(自身名字)] }
///                 ∪ { "r:" + romanKey(自身名字) }（非空时），
/// 空键忽略。有共同键的实体用并查集并成一组；只有 1 个成员的组不输出。
ArtistGrouping groupArtists(const QList<ArtistEntry> &entries,
    const QHash<QString, QStringList> &altNames, int maxGroupSize);

} // namespace linernotes::butler
