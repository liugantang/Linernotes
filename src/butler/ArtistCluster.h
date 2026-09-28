// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <cstdint>

namespace linernotes::butler {

enum class ArtistLinkKind : std::uint8_t { Exact, Romanized, Fuzzy };

struct ArtistEntry {
    qint64 artistId = 0;
    QString name { };
    int trackCount = 0;
};

struct ArtistLink {
    qint64 a = 0; // artistId
    qint64 b = 0;
    ArtistLinkKind kind = ArtistLinkKind::Exact;
};

struct ArtistCluster {
    QList<ArtistEntry> members; // 至少 2 个；按 trackCount 降序、再按名字
    QList<ArtistLink> links; // 只含 Exact / Romanized
};

struct ArtistClustering {
    QList<ArtistCluster> clusters; // 只由 Exact/Romanized 连通，规则可直接提议合并
    QList<ArtistLink> fuzzyCandidates; // Fuzzy 关系；两端不在同一个簇里才列出；需 LLM 确认
};

ArtistClustering clusterArtists(const QList<ArtistEntry> &entries);

} // namespace linernotes::butler
