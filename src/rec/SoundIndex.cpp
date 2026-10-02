// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SoundIndex.h"

#include <QString>

#include <audio/EmbeddingIndex.h>
#include <audio/TrackEmbedding.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>

#include <memory>
#include <utility>

namespace linernotes::rec {

SoundIndex::SoundIndex(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<const audio::EmbeddingIndex *> SoundIndex::index()
{
    const library::EmbeddingStore store(m_db, m_clock);
    const QString model = QString(audio::kEmbeddingModelId);
    const auto countRes = store.analyzedCount(model);
    if (!countRes.ok()) {
        return countRes.error();
    }

    const int currentCount = countRes.value();
    if (m_index != nullptr && m_indexedCount == currentCount) {
        return m_index.get();
    }

    const auto loadRes = store.loadAll(model);
    if (!loadRes.ok()) {
        return loadRes.error();
    }

    const auto &embeddings = loadRes.value();
    const int dim
        = embeddings.isEmpty() ? 1024 : static_cast<int>(embeddings.first().vector.size());
    auto newIndex = std::make_unique<audio::EmbeddingIndex>(dim);
    for (const auto &se : embeddings) {
        newIndex->add(se.trackId, se.vector);
    }

    m_index = std::move(newIndex);
    m_indexedCount = currentCount;
    return m_index.get();
}

} // namespace linernotes::rec
