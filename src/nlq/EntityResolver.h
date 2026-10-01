// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <core/Result.h>
#include <library/Database.h>
#include <nlq/NlqQuery.h>

namespace linernotes::nlq {

struct ArtistCandidate {
    qint64 artistId = 0;
    QString name; // artists.name
    int trackCount = 0; // 可见曲目数
    bool operator==(const ArtistCandidate &) const = default;
};

struct Clarification {
    int conditionIndex = 0; // query.rule.conditions 中的下标
    QString mention; // 模型给出的名字
    QList<ArtistCandidate> candidates; // 按 trackCount 降序，最多 8 个
    bool operator==(const Clarification &) const = default;
};

struct Resolution {
    Query query; // 能确定的已改写
    QList<Clarification> clarifications; // 需要用户选择的
    bool operator==(const Resolution &) const = default;
};

core::Result<Resolution> resolveArtists(library::Database &db, const Query &query);

/// 用户选定后本地改写：该条件变为 artist is <choice.name>。
Query applyArtistChoice(const Query &query, int conditionIndex, const ArtistCandidate &choice);

} // namespace linernotes::nlq
