// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QtGlobal>

#include <audio/EmbeddingIndex.h>
#include <core/Result.h>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {

class SoundIndex;

class SimilarTracks {
public:
    SimilarTracks(library::Database &db, SoundIndex &soundIndex);

    // 与 trackId 最相似的最多 k 首（按分数降序）。trackId 没有向量时返回空列表。
    [[nodiscard]] core::Result<QList<audio::Neighbor>> similarTo(qint64 trackId, int k);

    // 检查 trackId 是否存在向量
    [[nodiscard]] bool hasEmbedding(qint64 trackId);

private:
    library::Database &m_db;
    SoundIndex &m_soundIndex;
};

} // namespace linernotes::rec
